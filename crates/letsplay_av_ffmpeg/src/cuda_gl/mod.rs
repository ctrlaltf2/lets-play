//! Bindings to CUDA OpenGL interopability functionality,
//! since cudarc doesn't provide it at all.

#[allow(non_snake_case)]
pub mod sys;
use sys::*;

pub mod safe;

pub unsafe fn lib() -> &'static Lib {
	static LIB: std::sync::OnceLock<Lib> = std::sync::OnceLock::new();
	LIB.get_or_init(|| {
		if let Ok(lib) = Lib::new(libloading::library_filename("cuda")) {
			return lib;
		}
		panic!("cuda library doesn't exist.");
	})
}
