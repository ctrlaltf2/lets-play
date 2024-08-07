use crate::libretro_sys_new::*;

pub fn bytes_per_pixel_from_libretro(pf: PixelFormat) -> u32 {
	match pf {
		PixelFormat::ARGB1555 | PixelFormat::RGB565 => 2,
		PixelFormat::ARGB8888 => 4,
	}
}

/// Boilerplate code for dealing with NULL/otherwise terminated arrays,
/// which converts them into a Rust slice.
///
/// We rely on a user-provided callback currently to determine when iteration is complete.
/// This *could* be replaced with a object-safe trait (and a constraint to allow us to use said trait) to codify
/// the expected "end conditions" of a terminated array of a given type, but for now, the callback works.
pub fn terminated_array<'a, T>(ptr: *const T, end_fn: impl Fn(&T) -> bool) -> &'a [T] {
	// Make sure the array pointer itself isn't null. Strictly speaking, this check should be done
	// *before* this is called by the user, but to avoid anything going haywire
	// we additionally check here.
	assert!(
		!ptr.is_null(),
		"pointer to array given to terminated_array! cannot be null"
	);

	unsafe {
		let mut iter = ptr.clone();
		let mut len: usize = 0;

		loop {
			let item = iter.as_ref().unwrap();

			if end_fn(item) {
				break;
			}

			len += 1;
			iter = iter.add(1);
		}

		std::slice::from_raw_parts(ptr, len)
	}
}

/*
#[doc(hidden)]
#[macro_export]
macro_rules! __terminated_array {
	($pex:ident, $lex:expr $(,)?) => {
		$crate::util::__terminated_array_impl($pex, $lex)
	};
}

#[doc(inline)]
pub use __terminated_array as terminated_array;
*/
