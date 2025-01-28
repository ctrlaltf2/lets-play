use std::{
	collections::BTreeMap,
	sync::{Arc, Mutex},
	time::{Duration, Instant},
};

use letsplay_core::sleep;
use letsplay_gpu::{self as gpu, egl_helpers::DeviceContext};
use letsplay_retro_frontend::{
	frontend::{Frontend, FrontendInterface, HwGlInitData},
	input_devices::AnyDevice,
};
use letsplay_runner_core::*;

/// Libretro game. very much TODO
pub struct RetroGame {
	devices: BTreeMap<i32, AnyDevice>,

	frontend: Option<Box<Frontend>>,

	// timing
	frame_duration: Duration,
}

impl RetroGame {
	fn new() -> Box<Self> {
		let mut s = Box::new(Self {
			devices: BTreeMap::new(),
			frontend: None,
			frame_duration: Duration::new(0, 0),
		});

		// SAFETY: The only way to touch the pointer involves the frontend library calling retro_run,
		// and the core calling one of the given callbacks. Therefore this is gnarly, but "fine",
		// since once the main loop ends, there won't be an opporturnity for said callbacks to be called again.
		//
		// I'm still not really sure how to tell the borrow checker that this is alright,
		// short of Box::leak() (which I don't want to do, since ideally I'd like actual cleanup to occur).
		let obj = &mut *s as *mut dyn FrontendInterface;
		s.frontend = Some(Frontend::new(obj));

		s
	}

	fn get_frontend(&mut self) -> &mut Frontend {
		self.frontend.as_mut().unwrap()
	}
}

impl client::Game for RetroGame {
	fn init(&mut self, graphics_contexts: &client::GraphicsContexts) {
		// HACK: Make the context current when we reach init() because
		// libretro assumes you keep the context current when loading the game
		// Once unsuspended (and we're actually context sharing) we will
		// make current and release properly as we should
		{
			let lk = graphics_contexts.egl_device_context.lock().expect("???");
			lk.make_current();
		}
	}

	fn reset(&mut self) {
		self.get_frontend().reset();
	}

	fn set_property(&mut self, key: &str, value: &str) {
		match key {
			"libretro.core" => {
				tracing::info!("Core is {value}");
				// TODO: Failure should be logged!
				self.get_frontend()
					.load_core(value)
					.expect("Failed to load core");

				let av_info = self.get_frontend().get_av_info().expect("???");
				self.frame_duration = Duration::from_secs_f64(1.0 / av_info.timing.fps);
			}

			"libretro.rom" => {
				self.get_frontend()
					.load_game(value)
					.expect("Failed to load ROM/game");
			}

			_ => {}
		}
	}

	fn run_frame(&mut self) {
		self.get_frontend().run_frame();
	}

	fn wait_for_next_frame(&mut self, start: Instant) {
		let next_frame = start.checked_add(self.frame_duration).unwrap();
		sleep::sleep_until(next_frame);
	}
}

impl FrontendInterface for RetroGame {
	fn video_resize(&mut self, width: u32, height: u32) {
		tracing::info!("Resized to {width}x{height}");
	}

	fn video_update(&mut self, slice: &[u32], pitch: u32) {
		// TODO: Upload software framebuffer to OpenGL texture
	}

	fn video_update_gl(&mut self) {}

	fn audio_sample(&mut self, _slice: &[i16], _size: usize) {}

	fn input_poll(&mut self) {}

	fn hw_gl_init(&mut self) -> Option<HwGlInitData> {
		unsafe {
			// Load OpenGL functions using the EGL loader.
			gl::load_with(|s| {
				let str = std::ffi::CString::new(s).expect("gl::load_with fail");
				std::mem::transmute(gpu::egl::GetProcAddress(str.as_ptr()))
			});

			// set OpenGL debug message callback
			//gl::Enable(gl::DEBUG_OUTPUT);
			//gl::DebugMessageCallback(Some(opengl_message_callback), std::ptr::null());
		}

		return Some(HwGlInitData {
			get_proc_address: gpu::egl::GetProcAddress as *mut std::ffi::c_void,
		});
	}
}

client_main!(RetroGame);
