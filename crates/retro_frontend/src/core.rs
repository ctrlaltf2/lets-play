//! A "RAII" wrapper over a core. Mostly for having a play around with
//! a more rustic approach to app state.

use std::path::Path;

/// A "RAII" wrapper over a core. Kind of bleh, but it should make manual cleanup better.
pub struct Core;

impl Core {

	fn load<P: AsRef<Path>>(path: P) -> crate::result::Result<Self> {
		crate::frontend::load_core(path.as_ref())?;
		Ok(Self {

		})
	}

	fn load_rom<P: AsRef<Path>>(&mut self, rom_path: P) -> crate::result::Result<()> {
		crate::frontend::load_rom(rom_path)?;
		Ok(())
	}

}

impl Drop for Core {
	fn drop(&mut self) {
		let _ = crate::frontend::unload_core();
	}
}
