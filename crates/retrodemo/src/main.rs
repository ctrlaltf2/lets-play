use std::{cell::RefCell, rc::Rc};

use retro_frontend::{core::Core, frontend};
use tracing::Level;
use tracing_subscriber::FmtSubscriber;

use minifb::{Window, WindowOptions};

struct App {
	window: Option<Window>,

	/// True if the app's window has already been updated.
	window_updated: bool,
}

impl App {
	fn new() -> Self {
		Self {
			window: None,
			window_updated: false,
		}
	}

	fn new_and_init() -> Rc<RefCell<App>> {
		let mut app = App::new();
		app.init();

		Rc::new(RefCell::new(app))
	}

	fn init(&mut self) {
		let av_info = frontend::get_av_info().expect("No AV info");

		println!("AV info: {:#?}", av_info);

		let mut window = Window::new(
			"RetroDemo - retro_frontend demo",
			av_info.geometry.base_width as usize,
			av_info.geometry.base_height as usize,
			WindowOptions {
				scale: minifb::Scale::X2,
				..Default::default()
			},
		)
		.expect("Could not open window");

		window.set_target_fps((av_info.timing.fps) as usize);
		self.window = Some(window);
	}

	fn update_video_contents(&mut self, slice: &[u32]) {
		let framebuffer_size = frontend::get_size();
		let _ = self
			.window
			.as_mut()
			.unwrap()
			.update_with_buffer(&slice, framebuffer_size.0 as usize, framebuffer_size.1 as usize);
		self.window_updated = true;
	}

	// ?
	fn update(&mut self) {
		self.window.as_mut().unwrap().update();
	}

	fn should_run(&self) -> bool {
		self.window.as_ref().unwrap().is_open()
	}
}

fn main() {
	// Setup a tracing subscriber
	let subscriber = FmtSubscriber::builder()
		.with_max_level(Level::TRACE)
		.finish();

	tracing::subscriber::set_global_default(subscriber).unwrap();



	// Load a core
	//let mut core = Core::load("./cores/gambatte_libretro.so").expect("Core failed to load");
	//core.load_game("./roms/smw.gb").expect("ROM failed to load");

	let mut core = Core::load("./cores/nestopia_libretro.so").expect("Core failed to load");
	core.load_game("./roms/smb1.nes").expect("ROM failed to load");
	
	let app = App::new_and_init();

	let app_clone = app.clone();
	frontend::set_video_update_callback(move |slice| {
		app_clone.borrow_mut().update_video_contents(slice);
	});

	loop {

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


		frontend::run_frame();
	}
}
