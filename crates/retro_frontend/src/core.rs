//! A "RAII" wrapper over a core. Mostly for having a play around with
//! a more rustic approach to app state.

use std::path::Path;

use crate::result::Result;
use crate::frontend;

/// A "RAII" wrapper over a core, useful for making cleanup a bit less ardous.
pub struct Core();

impl Core {
	/// Same as [frontend::load_core], but returns a struct which will keep the core
	/// alive until it is dropped.
	pub fn load<P: AsRef<Path>>(path: P) -> Result<Self> {
		frontend::load_core(path.as_ref())?;
		Ok(Self {})
	}

	/// Same as [frontend::load_rom].
	pub fn load_rom<P: AsRef<Path>>(&mut self, rom_path: P) -> Result<()> {
		frontend::load_rom(rom_path)?;
		Ok(())
	}
}

impl Drop for Core {
	fn drop(&mut self) {
		let _ = frontend::unload_core();
	}
}
