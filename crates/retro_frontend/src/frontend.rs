use std::mem::MaybeUninit;

use crate::result::{Error, Result};
use libloading::Library;
use libretro_sys::*;

/// A libretro frontend. Only one of these can be running inside a Rust process.
/// (TODO: Actually enforce this invariant at runtime.)
pub struct Frontend {
	/// The current core's libretro functions.
	current_core_api: Option<CoreAPI>,

	/// The current core library.
	core_library: Option<Box<Library>>,
}

impl Frontend {
	pub fn new() -> Self {
		Self {
			current_core_api: None,
			core_library: None,
		}
	}

	// Just for testing.
	unsafe extern "C" fn libretro_environment_callback(
		command: u32,
		_data: *mut std::ffi::c_void,
	) -> bool {
		println!("test environment callback called with command: {}", command);
		false
	}

	/// Loads a core from the given path.
	///
	/// ```rust
	/// let mut fe = Frontend::new();
	/// fe.load_core("./cores/gbasp.so");
	/// ```
	pub fn load_core<P: AsRef<std::path::Path>>(&mut self, path: P) -> Result<()> {
		unsafe {
			let lib = Box::new(Library::new(path.as_ref())?);

			// bleh; CoreAPI doesn't implement Default so I can't do this in a "good" way
			let mut api: MaybeUninit<CoreAPI> = MaybeUninit::zeroed();
			let api_ptr = api.as_mut_ptr();

			// helper so I can just list off individual symbol names
			// and not have to repeat it constantly.
			macro_rules! load_symbol {
				($name:ident) => {
					//println!("Loading {}", stringify!($name));
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

			let api_initalized = api.assume_init();

			// Sanity check the libretro API version against bindings to make sure we can actually use this core.
			let api_version = (api_initalized.retro_api_version)();
			if api_version != libretro_sys::API_VERSION {
				return Err(Error::InvalidLibRetroAPI {
					expected: libretro_sys::API_VERSION,
					got: api_version,
				});
			}

			(api_initalized.retro_set_environment)(Self::libretro_environment_callback);

			// Initalize the libretro core
			(api_initalized.retro_init)();

			self.core_library = Some(lib);
			self.current_core_api = Some(api_initalized);
		}

		Ok(())
	}

	pub fn unload_core(&mut self) {
		// Deinitalize the libretro core before unloading it.
		if let Some(core_api) = &self.current_core_api {
			unsafe {
				(core_api.retro_deinit)();
			}
		}

		// Unload the library. We don't worry about error handling right now, but
		// we could (at least when not being dropped.)
		let lib = self.core_library.take().unwrap();
		let _ = lib.close();
	}
}

impl Drop for Frontend {
	fn drop(&mut self) {
		// Unload the core if we have one loaded when we're being dropped.
		if self.core_library.is_some() {
			self.unload_core();
		}
	}
}
