//! The primary frontend API.
//! This is a singleton API, not by choice, but due to Libretro's design.
//!
//! # Safety
//! Don't even think about using this across multiple threads. If you want to run multiple frontends,
//! it's easier to just host this crate in a runner process and fork off those runners.
use crate::frontend_impl::FRONTEND_IMPL;
use crate::result::Result;
use crate::libretro_sys::*;

pub fn set_video_update_callback(cb: impl FnMut(&[u32]) + 'static) {
	unsafe {
		FRONTEND_IMPL.set_video_update_callback(cb);
	}
}

/// Loads a core from the given path into the global frontend state.
///
/// ```rust
/// use retro_frontend::frontend;
/// frontend::load_core("./cores/gbasp.so");
/// ```
pub fn load_core<P: AsRef<std::path::Path>>(path: P) -> Result<()> {
	unsafe { FRONTEND_IMPL.load_core(path) }
}

/// Unloads the core currently running in the global frontend state.
///
/// ```rust
/// use retro_frontend::frontend;
/// frontend::unload_core();
/// ```
pub fn unload_core() -> Result<()> {
	unsafe { FRONTEND_IMPL.unload_core() }
}

/// Loads a ROM into the given core. This function requires that [load_core] has been called and has succeeded first.
///
/// ```rust
/// use retro_frontend::frontend;
/// frontend::load_rom("./roms/sma2.gba");
/// ```
pub fn load_game<P: AsRef<std::path::Path>>(path: P) -> Result<()> {
	unsafe { FRONTEND_IMPL.load_game(path) }
}

/// Unloads a ROM from the given core.
pub fn unload_game() -> Result<()> {
	unsafe { FRONTEND_IMPL.unload_game() }
}



/// Get's the core's current AV information.
pub fn get_av_info() -> Result<SystemAvInfo> {
	unsafe { FRONTEND_IMPL.get_av_info() }
}

pub fn get_size() -> (u32, u32) {
	unsafe {  FRONTEND_IMPL.get_size() }
}

/// Runs the loaded core for one video frame.
pub fn run_frame() {
	unsafe { FRONTEND_IMPL.run() }
}
