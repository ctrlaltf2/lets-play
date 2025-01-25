use crate::input_devices::InputDevice;
use crate::libretro_callbacks;
use crate::libretro_core_variable::CoreVariable;
use crate::result::{Error, Result};
use ffi::CString;
use libloading::Library;
use libretro_sys::*;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::ffi;
use std::os::unix::ffi::OsStrExt;
use std::path::Path;
use std::{fs, mem::MaybeUninit};

use tracing::{error, info};

/// The currently running frontend.
///
/// # Safety
/// Libretro itself is not thread safe, so we do not try and pretend that we are.
/// Only one instance of Frontend can be active in an application.
pub(crate) static mut FRONTEND: *mut Frontend = std::ptr::null_mut();

/// Initalization data for HW OpenGL cores.
pub struct HwGlInitData {
	/// A pointer to a function to allow cores to request OpenGL extension functions.
	pub get_proc_address: *mut ffi::c_void,
}

/// Interface for the frontend to call to user code.
pub trait FrontendInterface {
	/// Called when video is updated.
	fn video_update(&mut self, slice: &[u32], pitch: u32);

	/// Called when video is updated and the core is using HW OpenGL rendering.
	fn video_update_gl(&mut self);

	/// Called when resize occurs.
	fn video_resize(&mut self, width: u32, height: u32);

	// TODO(lily): This should probably return the amount of consumed frames,
	// as in some cases that *might* differ?
	fn audio_sample(&mut self, slice: &[i16], size: usize);

	/// Called to poll input
	fn input_poll(&mut self);

	/// Initalize hardware accelerated rendering using OpenGL.
	/// If this returns [Option::None], then it is assumed that
	/// OpenGL initalization has failed.
	fn hw_gl_init(&mut self) -> Option<HwGlInitData>;
}

/// Per-core settings
#[derive(Serialize, Deserialize)]
struct CoreSettingsFile {
	#[serde(flatten)]
	variables: HashMap<String, CoreVariable>,
}

pub struct Frontend {
	/// The current core's libretro functions.
	pub(crate) core_api: Option<CoreAPI>,

	/// The current core library.
	pub(crate) core_library: Option<Box<Library>>,

	pub(crate) game_loaded: bool,

	pub(crate) av_info: Option<SystemAvInfo>,
	pub(crate) sys_info: Option<SystemInfo>,

	/// The core's requested pixel format.
	/// TODO: HW accel. (or just not care)
	pub(crate) pixel_format: PixelFormat,

	/// Converted pixel buffer. We store it here so we don't keep allocating over and over.
	pub(crate) converted_pixel_buffer: Option<Box<[u32]>>,

	// Framebuffer attributes. TODO: This really should be another struct or something
	// with members to make dealing with it less annoying.
	pub(crate) fb_width: u32,
	pub(crate) fb_height: u32,
	pub(crate) fb_pitch: u32,

	// HW OpenGL FBO id.
	pub(crate) gl_fbo_id: u32,

	/// The "system" directory. Used for BIOS roms.
	pub(crate) system_directory: CString,

	/// The save directory. Used for saves. (TODO: Should make this per-core!!!)
	pub(crate) save_directory: CString,

	/// The config directory. Stores configuration for each core.
	pub(crate) config_directory: String,

	/// Hashmap of core variables.
	pub(crate) variables: HashMap<String, CoreVariable>,

	/// Hashmap of connected input devices.
	pub(crate) input_devices: HashMap<u32 /* port */, *mut dyn InputDevice>,

	pub(crate) interface: *mut dyn FrontendInterface,
}

impl Frontend {
	/// Creates a new boxed frontend instance. Note that the returned [Box]
	/// must be held until this frontend is no longer used.
	pub fn new(interface: *mut dyn FrontendInterface) -> Box<Self> {
		let mut boxed = Box::new(Self {
			core_api: None,
			core_library: None,

			game_loaded: false,

			av_info: None,
			sys_info: None,

			pixel_format: PixelFormat::RGB565,
			converted_pixel_buffer: None,

			fb_width: 0,
			fb_height: 0,
			fb_pitch: 0,
			gl_fbo_id: 0,

			// TODO: We should let callers set these, probably.
			// For now, this is probably fine.
			system_directory: CString::new("system").unwrap(),
			save_directory: CString::new("save").unwrap(),
			config_directory: "config".into(),

			variables: HashMap::new(),

			input_devices: HashMap::new(),

			interface: interface,
		});

		// Assign to the global frontend pointer
		unsafe {
			assert!(FRONTEND.is_null(), "Cannot have multiple frontends.");
			FRONTEND = &mut *boxed as *mut Frontend;
		}

		boxed
	}

