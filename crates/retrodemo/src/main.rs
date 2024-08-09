use tracing::Level;
use tracing_subscriber::FmtSubscriber;

use clap::{arg, command};

mod app;
mod window;

use anyhow::Result;

use app::*;

fn main() -> Result<()> {
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

	let mut app = App::new();

	app.load_core(core_path)?;

	if let Some(rom_path) = matches.get_one::<String>("rom") {
		app.load_game(rom_path)?;
	}

	app.init();

	app.main_loop();

	Ok(())
}
