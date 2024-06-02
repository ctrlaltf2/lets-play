//! The primary frontend API.
//! This is a singleton API, not by choice, but due to Libretro's design.
//!
//! # Safety
//! Don't even think about using this across multiple threads. If you want to run multiple frontends,
//! it's easier to just host this crate in a runner process and fork off those runners.
use crate::frontend_impl::FRONTEND_IMPL;
use crate::result::Result;

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
pub fn load_rom<P: AsRef<std::path::Path>>(path: P) -> Result<()> {
	unsafe { FRONTEND_IMPL.load_rom(path) }
}
