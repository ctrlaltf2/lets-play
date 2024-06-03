use retro_frontend::libretro_sys::PixelFormat;

pub(crate) struct App {

	/// The current pixel format
	pub current_pixel_format: PixelFormat
}

impl App {
	pub(crate) fn new() -> Self {
		Self {
			current_pixel_format: PixelFormat::RGB565
		}
	}
}
