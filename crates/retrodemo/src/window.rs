use minifb::{Key, Window, WindowOptions};

use letsplay_core::{Size, Surface};

/// A wrapper over minifb
pub struct AppWindow {
	window: Option<Window>,

	// sw framebuffer
	framebuffer: Surface,
}

impl AppWindow {
	pub fn new() -> Self {
		Self {
			window: None,
			framebuffer: Surface::new(),
		}
	}

	pub fn resize(&mut self, width: u16, height: u16) {
		self.framebuffer.resize(Size {
			width: width as u32,
			height: height as u32,
		});

		let window = Window::new(
			"RetroDemo - letsplay_retro_frontend demo (Press Esc to exit)",
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

		let size = self.framebuffer.size.clone();

		let has_disconnected_pitch = pitch != size.width as u32;

		// If this frame came from OpenGL we need to flip the image around
		// so it is right side up (from our perspective).
		if from_opengl {
			// Draw them
			for source_y in 0..size.height {
				let flipped_y = (size.height - 1) - source_y;

				let src_line_off = (source_y as u32 * pitch) as usize;
				let mut dest_line_off = (flipped_y as u32 * pitch) as usize;

				// copy only
				if has_disconnected_pitch {
					dest_line_off = (flipped_y * size.width) as usize;
				}

				let src_slice = &slice[src_line_off..src_line_off + size.width as usize];

				let dest_slice = &mut self.framebuffer.get_buffer()
					[dest_line_off..dest_line_off + size.width as usize];

				dest_slice.copy_from_slice(src_slice);

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
			for y in 0..size.height {
				let src_line_off = (y as u32 * pitch) as usize;
				let mut dest_line_off = src_line_off;

				// copy only
				if has_disconnected_pitch {
					dest_line_off = (y * size.width) as usize;
				}

				// Create slices repressenting each part
				let src_slice = &slice[src_line_off..src_line_off + size.width as usize];
				let dest_slice = &mut self.framebuffer.get_buffer()
					[dest_line_off..dest_line_off + size.width as usize];

				dest_slice.copy_from_slice(src_slice);
			}
		}

		let _ = self.window.as_mut().unwrap().update_with_buffer(
			&self.framebuffer.get_buffer(),
			size.width as usize,
			size.height as usize,
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
