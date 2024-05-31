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
pub fn unload_core() {
	unsafe { FRONTEND_IMPL.unload_core() }
}
