use super::window::*;

use std::{path::Path, time::Duration};

use std::ptr::{addr_of_mut, null};

use anyhow::Result;

use retro_frontend::{
	frontend::{Frontend, FrontendInterface, HwGlInitData},
	input_devices::{InputDevice, RetroPad},
	libretro_sys_new,
};

use minifb::Key;

// Mostly for code portability, but also
// because I can't be bothered to type the larger name
use egl::helpers::DeviceContext;
use letsplay_egl as egl;

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
	texture_id: gl::types::GLuint,
	renderbuffer_id: gl::types::GLuint,
	fbo_id: gl::types::GLuint,

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
			texture_id: 0,
			renderbuffer_id: 0,
			fbo_id: 0,
			readback_buffer: Vec::new(),
		});

		// SAFETY: The only way to touch the pointer involves the frontend library calling retro_run,
		// and the core calling one of the given callbacks. Therefore this is gnarly, but "fine",
		// since once the main loop ends, there never will be an opporturnity for said callbacks to be called again.
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
	fn hw_gl_exit(&mut self) {
		if self.egl_context.is_some() {
			// Delete FBO
			self.hw_gl_delete_fbo();
			self.egl_context.take().unwrap().destroy()
		}
	}

	/// Deletes all OpenGL FBO resources (the FBO itself, the render texture, and the renderbuffer used for depth)
	fn hw_gl_delete_fbo(&mut self) {
		unsafe {
			gl::DeleteFramebuffers(1, addr_of_mut!(self.fbo_id));
			self.fbo_id = 0;

			gl::DeleteTextures(1, addr_of_mut!(self.texture_id));
			self.texture_id = 0;

			gl::DeleteRenderbuffers(1, addr_of_mut!(self.renderbuffer_id));
			self.renderbuffer_id = 0;
		}
	}

	/// Creates the OpenGL FBO that the core renders to.
	fn hw_gl_create_fbo(&mut self, width: u32, height: u32) {
		unsafe {
			if self.fbo_id != 0 {
				self.hw_gl_delete_fbo();
			}

			gl::GenTextures(1, addr_of_mut!(self.texture_id));
			gl::BindTexture(gl::TEXTURE_2D, self.texture_id);

			gl::TexImage2D(
				gl::TEXTURE_2D,
				0,
				gl::RGBA8 as i32,
				width as i32,
				height as i32,
				0,
				gl::RGBA,
				gl::UNSIGNED_BYTE,
				null(),
			);

			gl::BindTexture(gl::TEXTURE_2D, 0);

			gl::GenRenderbuffers(1, addr_of_mut!(self.renderbuffer_id));
			gl::BindRenderbuffer(gl::RENDERBUFFER, self.renderbuffer_id);

			gl::RenderbufferStorage(
				gl::RENDERBUFFER,
				gl::DEPTH_COMPONENT,
				width as i32,
				height as i32,
			);

			gl::BindRenderbuffer(gl::RENDERBUFFER, 0);

			gl::GenFramebuffers(1, addr_of_mut!(self.fbo_id));
			gl::BindFramebuffer(gl::FRAMEBUFFER, self.fbo_id);

			gl::FramebufferTexture2D(
				gl::FRAMEBUFFER,
				gl::COLOR_ATTACHMENT0,
				gl::TEXTURE_2D,
				self.texture_id,
				0,
			);

			gl::FramebufferRenderbuffer(
				gl::FRAMEBUFFER,
				gl::DEPTH_ATTACHMENT,
				gl::RENDERBUFFER,
				self.renderbuffer_id,
			);

			gl::Viewport(0, 0, width as i32, height as i32);

			gl::BindFramebuffer(gl::FRAMEBUFFER, 0);

			// Notify the frontend layer about the new FBO
			let id = self.fbo_id;
			self.get_frontend().set_gl_fbo(id);

			// Resize the readback buffer
			self.readback_buffer.resize((width * height) as usize, 0);
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

		// Recreate the OpenGL FBO on resize.
		if self.egl_context.is_some() {
			self.hw_gl_create_fbo(width, height);
		}

		self.window.resize(width as u16, height as u16);
	}

	fn video_update(&mut self, slice: &[u32], pitch: u32) {
		self.window.update_buffer(&slice, pitch, false);
	}

	fn video_update_gl(&mut self) {
		let dimensions = self.get_frontend().get_size();

		// Read back the framebuffer with glReadPixels()
		// I know it sucks but it works for this case.
		// SAFETY: self.readback_buffer will always be allocated to the proper size before reaching here
		unsafe {
			gl::BindFramebuffer(gl::FRAMEBUFFER, self.fbo_id);

			gl::ReadPixels(
				0,
				0,
				dimensions.0 as i32,
				dimensions.1 as i32,
				gl::RGBA,
				gl::UNSIGNED_BYTE,
				self.readback_buffer.as_mut_ptr() as *mut std::ffi::c_void,
			);

			gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
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
			let extensions = egl::helpers::get_extensions(context.get_display());

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
					std::mem::transmute(egl::GetProcAddress(str.as_ptr()))
				});

				// set OpenGL debug message callback
				gl::Enable(gl::DEBUG_OUTPUT);
				gl::DebugMessageCallback(Some(opengl_message_callback), null());
			}
		}

		// Create the initial FBO for the core to render to
		let dimensions = self.get_frontend().get_size();
		self.hw_gl_create_fbo(dimensions.0, dimensions.1);

		return Some(HwGlInitData {
			get_proc_address: egl::GetProcAddress as *mut std::ffi::c_void,
		});
	}
}

impl Drop for App {
	fn drop(&mut self) {
		// Terminate EGL and GL resources if need be
		self.hw_gl_exit();
	}
}
