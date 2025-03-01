use std::{
	collections::BTreeMap,
	time::{Duration, Instant},
};

use anyhow::Context;
use client::{ConfigurationState, GraphicsContexts};
use letsplay_core::sleep;
use letsplay_gpu::{self as gpu, GlFramebuffer};
use letsplay_retro_frontend::{
	input_devices::AnyDevice, Frontend, FrontendInterface, HwGlInitData,
};
use letsplay_runner_core::*;

use letsplay_av_ffmpeg::encoder_thread::Control;
use letsplay_av_ffmpeg::encoder_thread::EncoderCommand;

/// Libretro game. very much TODO
pub struct RetroGame {
	graphics_contexts: Option<GraphicsContexts>,
	encoder_control: Option<Control>,

	input_devices: BTreeMap<i32, AnyDevice>,

	frontend: Option<Box<Frontend>>,

	// timing
	frame_duration: Duration,

	// gl styff
	gl_framebuffer: GlFramebuffer,
}

impl RetroGame {
	pub(crate) fn new() -> Box<Self> {
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
	fn game_desc(&self) -> &'static str {
		"Let's Play libretro Runner"
	}

	fn init(&mut self, graphics_contexts: &client::GraphicsContexts, encoder_control: &Control) {
		// HACK: Make the EGL context current when we reach init().
		//
		// This fun little hack is (unsuprisingly, because of libretro) because libretro assumes that
		// a hardware context has been made current when retro_load_game() is called
		// (and the core calls the environment callback to get a hardware context).
		//
		// Once unsuspended (and we're actually context sharing) we will
		// do the right thing, and all will be right with the world. Probably.
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

	fn get_configuration_state(&mut self) -> ConfigurationState {
		if self.get_frontend().game_loaded() {
			return ConfigurationState::ConfigurationComplete;
		}

		ConfigurationState::ConfigurationNeeded
	}

	fn set_property(&mut self, key: &str, value: &str) -> anyhow::Result<()> {
		match key {
			"libretro.core" => {
				self.get_frontend()
					.load_core(value)
					.with_context(|| format!("While trying to load core {}", value))?;

				tracing::info!("Loaded core \"{}\"", value);

				let av_info = self.get_frontend().get_av_info().expect("???");
				self.frame_duration = match av_info.timing.fps {
					// Some cores are stupid and do not provide a proper system_av_info struct
					// (.fps is 0) when initally loaded.
					//
					// Just default to NTSC 59.94hz update rate.
					// FIXME: Implement ENVIRONMENT_SET_SYSTEM_AV_INFO
					0. => Duration::from_secs_f64(1.0 / 59.94),
					other => Duration::from_secs_f64(1.0 / other)
				};

				Ok(())
			}

			"libretro.rom" => {
				self.get_frontend()
					.load_game(value)
					.with_context(|| format!("While trying to load game {}", value))?;

				tracing::info!("Loaded game \"{}\"", value);
				Ok(())
			}

			_ => Err(anyhow::anyhow!(
				"No such key \"{key}\" is known (value \"{value}\""
			)),
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
				resolution: letsplay_core::Size { width, height },
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