	pub fn core_loaded(&self) -> bool {
		// Ideally this logic could be simplified but just to make sure..
		self.core_library.is_some() && self.core_api.is_some()
	}

	/// Plugs in an input device to the specified port.
	pub fn plug_input_device(&mut self, port: u32, device: *mut dyn InputDevice) {
		if self.core_loaded() {
			let core_api = self.core_api.as_mut().unwrap();

			unsafe {
				(core_api.retro_set_controller_port_device)(port, (*device).device_type());
			}

			if !self.input_devices.contains_key(&port) {
				self.input_devices.insert(port, device);
			} else {
				(*self.input_devices.get_mut(&port).unwrap()) = device;
			}
		}
	}

	/// Unplugs a input device from the given port.
	pub fn unplug_input_device(&mut self, port: u32) {
		if self.core_loaded() {
			let core_api = self.core_api.as_mut().unwrap();

			unsafe {
				(core_api.retro_set_controller_port_device)(port, DEVICE_NONE);
			}

			if !self.input_devices.contains_key(&port) {
				self.input_devices.remove(&port);
			}
		}
	}

	fn get_config_file_path(&mut self) -> Result<String> {
		let system_info = self.get_system_info()?;

		// SAFETY: libretro declares that the pointers inside of the SystemInfo structure
		// must always point to valid constant data. If it doesn't then other frontends
		// would probably blow up too.
		let path = unsafe {
			#[cfg(debug_assertions)]
			assert!(
				!system_info.library_name.is_null(),
				"Core library name is somehow null"
			);

			let c_name = ffi::CStr::from_ptr(system_info.library_name);

			format!(
				"{}/{}.toml",
				self.config_directory,
				c_name.to_str().expect("ughh")
			)
		};

		Ok(path)
	}

	// TODO: make this a bit less janky (and use Results)

	pub fn load_settings(&mut self) {
		let path_string = self
			.get_config_file_path()
			.expect("Could not get config file path");
		let path: &Path = path_string.as_ref();

		match path.try_exists() {
			Ok(exists) => {
				if exists {
					let data = fs::read_to_string(path).expect("Could not read config");
					let config =
						toml::from_str::<CoreSettingsFile>(&data).expect("Could not parse config");
					self.variables = config.variables;
				} else {
					// Save the core's initial settings to disk
					self.save_settings();
				}
			}
			Err(e) => {
				error!("Can't seem to read {}: {}", path.display(), e);
			}
		}
	}

	pub fn save_settings(&mut self) {
		let path = self
			.get_config_file_path()
			.expect("Could not get config file path");

		let settings = CoreSettingsFile {
			variables: self.variables.clone(),
		};

		let string = toml::to_string(&settings).expect("Could not serialize settings");
		fs::write(path.clone(), string).expect("Could not save settings to disk");

		info!("Saved settings to {path}");
	}

	pub fn load_core<P: AsRef<Path>>(&mut self, path: P) -> Result<()> {
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
			// If we can't, then fail the load.
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

			self.core_library = Some(lib);
			self.core_api = Some(core_api);

			let core_api_ref = self.core_api.as_ref().unwrap();

			// Set required libretro callbacks before calling libretro_init.
			// Some cores expect some callbacks to be set before libretro_init is called,
			// some cores don't. For maximum compatibility, pamper the cores which do.
			(core_api_ref.retro_set_environment)(libretro_callbacks::environment_callback);

			// Initalize the libretro core. We do this first because
			// there are a Few cores which initalize resources that later
			// are poked by the later callback setting that could break if we don't.
			(core_api_ref.retro_init)();

			// Set more libretro callbacks now that we have initalized the core.
			(core_api_ref.retro_set_video_refresh)(libretro_callbacks::video_refresh_callback);
			(core_api_ref.retro_set_input_poll)(libretro_callbacks::input_poll_callback);
			(core_api_ref.retro_set_input_state)(libretro_callbacks::input_state_callback);
			(core_api_ref.retro_set_audio_sample_batch)(
				libretro_callbacks::audio_sample_batch_callback,
			);
			(core_api_ref.retro_set_audio_sample)(libretro_callbacks::audio_sample_callback);

			info!("Core {} loaded", path.as_ref().display());
		}

