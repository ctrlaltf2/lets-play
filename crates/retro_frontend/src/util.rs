use crate::libretro_sys_new::*;

pub fn bytes_per_pixel_from_libretro(pf: PixelFormat) -> u32 {
	match pf {
		PixelFormat::ARGB1555 | PixelFormat::RGB565 => 2,
		PixelFormat::ARGB8888 => 4,
	}
}
