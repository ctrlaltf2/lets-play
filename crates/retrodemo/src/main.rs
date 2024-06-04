use core::slice;
use std::{cell::RefCell, rc::Rc};

use retro_frontend::{core::Core, frontend, libretro_sys};
use tracing::Level;
use tracing_subscriber::FmtSubscriber;

use minifb::{Window, WindowOptions};

struct App {
	window: Window,

	/// True if the app's window has already been updated.
	window_updated: bool,
}

impl App {
	fn new() -> Self {
		Self {
			window: Window::new(
				"RetroDemo - retro_frontend demo",
				160,
				144,
				WindowOptions {
					scale: minifb::Scale::X2,
					..Default::default()
				},
			)
			.expect("Could not open window"),

			window_updated: false,
		}
	}

	fn new_and_init() -> Rc<RefCell<App>> {
		let mut app = App::new();
		app.init();

		Rc::new(RefCell::new(app))
	}

	fn init(&mut self) {
		//self.window.set_target_fps(60);
	}

	fn update_video_contents(&mut self, slice: &[u32]) {
		let size = frontend::get_size();
		let _ = self
			.window
			.update_with_buffer(&slice, size.0 as usize, size.1 as usize);
		self.window_updated = true;
	}

	// ?
	fn update(&mut self) {
		self.window.update();
	}

	fn should_run(&self) -> bool {
		self.window.is_open()
	}
}

fn main() {
	// Setup a tracing subscriber
	let subscriber = FmtSubscriber::builder()
		.with_max_level(Level::TRACE)
		.finish();

	tracing::subscriber::set_global_default(subscriber).unwrap();

	let app = App::new_and_init();

	let app_clone = app.clone();
	frontend::set_video_update_callback(move |slice| {
		app_clone.borrow_mut().update_video_contents(slice);
	});

	// Load a core
	let mut core = Core::load("./cores/gambatte_libretro.so").expect("Core failed to load");
	core.load_rom("./roms/smw.gb").expect("ROM failed to load");

	// For later
	//frontend::load_core("./cores/nestopia_libretro.so").expect("Core should have loaded");
	//frontend::load_rom("./roms/smb1.nes").expect("ROM should have loaded");

	loop {
		frontend::run_frame();

		{
			let mut borrowed_app = app.borrow_mut();
			if !borrowed_app.should_run() {
				break
			}

			if borrowed_app.window_updated == false {
				borrowed_app.update();
				borrowed_app.window_updated = true;
			} else {
				borrowed_app.window_updated = false;
			}
		}
	}
}