		Ok(())
	}

	pub fn unload_core(&mut self) -> Result<()> {
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

		self.fb_width = 0;
		self.fb_height = 0;
		self.fb_pitch = 0;
		self.converted_pixel_buffer = None;

		// disconnect all currently connected joypads
		self.input_devices.clear();

		Ok(())
	}

	pub fn load_game<P: AsRef<Path>>(&mut self, path: P) -> Result<()> {
		if !self.core_loaded() {
			return Err(Error::CoreNotLoaded);
		}

		// I'm aware this is nasty but bleh
		let slice = path.as_ref().as_os_str().as_bytes();
		let path_string = CString::new(slice).expect("shouldn't fail");

		let system_info = self.get_system_info()?;

		let core_api = self.core_api.as_ref().unwrap();

		let mut gameinfo = GameInfo {
			path: path_string.as_ptr(),
			data: std::ptr::null(),
			size: 0,
			meta: std::ptr::null(),
		};

		// If the core does not need fullpath, then
		// read the file data into a buffer we give to the core.
		// This is pretty wasteful but works.
		if !system_info.need_fullpath {
			let contents = fs::read(path)?;
			gameinfo.data = contents.as_ptr() as *const ffi::c_void;
			gameinfo.size = contents.len();

			unsafe {
				if !(core_api.retro_load_game)(&gameinfo) {
					return Err(Error::RomLoadFailed);
				}

				self.game_loaded = true;
				Ok(())
			}
		} else {
			unsafe {
				if !(core_api.retro_load_game)(&gameinfo) {
					return Err(Error::RomLoadFailed);
				}

				self.game_loaded = true;
				Ok(())
			}
		}
	}

	pub fn unload_game(&mut self) -> Result<()> {
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

	pub fn get_av_info(&mut self) -> Result<SystemAvInfo> {
		if !self.core_loaded() {
			return Err(Error::CoreNotLoaded);
		}

		if let Some(av) = self.av_info.as_ref() {
			Ok(av.clone())
		} else {
			// Get AV info
			// Like core API, we have to MaybeUninit again.
			let mut av_info: MaybeUninit<SystemAvInfo> = MaybeUninit::uninit();
			unsafe {
				let core_api = self.core_api.as_ref().unwrap();
				(core_api.retro_get_system_av_info)(av_info.as_mut_ptr());

				self.av_info = Some(av_info.assume_init());
			}

			Ok(self.av_info.as_ref().unwrap().clone())
		}
	}

	pub fn get_system_info(&mut self) -> Result<SystemInfo> {
		if !self.core_loaded() {
			return Err(Error::CoreNotLoaded);
		}

		if let Some(sys) = self.sys_info.as_ref() {
			Ok(sys.clone())
		} else {
			let mut sys_info: MaybeUninit<SystemInfo> = MaybeUninit::uninit();

			// Actually get the system info
			unsafe {
				let core_api = self.core_api.as_ref().unwrap();
				(core_api.retro_get_system_info)(sys_info.as_mut_ptr());

				self.sys_info = Some(sys_info.assume_init());
			}

			Ok(self.sys_info.as_ref().unwrap().clone())
		}
	}

	pub fn get_size(&mut self) -> (u32, u32) {
		(self.fb_width, self.fb_height)
	}

	pub fn set_gl_fbo(&mut self, id: u32) {
		self.gl_fbo_id = id;
	}

	pub fn reset(&mut self) {
		let core_api = self.core_api.as_ref().unwrap();

		unsafe {
			(core_api.retro_reset)();
		}
	}

	pub fn run_frame(&mut self) {
		let core_api = self.core_api.as_ref().unwrap();

		unsafe {
			(core_api.retro_run)();
		}
	}
}

impl Drop for Frontend {
	fn drop(&mut self) {
		// Null out the global frontend pointer first,
		// so any attempted UAF will instead result in a segfault
		unsafe {
			assert!(!FRONTEND.is_null());
			FRONTEND = std::ptr::null_mut();
		}

		if self.core_loaded() {
			let _ = self.unload_core();
		}
	}
}
