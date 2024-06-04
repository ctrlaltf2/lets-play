use crate::libretro_callbacks;
use crate::result::{Error, Result};
use ffi::CString;
use libloading::Library;
use libretro_sys::*;
use once_cell::sync::Lazy;
use std::os::unix::ffi::OsStrExt;
use std::path::Path;
use std::{fs, mem::MaybeUninit};

use std::ffi;

use tracing::{error, info};

/// The frontend implementation.
///
/// # Safety
/// Note that Libretro itself is not thread safe, so we do not try and pretend
/// that we are thread safe either.
pub(crate) static mut FRONTEND_IMPL: Lazy<FrontendStateImpl> =
	Lazy::new(|| FrontendStateImpl::new());

pub(crate) type UpdateCallback = dyn FnMut(&[u32]);

pub(crate) struct FrontendStateImpl {
	/// The current core's libretro functions.
	pub(crate) core_api: Option<CoreAPI>,

	/// The current core library.
	pub(crate) core_library: Option<Box<Library>>,

	pub(crate) game_loaded: bool,

	pub(crate) av_info: Option<SystemAvInfo>,

	/// Core requested pixel format.
	pub(crate) pixel_format: PixelFormat,

	// Converted pixel buffer. We store it here so we don't keep allocating over and over.
	pub(crate) converted_pixel_buffer: Vec<u32>,

	pub(crate) fb_width: u32,
	pub(crate) fb_height: u32,
	pub(crate) fb_pitch: u32,

	// Callbacks that consumers can set
	pub(crate) video_update_callback: Option<Box<UpdateCallback>>,
}

impl FrontendStateImpl {
	fn new() -> Self {
		Self {
			core_api: None,
			core_library: None,

			game_loaded: false,

			av_info: None,

			pixel_format: PixelFormat::RGB565,
			converted_pixel_buffer: Vec::new(),

			fb_width: 0,
			fb_height: 0,
			fb_pitch: 0,

			video_update_callback: None,
		}
	}

	pub(crate) fn core_loaded(&self) -> bool {
		// Ideally this logic could be simplified but just to make sure..
		self.core_library.is_some() && self.core_api.is_some()
	}

	pub(crate) fn set_video_update_callback(&mut self, cb: impl FnMut(&[u32]) + 'static) {
		self.video_update_callback = Some(Box::new(cb));
	}

	pub(crate) fn load_core<P: AsRef<Path>>(&mut self, path: P) -> Result<()> {
		if self.core_loaded() {
			return Err(Error::CoreAlreadyLoaded);
		}

		unsafe {
			let lib = Box::new(Library::new(path.as_ref())?);

			// bleh; CoreAPI doesn't implement Default so I can't do this in a "good" way
			let mut api_uninitialized: MaybeUninit<CoreAPI> = MaybeUninit::zeroed();
			let api_ptr = api_uninitialized.as_mut_ptr();

			// helper for DRY reasons
			macro_rules! load_symbol {
				($name:ident) => {
					(*api_ptr).$name = *(lib.get(stringify!($name).as_bytes())?);
				};
			}

			load_symbol!(retro_set_environment);
			load_symbol!(retro_set_video_refresh);
			load_symbol!(retro_set_audio_sample);
			load_symbol!(retro_set_audio_sample_batch);
			load_symbol!(retro_set_input_poll);
			load_symbol!(retro_set_input_state);
			load_symbol!(retro_init);
			load_symbol!(retro_deinit);
			load_symbol!(retro_api_version);
			load_symbol!(retro_get_system_info);
			load_symbol!(retro_get_system_av_info);
			load_symbol!(retro_set_controller_port_device);
			load_symbol!(retro_reset);
			load_symbol!(retro_run);
			load_symbol!(retro_serialize_size);
			load_symbol!(retro_serialize);
			load_symbol!(retro_unserialize);
			load_symbol!(retro_cheat_reset);
			load_symbol!(retro_cheat_set);
			load_symbol!(retro_load_game);
			load_symbol!(retro_load_game_special);
			load_symbol!(retro_unload_game);
			load_symbol!(retro_get_region);
			load_symbol!(retro_get_memory_data);
			load_symbol!(retro_get_memory_size);

			// If we get here, then we have initalized all the core API without failing.
			// We can now get an initalized CoreAPI.
			let core_api = api_uninitialized.assume_init();

			// Let's sanity check the libretro API version against bindings to make sure we can actually use this core.
			// If we can't then fail the load.
			let api_version = (core_api.retro_api_version)();
			if api_version != libretro_sys::API_VERSION {
				error!(
					"Core {} has invalid API version {api_version}; refusing to continue loading",
					path.as_ref().display()
				);
				return Err(Error::InvalidLibRetroAPI {
					expected: libretro_sys::API_VERSION,
					got: api_version,
				});
			}

			// Set required libretro callbacks before calling libretro_init.
			// Some cores expect some callbacks to be set before libretro_init is called,
			// some cores don't. For maximum compatibility, pamper the cores which do.
			(core_api.retro_set_environment)(libretro_callbacks::environment_callback);

			// Initalize the libretro core. We do this first because
			// there are a Few cores which initalize resources that later
			// are poked by the later callback setting that could break if we don't.
			(core_api.retro_init)();

			// Set more libretro callbacks now that we have initalized the core.
			(core_api.retro_set_video_refresh)(libretro_callbacks::video_refresh_callback);
			(core_api.retro_set_input_poll)(libretro_callbacks::input_poll_callback);
			(core_api.retro_set_input_state)(libretro_callbacks::input_state_callback);
			(core_api.retro_set_audio_sample_batch)(
				libretro_callbacks::audio_sample_batch_callback,
			);

			info!("Core {} loaded", path.as_ref().display());

			// Get AV info
			// Like core API, we have to MaybeUninit again.
			let mut av_info: MaybeUninit<SystemAvInfo> = MaybeUninit::uninit();
			(core_api.retro_get_system_av_info)(av_info.as_mut_ptr());

			self.av_info = Some(av_info.assume_init());

			self.core_library = Some(lib);
			self.core_api = Some(core_api);
		}

		Ok(())
	}

