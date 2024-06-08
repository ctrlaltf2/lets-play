use std::{cell::RefCell, rc::Rc};

use retro_frontend::{
	core::Core,
	frontend,
	joypad::{Joypad, RetroPad},
	libretro_sys_new,
};
use tracing::Level;
use tracing_subscriber::FmtSubscriber;

use minifb::{Key, Window, WindowOptions};

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
			pad: Rc::new(RefCell::new(RetroPad::new())),
		}
	}

	fn new_and_init() -> Rc<RefCell<App>> {
		let app = App::new();
		let rc = Rc::new(RefCell::new(app));

		// Initalize all the frontend callbacks and stuff.
		App::init(&rc);

		rc
	}

	/// Initalizes the frontend library with callbacks back to us,
	/// and performs an initial window resize.
	fn init(rc: &Rc<RefCell<Self>>) {
		let app_clone = rc.clone();
		frontend::set_video_update_callback(move |slice| {
			app_clone.borrow_mut().frame_update(slice);
		});

		let app_resize_clone = rc.clone();
		frontend::set_video_resize_callback(move |width, height| {
			app_resize_clone.borrow_mut().resize(width, height);
		});

		frontend::set_audio_sample_callback(|_slice, _frames| {
			//println!("Got audio sample batch with {_frames} frames");
		});

		let app_input_poll_clone = rc.clone();
		frontend::set_input_poll_callback(move || {
			app_input_poll_clone.borrow_mut().input_poll();
		});

		// Currently retrodemo just hardcodes the assumption of a single RetroPad.
		frontend::set_input_port_device(0, rc.borrow().pad.clone());

		let av_info = frontend::get_av_info().expect("No AV info");
		rc.borrow_mut()
			.resize(av_info.geometry.base_width, av_info.geometry.base_height);
	}

	/// Called by the frontend library when a video resize needs to occur.
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

	/// Called by the frontend library on video frame updates
	/// The framebuffer is *always* a RGBX8888 slice regardless of whatever video mode
	/// the core has setup internally; this is by design to make code less annoying
	fn frame_update(&mut self, slice: &[u32]) {
		let framebuffer_size = frontend::get_size();
		let _ = self.window.as_mut().unwrap().update_with_buffer(
			&slice,
			framebuffer_size.0 as usize,
			framebuffer_size.1 as usize,
		);
	}

	/// Called by the frontend library during retro_run() to poll input
	fn input_poll(&mut self) {
		let window = self.window.as_ref().unwrap();
		let mut pad = self.pad.borrow_mut();

		pad.reset();

		for key in &window.get_keys() {
			match key {
				// hardcoded specifcially for mesen, but it proves that it works!
				Key::Backslash => {
					pad.press_button(libretro_sys_new::DEVICE_ID_JOYPAD_SELECT, None);
				}
				Key::Enter => {
					pad.press_button(libretro_sys_new::DEVICE_ID_JOYPAD_START, None);
				}
				Key::Up => {
					pad.press_button(libretro_sys_new::DEVICE_ID_JOYPAD_UP, None);
				}
				Key::Down => {
					pad.press_button(libretro_sys_new::DEVICE_ID_JOYPAD_DOWN, None);
				}
				Key::Left => {
					pad.press_button(libretro_sys_new::DEVICE_ID_JOYPAD_LEFT, None);
				}
				Key::Right => {
					pad.press_button(libretro_sys_new::DEVICE_ID_JOYPAD_RIGHT, None);
				}
				Key::A => {
					pad.press_button(libretro_sys_new::DEVICE_ID_JOYPAD_A, None);
				}
				Key::Z => {
					pad.press_button(libretro_sys_new::DEVICE_ID_JOYPAD_B, None);
				}
				_ => {}
			}
		}
	}

	/// Returns if the main loop should continue running.
	/// Currently we only check if the window has been closed;
	/// we could put a check for Esc in [Self::input_poll]
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
		// Not that it matters, but this is only really required for cores that require
		// content to be loaded; that's most cores, but libretro does support the difference.
		.arg(arg!(--rom <VALUE>).required(false))
		.get_matches();

	let core_path: &String = matches.get_one("core").unwrap();

	// Load the user's provided core
	let mut core = Core::load(core_path).expect("Provided core failed to load");

	// Initalize the app
	let app = App::new_and_init();

	if let Some(rom_path) = matches.get_one::<String>("rom") {
		core.load_game(rom_path)
			.expect("Provided ROM failed to load");
	}

	// Do the main loop
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
