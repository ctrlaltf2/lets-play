use crate::result::{Error, Result};
use libloading::Library;
use libretro_sys::*;
use once_cell::sync::Lazy;
use std::ffi::CString;
use std::os::unix::ffi::OsStrExt;
use std::path::Path;
use std::{fs, mem::MaybeUninit};

use tracing::{error, info, warn};

/// The frontend implementation.
///
/// # Safety
/// Note that Libretro itself is not thread safe, so we do not try and pretend
/// that we are thread safe either.
pub(crate) static mut FRONTEND_IMPL: Lazy<FrontendStateImpl> =
	Lazy::new(|| FrontendStateImpl::default());

pub(crate) type RefreshCallback = dyn FnMut(&[u8], u32, u32, u32);
pub(crate) type SetPixelFormatCallback = dyn FnMut(PixelFormat);

#[derive(Default)]
pub(crate) struct FrontendStateImpl {
	/// The current core's libretro functions.
	core_api: Option<CoreAPI>,

	/// The current core library.
	core_library: Option<Box<Library>>,

	// Callbacks:

	video_refresh_callback: Option<Box<RefreshCallback>>,
	video_set_pixel_format_callback: Option<Box<SetPixelFormatCallback>>
}

impl FrontendStateImpl {
	unsafe extern "C" fn libretro_environment_callback(
		environment_command: u32,
		data: *mut std::ffi::c_void,
	) -> bool {
		match environment_command {
			ENVIRONMENT_GET_CAN_DUPE => {
				*(data as *mut bool) = true;
				return true;
			}

			ENVIRONMENT_SET_PIXEL_FORMAT => {
				let _pixel_format = *(data as *const std::ffi::c_uint);
				let pixel_format = PixelFormat::from_uint(_pixel_format).unwrap();

				if let Some(video_set_pixel_format) = &mut FRONTEND_IMPL.video_set_pixel_format_callback {
					video_set_pixel_format(pixel_format);
				}

				return true;
			}

			ENVIRONMENT_GET_VARIABLE => {
				// Make sure the core actually is giving us a pointer
				if data.is_null() {
					return false;
				}

				let var = (data as *mut Variable).as_mut().unwrap();

				match std::ffi::CStr::from_ptr(var.key).to_str() {
					Ok(key) => {
						info!("Core wants to get variable \"{}\" from us", key);
					}
					Err(_err) => {
						// Maybe notify about this.
						return false;
					}
				}
			}

			_ => {
				warn!("Environment callback called with unhandled command: {environment_command}");
			}
		}

		false
	}

	unsafe extern "C" fn libretro_video_refresh(
		pixels: *const std::ffi::c_void,
		width: std::ffi::c_uint,
		height: std::ffi::c_uint,
		pitch: usize,
	) {
		warn!("Video refresh called");
		// TODO:..

		//if let Some(video_refresh) = &mut FRONTEND_IMPL.video_refresh_callback {
			//video_refresh()
		//}

	}

	pub(crate) fn core_loaded(&self) -> bool {
		// Ideally this logic could be simplified but just to make sure..
		self.core_library.is_some() && self.core_api.is_some()
	}

	pub(crate) fn set_video_refresh_callback(&mut self, cb: impl FnMut(&[u8], u32, u32, u32) + 'static) {
		self.video_refresh_callback = Some(Box::new(cb));
	}

	pub(crate) fn set_video_pixel_format_callback(&mut self, cb: impl FnMut(PixelFormat) + 'static) {
		self.video_set_pixel_format_callback = Some(Box::new(cb));
	}

	pub(crate) fn load_core<P: AsRef<Path>>(&mut self, path: P) -> Result<()> {
		// TODO: If path already has _libretro.[platform_extension], don't add it,
		// but if it doesn't, add it

		// Make sure to unload and deinitalize an existing core.
		// If this fails, we will probably end up in a unclean state, so
		// /shrug.
		if self.core_loaded() {
			self.unload_core()?;
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
			// Let's make sure we have some sanity first!

			let core_api = api_uninitialized.assume_init();

			// Let's sanity check the libretro API version against bindings to make sure we can actually use this core.
			// If we can't then stop here.
			let api_version = (core_api.retro_api_version)();
			if api_version != libretro_sys::API_VERSION {
				error!(
					"Core {} has invalid API version {api_version}",
					path.as_ref().display()
				);
				return Err(Error::InvalidLibRetroAPI {
					expected: libretro_sys::API_VERSION,
					got: api_version,
				});
			}

			// Set required libretro callbacks. This is required to avoid a crash.
			(core_api.retro_set_environment)(Self::libretro_environment_callback);
			(core_api.retro_set_video_refresh)(Self::libretro_video_refresh);

			// Initalize the libretro core
			(core_api.retro_init)();

			info!("Core {} loaded", path.as_ref().display());

			self.core_library = Some(lib);
			self.core_api = Some(core_api);
		}

		Ok(())
	}

	pub(crate) fn unload_core(&mut self) -> Result<()> {
		if !self.core_loaded() {
			return Err(Error::CoreNotLoaded);
		}

		// First deinitalize the libretro core before unloading the library.
		if let Some(core_api) = &self.core_api {
			unsafe {
				(core_api.retro_deinit)();
			}
		}

		// Unload the library. We don't worry about error handling right now, but
		// we could (at least, when not being dropped.)
		let lib = self.core_library.take().unwrap();
		lib.close()?;

		self.core_api = None;
		self.core_library = None;

		// FIXME: Do other various cleanup (when we need to do said cleanup)

		Ok(())
	}

	pub(crate) fn load_rom<P: AsRef<Path>>(&mut self, path: P) -> Result<()> {
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
			data: contents.as_ptr() as *const std::ffi::c_void,
			size: contents.len(),
			meta: std::ptr::null(),
		};

		let core_api = self.core_api.as_ref().unwrap();

		unsafe {
			if !(core_api.retro_load_game)(&gameinfo) {
				return Err(Error::RomLoadFailed);
			}

			Ok(())
		}
	}
}