	pub(crate) fn unload_core(&mut self) -> Result<()> {
		if !self.core_loaded() {
			return Err(Error::CoreNotLoaded);
		}

		if self.game_loaded {
			self.unload_game()?;
		}

		// First deinitalize the libretro core before unloading the library.
		if let Some(core_api) = &self.core_api {
			unsafe {
				(core_api.retro_deinit)();
			}
		}

		// Unload the library. We don't worry about error handling right now, but
		// we could.
		let lib = self.core_library.take().unwrap();
		lib.close()?;

		self.core_api = None;
		self.core_library = None;

		// FIXME: Do other various cleanup (when we need to do said cleanup)
		self.av_info = None;

		Ok(())
	}

	pub(crate) fn load_game<P: AsRef<Path>>(&mut self, path: P) -> Result<()> {
		if !self.core_loaded() {
			return Err(Error::CoreNotLoaded);
		}

		// For now I'm only implementing the gameinfo garbage that
		// makes you read the whole file in. Later on I'll look into VFS
		// support; but for now, it seems more cores will probably
		// play ball with this.. which sucks :(

		// I'm aware this is nasty but bleh
		let slice = path.as_ref().as_os_str().as_bytes();
		let path_string = CString::new(slice).expect("shouldn't fail");
		let contents = fs::read(path)?;

		let gameinfo = GameInfo {
			path: path_string.as_ptr(),
			data: contents.as_ptr() as *const ffi::c_void,
			size: contents.len(),
			meta: std::ptr::null(),
		};

		let core_api = self.core_api.as_ref().unwrap();

		unsafe {
			if !(core_api.retro_load_game)(&gameinfo) {
				return Err(Error::RomLoadFailed);
			}

			self.game_loaded = true;
			Ok(())
		}
	}

	pub(crate) fn unload_game(&mut self) -> Result<()> {
		if !self.core_loaded() {
			return Err(Error::CoreNotLoaded);
		}

		let core_api = self.core_api.as_ref().unwrap();

		if self.game_loaded {
			unsafe {
				(core_api.retro_unload_game)();
			}

			self.game_loaded = false;
		}

		Ok(())
	}

	pub(crate) fn get_size(&mut self) -> (u32, u32) {
		(self.fb_width, self.fb_height)
	}

	pub(crate) fn run(&mut self) {
		let core_api = self.core_api.as_ref().unwrap();

		unsafe {
			(core_api.retro_run)();
		}
	}
}
