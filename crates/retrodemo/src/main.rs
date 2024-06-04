use std::{cell::RefCell, rc::Rc};

use retro_frontend::{frontend, libretro_sys};
use tracing::Level;
use tracing_subscriber::FmtSubscriber;

struct App {
}

impl App {
	fn new() -> Self {
		Self {
		}
	}


	// ?
	fn render(&self) {

	}
}

fn main() {
	// Setup a tracing subscriber
	let subscriber = FmtSubscriber::builder()
		.with_max_level(Level::TRACE)
		.finish();

	tracing::subscriber::set_global_default(subscriber).unwrap();

	let app: Rc<RefCell<App>> = Rc::new(RefCell::new(App::new()));

	//let app_clone = app.clone();


	// TODO
	//frontend::set_video_refresh_callback()

	frontend::load_core("./cores/gambatte_libretro.so").expect("Core should have loaded");
	frontend::load_rom("./roms/smw.gb").expect("ROM should have loaded");

	loop {
		frontend::run();

		app.borrow().render();
	}

}
