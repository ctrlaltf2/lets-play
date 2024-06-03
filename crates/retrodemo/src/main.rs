use retro_frontend::frontend;
use tracing::Level;
use tracing_subscriber::FmtSubscriber;

fn main() {
	// Setup a tracing subscriber
	let subscriber = FmtSubscriber::builder()
		.with_max_level(Level::TRACE)
		.finish();

	tracing::subscriber::set_global_default(subscriber).unwrap();

	frontend::load_core("./cores/gambatte_libretro.so").expect("Core should have loaded");

	// We could now do interesting stuff
	println!("Core loaded!");

	frontend::load_rom("./roms/smw.gb").expect("ROM should have loaded");

	println!("ROM loaded!");

	// Unload the core since we're shutting down now
	frontend::unload_core().expect("Core should have unloaded");
	
	println!("Core unloaded.")
}
