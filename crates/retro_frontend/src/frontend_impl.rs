use std::mem::MaybeUninit;
use crate::result::{Error, Result};
use libloading::Library;
use libretro_sys::*;

use once_cell::sync::Lazy;


/// The frontend implementation.
/// 
/// # Safety
/// Note that Libretro itself is not thread safe, so we do not try and pretend
/// that we are thread safe either.
pub(crate) static mut FRONTEND_IMPL : Lazy<FrontendStateImpl> = Lazy::new(|| {
	FrontendStateImpl::default()
});

/// Used to assert that another [crate::Frontend] wrapper object is not created
/// while this implementation layer isn't cleaned up.
pub(crate) fn assert_cleaned_up() {
	unsafe {
		assert!(FRONTEND_IMPL.core_library.is_none());
		assert!(FRONTEND_IMPL.current_core_api.is_none());
	}
}

#[derive(Default)]
pub(crate) struct FrontendStateImpl {

	/// The current core's libretro functions.
	current_core_api: Option<CoreAPI>,

	/// The current core library.
	core_library: Option<Box<Library>>,

}


impl FrontendStateImpl {
	// Just for testing. I'll write a much better one (that is actually useful) later
	unsafe extern "C" fn libretro_environment_callback(
		command: u32,
		_data: *mut std::ffi::c_void,
	) -> bool {
		println!("test environment callback called with command: {command}");
		false
	}

	/// Loads a core from the given path.
	///
	/// ```rust
	/// let mut fe = Frontend::new();
	/// fe.load_core("./cores/gbasp.so");
	/// ```
	pub(crate) fn load_core<P: AsRef<std::path::Path>>(&mut self, path: P) -> Result<()> {
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

			// Let's sanity check the libretro API version against bindings to make sure we can actually use this core.
			// If we can't then stop here.
			let api_version = (api_initalized.retro_api_version)();
			if api_version != libretro_sys::API_VERSION {
				return Err(Error::InvalidLibRetroAPI {
					expected: libretro_sys::API_VERSION,
					got: api_version,
				});
			}

			// Set required libretro callbacks. This is required to avoid a crash.
			(api_initalized.retro_set_environment)(Self::libretro_environment_callback);

			// Initalize the libretro core
			(api_initalized.retro_init)();

			self.core_library = Some(lib);
			self.current_core_api = Some(api_initalized);
		}

		Ok(())
	}

	pub(crate) fn unload_core(&mut self) {
		// First deinitalize the libretro core before unloading the library.
		if let Some(core_api) = &self.current_core_api {
			unsafe {
				(core_api.retro_deinit)();
			}
		} else {
			// Return early if we have no core API; this means we don't have 
			// a library either.
			return ();
		}

		// Unload the library. We don't worry about error handling right now, but
		// we could (at least, when not being dropped.)
		let lib = self.core_library.take().unwrap();
		let _ = lib.close();

		// FIXME: Do other various cleanup (when we need to do said cleanup)
	}
}
