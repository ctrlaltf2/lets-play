use minifb::{Key, Window, WindowOptions};

/// A wrapper over minifb
pub struct AppWindow {
	window: Option<Window>,

	// sw framebuffer
	width: u16,
	height: u16,
	framebuffer: Vec<u32>,
}

impl AppWindow {
	pub fn new() -> Self {
		Self {
			window: None,
			width: 0,
			height: 0,
			framebuffer: Vec::new(),
		}
	}

	pub fn resize(&mut self, width: u16, height: u16) {
		let len = (width as usize) * (height as usize);

		self.width = width as u16;
		self.height = height as u16;

		self.framebuffer.resize(len, 0);

		let window = Window::new(
			"RetroDemo - retro_frontend demo (Press Esc to exit)",
			width as usize,
			height as usize,
			WindowOptions {
				scale: minifb::Scale::X1,
				..Default::default()
			},
		)
		.expect("Could not open window");

		// note that we don't set a FPS limit now since the main loop limits itself.
		self.window = Some(window);
	}

	pub fn update_buffer(&mut self, slice: &[u32], pitch: u32, from_opengl: bool) {
		if self.window.is_none() {
			return;
		}

		let has_disconnected_pitch = pitch != self.width as u32;

		// If this frame came from OpenGL we need to flip the image around
		// so it is right side up (from our perspective).
		if from_opengl {
			let mut scanlines: Vec<&[u32]> = Vec::with_capacity(self.height as usize);

			// Push scanline slices in reverse order (which will actually flip them to the right orientation)
			for y in (0..self.height).rev() {
				let src_line_off = (y as u32 * pitch) as usize;
				let src_slice = &slice[src_line_off..src_line_off + self.width as usize];
				scanlines.push(src_slice);
			}

			// Draw them
			for y in 0..self.height {
				let src_line_off = (y as u32 * pitch) as usize;
				let mut dest_line_off = src_line_off;

				// copy only
				if has_disconnected_pitch {
					dest_line_off = (y * self.width) as usize;
				}

				let dest_slice =
					&mut self.framebuffer[dest_line_off..dest_line_off + self.width as usize];

				dest_slice.copy_from_slice(scanlines[y as usize]);

				// swap the scanline pixels to BGRA order to make minifb happy
				// not the fastest code but this should do for an example
				for pix in dest_slice {
					let a = (*pix & 0xff000000) >> 24;
					let b = (*pix & 0x00ff0000) >> 16;
					let g = (*pix & 0x0000ff00) >> 8;
					let r = *pix & 0x000000ff;
					*pix = a << 24 | r << 16 | g << 8 | b;
				}
			}
		} else {
			for y in 0..self.height {
				let src_line_off = (y as u32 * pitch) as usize;
				let mut dest_line_off = src_line_off;

				// copy only
				if has_disconnected_pitch {
					dest_line_off = (y * self.width) as usize;
				}

				// Create slices repressenting each part
				let src_slice = &slice[src_line_off..src_line_off + self.width as usize];
				let dest_slice =
					&mut self.framebuffer[dest_line_off..dest_line_off + self.width as usize];

				dest_slice.copy_from_slice(src_slice);
			}
		}

		let _ = self.window.as_mut().unwrap().update_with_buffer(
			&self.framebuffer,
			self.width as usize,
			self.height as usize,
		);
	}

	pub fn get_keys(&self) -> Option<Vec<Key>> {
		if self.window.is_none() {
			None
		} else {
			let window = self.window.as_ref().unwrap();
			Some(window.get_keys())
		}
	}

	pub fn is_key_down(&self, key: Key) -> bool {
		if self.window.is_none() {
			false
		} else {
			self.window.as_ref().unwrap().is_key_down(key)
		}
	}

	pub fn close(&mut self) {
		self.window = None;
	}

	pub fn is_open(&self) -> bool {
		if self.window.is_none() {
			return false;
		} else {
			self.window.as_ref().unwrap().is_open()
		}
	}
}
