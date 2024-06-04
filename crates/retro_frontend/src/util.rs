use crate::libretro_sys;

pub fn bytes_per_pixel_from_libretro(pf: libretro_sys::PixelFormat) -> u32 {
	match pf {
		libretro_sys::PixelFormat::ARGB1555 | libretro_sys::PixelFormat::RGB565 => 2,
		libretro_sys::PixelFormat::ARGB8888 => 4,
	}
}
