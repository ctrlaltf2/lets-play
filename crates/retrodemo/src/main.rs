use std::{cell::RefCell, rc::Rc};

use retro_frontend::{core::Core, frontend, joypad::{Joypad, RetroPad}, libretro_sys_new};
use tracing::Level;
use tracing_subscriber::FmtSubscriber;

use minifb::{Window, Key, WindowOptions};

use clap::{arg, command};

struct App {
	window: Option<Window>,
	pad: Rc<RefCell<RetroPad>>,
}

impl App {
	fn new() -> Self {
		Self {
			window: None,
			// nasty, but idk a better way
			pad: Rc::new(RefCell::new(RetroPad::new()))
		}
	}

	fn new_and_init() -> Rc<RefCell<App>> {
		let mut app = App::new();
		app.init();

		Rc::new(RefCell::new(app))
	}

	fn init(&mut self) {
		let av_info = frontend::get_av_info().expect("No AV info");

		frontend::set_input_port_device(0, self.pad.clone());

		self.resize(av_info.geometry.base_width, av_info.geometry.base_height);
	}

	fn resize(&mut self, width: u32, height: u32) {
		let av_info = frontend::get_av_info().expect("No AV info");

		println!("Resized to {width}x{height}");

		let mut window = Window::new(
			"RetroDemo - retro_frontend demo",
			width as usize,
			height as usize,
			WindowOptions {
				scale: minifb::Scale::X2,
				..Default::default()
			},
		)
		.expect("Could not open window");

		window.set_target_fps((av_info.timing.fps) as usize);
		self.window = Some(window);
	}

	fn frame_update(&mut self, slice: &[u32]) {
		let framebuffer_size = frontend::get_size();
		let _ = self.window.as_mut().unwrap().update_with_buffer(
			&slice,
			//framebuffer_size.0 as usize,
			framebuffer_size.0 as usize,
			framebuffer_size.1 as usize,
		);
	}

	fn input_poll(&mut self) {
		let window = self.window.as_ref().unwrap();

		// reset the pad state
		self.pad.borrow_mut().reset();

		for key in &window.get_keys() {
			match key {
				// hardcoded specifcially for mesen, but it proves that it works!
				Key::Backslash => {
					self.pad.borrow_mut().press_button(libretro_sys_new::DEVICE_ID_JOYPAD_SELECT, None);
				}
				Key::Enter => {
					self.pad.borrow_mut().press_button(libretro_sys_new::DEVICE_ID_JOYPAD_START, None);
				},
				Key::Up => {
					self.pad.borrow_mut().press_button(libretro_sys_new::DEVICE_ID_JOYPAD_UP, None);
				},
				Key::Down => {
					self.pad.borrow_mut().press_button(libretro_sys_new::DEVICE_ID_JOYPAD_DOWN, None);
				},
				Key::Left => {
					self.pad.borrow_mut().press_button(libretro_sys_new::DEVICE_ID_JOYPAD_LEFT, None);
				},
				Key::Right => {
					self.pad.borrow_mut().press_button(libretro_sys_new::DEVICE_ID_JOYPAD_RIGHT, None);
				},
				Key::A => {
					self.pad.borrow_mut().press_button(libretro_sys_new::DEVICE_ID_JOYPAD_A, None);
				}
				Key::Z => {
					self.pad.borrow_mut().press_button(libretro_sys_new::DEVICE_ID_JOYPAD_B, None);
				}
				_ => {}
			}
		}
	}

	fn should_run(&self) -> bool {
		self.window.as_ref().unwrap().is_open()
	}
}

fn main() {
	// Setup a tracing subscriber
	let subscriber = FmtSubscriber::builder()
		.with_max_level(Level::INFO)
		.finish();

	tracing::subscriber::set_global_default(subscriber).unwrap();

	let matches = command!()
		.arg(arg!(--core <VALUE>).required(true))
		.arg(arg!(--rom <VALUE>).required(true))
		.get_matches();

	let core_path: &String = matches.get_one("core").unwrap();
	let rom_path: &String = matches.get_one("rom").unwrap();

	// Load a core
	let mut core = Core::load(core_path).expect("Core failed to load");

	let app = App::new_and_init();

	let app_clone = app.clone();
	frontend::set_video_update_callback(move |slice| {
		app_clone.borrow_mut().frame_update(slice);
	});

	let app_resize_clone = app.clone();
	frontend::set_video_resize_callback(move |width, height| {
		app_resize_clone.borrow_mut().resize(width, height);
	});

	frontend::set_audio_sample_callback(|_slice, _frames| {
		//println!("Got audio sample batch with {_frames} frames");
	});

	let app_input_poll_clone = app.clone();
	frontend::set_input_poll_callback(move || {
		app_input_poll_clone.borrow_mut().input_poll();
	});


	core.load_game(rom_path).expect("ROM failed to load");

	loop {
		{
			let borrowed_app = app.borrow();
			if !borrowed_app.should_run() {
				break;
			}
		}

		frontend::run_frame();
	}
}
