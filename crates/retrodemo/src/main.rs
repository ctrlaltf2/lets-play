use std::{path::Path, time::Duration};

use std::ptr::{addr_of_mut, null};

use anyhow::Result;

use retro_frontend::{
	frontend::{Frontend, FrontendInterface, HwGlInitData},
	input_devices::{InputDevice, RetroPad},
	libretro_sys_new,
};

use tracing::Level;
use tracing_subscriber::FmtSubscriber;

use minifb::{Key, Window, WindowOptions};

use clap::{arg, command};

// Mostly for code portability, but also
// because I can't be bothered to type the larger name
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

/// A wrapper over minifb
struct AppWindow {
	window: Option<Window>,

	// sw framebuffer
	width: u16,
	height: u16,
	framebuffer: Vec<u32>,
}

impl AppWindow {
	fn new() -> Self {
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

struct App {
	window: AppWindow,

	frontend: Option<Box<Frontend>>,

	pad: RetroPad,

	/// True if HW rendering is active.
	using_hardware_rendering: bool,

	// EGL state
	egl_display: egl::types::EGLDisplay,
	egl_context: egl::types::EGLContext,

	// OpenGL object IDs
	texture_id: gl::types::GLuint,
	renderbuffer_id: gl::types::GLuint,
	fbo_id: gl::types::GLuint,

	/// Cached readback buffer.
	readback_buffer: Vec<u32>,
}

impl App {
	fn new() -> Box<Self> {
		let mut boxed = Box::new(Self {
			window: AppWindow::new(),
			frontend: None,
			pad: RetroPad::new(),

			using_hardware_rendering: false,
			egl_display: null(),
			egl_context: null(),
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

	/// Initalizes the frontend library with callbacks back to us,
	/// and performs an initial window resize.
	fn init(&mut self) {
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

	/// Initalizes a headless EGL context for OpenGL rendering.
	fn hw_gl_egl_init(&mut self) {
		// Currently we assume the first device on the Device platform.
		// In most cases (at least on NVIDIA), this is usually a real GPU.
		self.egl_display = egl::get_device_platform_display(0);

		self.egl_context = unsafe {
			const EGL_CONFIG_ATTRIBUTES: [egl::types::EGLenum; 13] = [
				egl::SURFACE_TYPE,
				egl::PBUFFER_BIT,
				egl::BLUE_SIZE,
				8,
				egl::RED_SIZE,
				8,
				egl::BLUE_SIZE,
				8,
				egl::DEPTH_SIZE,
				8,
				egl::RENDERABLE_TYPE,
				egl::OPENGL_BIT,
				egl::NONE,
			];
			let mut egl_major: egl::EGLint = 0;
			let mut egl_minor: egl::EGLint = 0;

			let mut egl_config_count: egl::EGLint = 0;

			let mut config: egl::types::EGLConfig = null();

			egl::Initialize(
				self.egl_display,
				addr_of_mut!(egl_major),
				addr_of_mut!(egl_minor),
			);

			egl::ChooseConfig(
				self.egl_display,
				EGL_CONFIG_ATTRIBUTES.as_ptr() as *const egl::EGLint,
				addr_of_mut!(config),
				1,
				addr_of_mut!(egl_config_count),
			);

			egl::BindAPI(egl::OPENGL_API);

			let context = egl::CreateContext(self.egl_display, config, egl::NO_CONTEXT, null());

			// Make the context current on the display so OpenGL routines "just work"
			egl::MakeCurrent(self.egl_display, egl::NO_SURFACE, egl::NO_SURFACE, context);

			context
		};
	}

	/// Destroys EGL resources.
	fn hw_gl_egl_exit(&mut self) {
		if self.using_hardware_rendering {
			// Delete FBO
			self.hw_gl_delete_fbo();

			// Release the EGL context we created before destroying it
			unsafe {
				egl::MakeCurrent(
					self.egl_display,
					egl::NO_SURFACE,
					egl::NO_SURFACE,
					egl::NO_CONTEXT,
				);
				egl::DestroyContext(self.egl_display, self.egl_context);
				egl::Terminate(self.egl_display);
			}
			self.egl_display = null();
			self.egl_context = null();
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

			gl::GenFramebuffers(1, addr_of_mut!(self.fbo_id));
			gl::BindFramebuffer(gl::FRAMEBUFFER, self.fbo_id);

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

			gl::GenRenderbuffers(1, addr_of_mut!(self.renderbuffer_id));
			gl::BindRenderbuffer(gl::RENDERBUFFER, self.renderbuffer_id);

			gl::RenderbufferStorage(
				gl::RENDERBUFFER,
				gl::DEPTH_COMPONENT,
				width as i32,
				height as i32,
			);

			gl::FramebufferTexture2D(
				gl::FRAMEBUFFER,
				gl::COLOR_ATTACHMENT0,
				gl::TEXTURE_2D,
				self.texture_id,
				0,
			);

			gl::BindTexture(gl::TEXTURE_2D, 0);

			gl::FramebufferRenderbuffer(
				gl::FRAMEBUFFER,
				gl::DEPTH_ATTACHMENT,
				gl::RENDERBUFFER,
				self.renderbuffer_id,
			);

			gl::BindRenderbuffer(gl::RENDERBUFFER, 0);

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
		// TODO: This can change, so it should probably be put in the loop.
		let av_info = self.get_frontend().get_av_info().expect("???");
		let step_ms = (1.0 / av_info.timing.fps) * 1000.;
		let step_duration = Duration::from_micros((step_ms * 1000.) as u64);

		// Do the main loop

		while self.window.is_open() && !self.window.is_key_down(Key::Escape) {
			self.get_frontend().run_frame();
			std::thread::sleep(step_duration);
		}

		tracing::info!("done with main loop");
		self.window.close();
	}
}

impl FrontendInterface for App {
	fn video_resize(&mut self, width: u32, height: u32) {
		tracing::info!("Resized to {width}x{height}");

		// Recreate the OpenGL FBO on resize.
		if self.using_hardware_rendering {
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

	fn hw_gl_init(&mut self) -> HwGlInitData {
		if self.using_hardware_rendering {
			panic!("Cannot initalize HW rendering while already initalized");
		}

		// Initalize EGL
		self.hw_gl_egl_init();

		let extensions = egl::get_extensions(self.egl_display);

		tracing::debug!("Supported EGL extensions: {:?}", extensions);

		// Check for EGL_KHR_get_all_proc_addresses, so we can use eglGetProcAddress() to load OpenGL functions
		// TODO: instead of panicing, we should probably make this return a Option<_>, and treat None on the frontend side
		// as a failure.
		if !extensions.contains(&"EGL_KHR_get_all_proc_addresses".into()) {
			tracing::error!("Your graphics driver doesn't support the EGL_KHR_get_all_proc_addresses extension. Failing");
			panic!("Cannot initalize OpenGL rendering");
		}

		unsafe {
			// Load OpenGL functions using the EGL loader.
			gl::load_with(|s| {
				let str = std::ffi::CString::new(s).expect("Uhh huh.");
				std::mem::transmute(egl::GetProcAddress(str.as_ptr()))
			});

			// set OpenGL debug message callback
			gl::Enable(gl::DEBUG_OUTPUT);
			gl::DebugMessageCallback(Some(opengl_message_callback), null());
		}

		// Create the initial FBO for the core to render to
		let dimensions = self.get_frontend().get_size();
		self.hw_gl_create_fbo(dimensions.0, dimensions.1);

		self.using_hardware_rendering = true;

		return unsafe {
			HwGlInitData {
				get_proc_address: std::mem::transmute(egl::GetProcAddress as *mut std::ffi::c_void),
			}
		};
	}
}

impl Drop for App {
	fn drop(&mut self) {
		// Terminate EGL and GL resources
		if self.using_hardware_rendering {
			tracing::info!("Releasing OpenGL resources");
			self.hw_gl_egl_exit();
		}
	}
}

fn main() -> Result<()> {
	// Setup a tracing subscriber
	let subscriber = FmtSubscriber::builder()
		.with_max_level(Level::INFO)
		.finish();

	tracing::subscriber::set_global_default(subscriber).unwrap();

	let matches = command!()
		.arg(arg!(--core <VALUE>).required(true))
		// Not that it matters, but this is only really required for cores that require
		// content to be loaded; that's most cores, but libretro does support the difference.
		.arg(arg!(--rom <VALUE>).required(false))
		.get_matches();

	let core_path: &String = matches.get_one("core").unwrap();

	let mut app = App::new();

	app.load_core(core_path)?;

	if let Some(rom_path) = matches.get_one::<String>("rom") {
		app.load_game(rom_path)?;
	}

	app.init();

	app.main_loop();

	Ok(())
}
