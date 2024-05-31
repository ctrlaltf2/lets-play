use crate::frontend_impl;
use crate::result::Result;

/// A libretro frontend.
pub struct Frontend;

impl Frontend {
	pub fn new() -> Self {
		// Make sure that we cannot create another frontend wrapper
		// while another one is active.
		frontend_impl::assert_cleaned_up();

		Self {
		}
	}

	pub fn load_core<P: AsRef<std::path::Path>>(&mut self, path: P) -> Result<()> {
		unsafe {
			frontend_impl::FRONTEND_IMPL.load_core(path)
		}
	}

	pub fn unload_core(&mut self) {
		unsafe {
			frontend_impl::FRONTEND_IMPL.unload_core()
		}
	}
	
}

impl Drop for Frontend {
	fn drop(&mut self) {
		self.unload_core()
	}
}

