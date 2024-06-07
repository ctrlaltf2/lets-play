use std::path::Path;

use crate::frontend;
use crate::result::Result;

/// A "RAII" wrapper over a core, useful for making cleanup a bit less ardous.
pub struct Core();

impl Core {
	/// Same as [frontend::load_core], but returns a struct which will keep the core
	/// alive until it is dropped.
	pub fn load<P: AsRef<Path>>(path: P) -> Result<Self> {
		frontend::load_core(path.as_ref())?;
		Ok(Self {})
	}

	/// Same as [frontend::load_game].
	pub fn load_game<P: AsRef<Path>>(&mut self, rom_path: P) -> Result<()> {
		frontend::load_game(rom_path)?;
		Ok(())
	}

	/// Same as [frontend::unload_game].
	pub fn unload_game(&mut self) -> Result<()> {
		frontend::unload_game()?;
		Ok(())
	}
}

impl Drop for Core {
	fn drop(&mut self) {
		let _ = frontend::unload_core();
	}
}
