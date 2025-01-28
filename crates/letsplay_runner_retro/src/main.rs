use std::{
	collections::BTreeMap,
	sync::{Arc, Mutex},
	time::{Duration, Instant},
};

use client::GraphicsContexts;
use letsplay_core::sleep;
use letsplay_gpu::{self as gpu, egl_helpers::DeviceContext, GlFramebuffer};
use letsplay_retro_frontend::{
	frontend::{Frontend, FrontendInterface, HwGlInitData},
	input_devices::AnyDevice,
};
use letsplay_runner_core::*;

use letsplay_av_ffmpeg::encoder_thread::EncoderCommand;
use letsplay_av_ffmpeg::encoder_thread::EncoderThreadControl;

/// Libretro game. very much TODO
pub struct RetroGame {
	graphics_contexts: Option<GraphicsContexts>,
	encoder_control: Option<EncoderThreadControl>,

	input_devices: BTreeMap<i32, AnyDevice>,

	frontend: Option<Box<Frontend>>,

	// timing
	frame_duration: Duration,

	// gl styff
	gl_framebuffer: GlFramebuffer,
}

impl RetroGame {
	fn new() -> Box<Self> {
		let mut s = Box::new(Self {
			graphics_contexts: None,
			encoder_control: None,
			input_devices: BTreeMap::new(),
			frontend: None,
			frame_duration: Duration::new(0, 0),

			gl_framebuffer: GlFramebuffer::new(),
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
	fn init(
		&mut self,
		graphics_contexts: &client::GraphicsContexts,
		encoder_control: &EncoderThreadControl,
	) {
		// HACK: Make the context current when we reach init() because
		// libretro assumes you keep the context current when loading the game
		// Once unsuspended (and we're actually context sharing) we will
		// make current and release properly as we should
		{
			let lk = graphics_contexts.egl_device_context.lock().expect("???");
			lk.make_current();
		}

		// Scary but these are all Arc<> pointers anyways so its not a big deal
		self.graphics_contexts = Some(graphics_contexts.clone());
		self.encoder_control = Some(encoder_control.clone());
	}

	fn reset(&mut self) {
		self.get_frontend().reset();
	}

	fn set_property(&mut self, key: &str, value: &str) {
		match key {
			"libretro.core" => {
				tracing::info!("Core is {value}");
				// TODO: Failure should be logged, not panic worthy
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
		self.gl_framebuffer.resize(width, height);
		let raw = self.gl_framebuffer.as_raw();

		// Notify the frontend layer about the new FBO ID
		self.get_frontend().set_gl_fbo(raw);

		// register the FBO's texture to our cuda interop resource
		#[cfg(feature = "av-nvidia")]
		{
			let mut locked = self
				.graphics_contexts
				.as_ref()
				.unwrap()
				.cuda_interop_context
				.lock()
				.expect("Failed to lock CUDA resource");

			locked
				.device()
				.bind_to_thread()
				.expect("Failed to bind CUDA device to thread");

			locked
				.register(self.gl_framebuffer.texture_id(), gl::TEXTURE_2D)
				.expect("Failed to register OpenGL texture with CUDA Graphics resource");
		}

		// FIXME: Not this
		self.encoder_control
			.as_ref()
			.unwrap()
			.send_command(EncoderCommand::Init {
				size: letsplay_core::Size { width, height },
			});
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
		}

		return Some(HwGlInitData {
			get_proc_address: gpu::egl::GetProcAddress as *mut std::ffi::c_void,
		});
	}
}

client_main!(RetroGame);
