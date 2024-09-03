use super::window::*;

use std::{path::Path, time::Duration};

use anyhow::Result;

use retro_frontend::{
	frontend::{Frontend, FrontendInterface, HwGlInitData},
	input_devices::{InputDevice, RetroPad},
	libretro_sys_new,
};

use minifb::Key;

use letsplay_gpu as gpu;
use gpu::egl_helpers::DeviceContext;

/// Called by OpenGL. We use this to dump errors.
extern "system" fn opengl_message_callback(
	source: gl::types::GLenum,
	_type: gl::types::GLenum,
	id: gl::types::GLuint,
	_severity: gl::types::GLenum,
	_length: gl::types::GLsizei,
	message: *const gl::types::GLchar,
	_user: *mut std::ffi::c_void,
) {
	unsafe {
		let message = std::ffi::CStr::from_ptr(message);
		if _type == gl::DEBUG_TYPE_ERROR {
			tracing::error!(
				"OpenGL error: {:?} (res {:08x}, id = {:08x}, source = {:08x})",
				message,
				_type,
				id,
				source
			);
		}
	}
}

pub struct App {
	window: AppWindow,

	frontend: Option<Box<Frontend>>,

	pad: RetroPad,

	// EGL state
	egl_context: Option<DeviceContext>,

	// OpenGL object IDs
	framebuffer: gpu::GlFramebuffer,

	/// Cached readback buffer.
	readback_buffer: Vec<u32>,
}

impl App {
	pub fn new() -> Box<Self> {
		let mut boxed = Box::new(Self {
			window: AppWindow::new(),
			frontend: None,
			pad: RetroPad::new(),

			egl_context: None,
			framebuffer: gpu::GlFramebuffer::new(),
			readback_buffer: Vec::new(),
		});

		// SAFETY: The only way to touch the pointer involves the frontend library calling retro_run,
		// and the core calling one of the given callbacks. Therefore this is gnarly, but "fine",
		// since once the main loop ends, there won't be an opporturnity for said callbacks to be called again.
		//
		// I'm still not really sure how to tell the borrow checker that this is alright,
		// short of Box::leak() (which I don't want to do, since ideally I'd like actual cleanup to occur).
		let obj = &mut *boxed as *mut dyn FrontendInterface;
		boxed.frontend = Some(Frontend::new(obj));

		boxed
	}

	fn get_frontend(&mut self) -> &mut Frontend {
		self.frontend.as_mut().unwrap()
	}

	/// Inserts our RetroPad and initalizes the display.
	pub fn init(&mut self) {
		// SAFETY: This too won't ever be Use-After-Free'd because the only chance to
		// goes away on drop as well. That's a bit flaky reasoning wise, but is true.
		//
		// In all honesty, I'm not sure this even needs to be a *mut so I could see if
		// making it a immutable reference works.
		let pad = &mut self.pad as *mut dyn InputDevice;
		self.get_frontend().plug_input_device(0, pad);

		self.init_display();
	}

	fn init_display(&mut self) {
		let av_info = self.get_frontend().get_av_info().expect("No AV info");

		self.window.resize(
			av_info.geometry.base_width as u16,
			av_info.geometry.base_height as u16,
		);
	}

	pub fn load_core<P: AsRef<Path>>(&mut self, path: P) -> Result<()> {
		// Unload an existing core.
		if self.get_frontend().core_loaded() {
			let _ = self.get_frontend().unload_core();
		}

		self.get_frontend().load_core(path)?;
		Ok(())
	}

	pub fn load_game<P: AsRef<Path>>(&mut self, path: P) -> Result<()> {
		self.get_frontend().load_game(path)?;
		Ok(())
	}

	/// Initalizes the headless EGL context used for OpenGL rendering.
	fn hw_gl_egl_init(&mut self) {
		self.egl_context = Some(DeviceContext::new(0));
	}

	/// Destroys OpenGL resources and the EGL context.
	fn hw_gl_destroy(&mut self) {
		if self.egl_context.is_some() {
			self.framebuffer.destroy();
			self.egl_context.take().unwrap().destroy()
		}
	}

	/// The main loop. Should probably be abstracted a bit better.
	pub fn main_loop(&mut self) {
		while self.window.is_open() && !self.window.is_key_down(Key::Escape) {
			let av_info = self.get_frontend().get_av_info().expect("???");
			let step_ms = (1.0 / av_info.timing.fps) * 1000.;
			let step_duration = Duration::from_millis(step_ms as u64);

			self.get_frontend().run_frame();
			std::thread::sleep(step_duration);
		}

		self.window.close();
	}
}

