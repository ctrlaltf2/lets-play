

use retro_frontend::frontend;
use tracing::Level;
use tracing_subscriber::FmtSubscriber;

use singlyton::SingletonUninit;

mod app;

static APP: SingletonUninit<app::App> = SingletonUninit::uninit();


fn main() {
	// Setup a tracing subscriber
	let subscriber = FmtSubscriber::builder()
		.with_max_level(Level::TRACE)
		.finish();

	tracing::subscriber::set_global_default(subscriber).unwrap();

	APP.init(app::App::new());

	frontend::set_video_pixel_format_callback(|pf| {
		println!("Core wants to set pixel format {:?}", pf);

		(*APP.get_mut()).current_pixel_format = pf;
	});

	frontend::load_core("./cores/gambatte_libretro.so").expect("Core should have loaded");
	frontend::load_rom("./roms/smw.gb").expect("ROM should have loaded");
}