impl FrontendInterface for App {
	fn video_resize(&mut self, width: u32, height: u32) {
		tracing::info!("Resized to {width}x{height}");

		if self.egl_context.is_some() {
			self.framebuffer.resize(width, height);
			let raw = self.framebuffer.as_raw();

			// Notify the frontend layer about the new FBO ID
			self.get_frontend().set_gl_fbo(raw);

			// Resize the readback buffer
			self.readback_buffer.resize((width * height) as usize, 0);
		}

		self.window.resize(width as u16, height as u16);
	}

	fn video_update(&mut self, slice: &[u32], pitch: u32) {
		self.window.update_buffer(&slice, pitch, false);
	}

	fn video_update_gl(&mut self) {
		let dimensions = self.get_frontend().get_size();

		// Read back the framebuffer
		{
			self.framebuffer
				.read_pixels(&mut self.readback_buffer[..], dimensions.0, dimensions.1)
		}

		let slice = self.readback_buffer.as_slice();
		self.window.update_buffer(slice, dimensions.0, true);
	}

	fn audio_sample(&mut self, _slice: &[i16], _size: usize) {}

	fn input_poll(&mut self) {
		self.pad.reset();

		if let Some(keys) = self.window.get_keys() {
			for key in &keys {
				match key {
					Key::Backslash => {
						self.pad
							.press_button(libretro_sys_new::DEVICE_ID_JOYPAD_SELECT, None);
					}
					Key::Enter => {
						self.pad
							.press_button(libretro_sys_new::DEVICE_ID_JOYPAD_START, None);
					}
					Key::Up => {
						self.pad
							.press_button(libretro_sys_new::DEVICE_ID_JOYPAD_UP, None);
					}
					Key::Down => {
						self.pad
							.press_button(libretro_sys_new::DEVICE_ID_JOYPAD_DOWN, None);
					}
					Key::Left => {
						self.pad
							.press_button(libretro_sys_new::DEVICE_ID_JOYPAD_LEFT, None);
					}
					Key::Right => {
						self.pad
							.press_button(libretro_sys_new::DEVICE_ID_JOYPAD_RIGHT, None);
					}

					Key::S => {
						self.pad
							.press_button(libretro_sys_new::DEVICE_ID_JOYPAD_B, None);
					}

					Key::A => {
						self.pad
							.press_button(libretro_sys_new::DEVICE_ID_JOYPAD_A, None);
					}

					Key::Q => {
						self.pad
							.press_button(libretro_sys_new::DEVICE_ID_JOYPAD_X, None);
					}

					Key::W => {
						self.pad
							.press_button(libretro_sys_new::DEVICE_ID_JOYPAD_Y, None);
					}

					Key::LeftCtrl => {
						self.pad
							.press_button(libretro_sys_new::DEVICE_ID_JOYPAD_L, None);
					}

					Key::LeftShift => {
						self.pad
							.press_button(libretro_sys_new::DEVICE_ID_JOYPAD_L2, None);
					}

					Key::LeftAlt => {
						self.pad
							.press_button(libretro_sys_new::DEVICE_ID_JOYPAD_R, None);
					}

					Key::Z => {
						self.pad
							.press_button(libretro_sys_new::DEVICE_ID_JOYPAD_R2, None);
					}

					_ => {}
				}
			}
		}
	}

	fn hw_gl_init(&mut self) -> Option<HwGlInitData> {
		// Only create a new EGL/OpenGL context if we have to.
		if self.egl_context.is_none() {
			// Initalize EGL
			self.hw_gl_egl_init();

			let context = self.egl_context.as_ref().unwrap();
			let extensions = gpu::egl_helpers::get_extensions(context.get_display());

			tracing::debug!("Supported EGL extensions: {:?}", extensions);

			// Check for EGL_KHR_get_all_proc_addresses, so we can use eglGetProcAddress() to load OpenGL functions
			if !extensions.contains(&"EGL_KHR_get_all_proc_addresses".into()) {
				tracing::error!("Your graphics driver doesn't support the EGL_KHR_get_all_proc_addresses extension.");
				tracing::error!("Retrodemo currently needs this to load OpenGL functions. HW rendering will be disabled.");
				return None;
			}

			unsafe {
				// Load OpenGL functions using the EGL loader.
				gl::load_with(|s| {
					let str = std::ffi::CString::new(s).expect("gl::load_with fail");
					std::mem::transmute(gpu::egl::GetProcAddress(str.as_ptr()))
				});

				// set OpenGL debug message callback
				gl::Enable(gl::DEBUG_OUTPUT);
				gl::DebugMessageCallback(Some(opengl_message_callback), std::ptr::null());
			}
		}

		// Create the initial FBO for the core to render to
		let dimensions = self.get_frontend().get_size();
		self.framebuffer.resize(dimensions.0, dimensions.1);

		return Some(HwGlInitData {
			get_proc_address: gpu::egl::GetProcAddress as *mut std::ffi::c_void,
		});
	}
}

impl Drop for App {
	fn drop(&mut self) {
		// Terminate EGL and GL resources if need be
		self.hw_gl_destroy();
	}
}
