use std::time::Duration;

use tracing::Level;
use tracing_subscriber::FmtSubscriber;

use letsplay_runner_core::runner;

/// Libretro game. very much TODO
pub struct RetroGame {}

impl runner::Game for RetroGame {
	fn init(&self) {
		tracing::info!("Init game");
	}

	fn run_one(&mut self) {
		tracing::info!("Game run");
		std::thread::sleep(Duration::from_millis(66));
	}
}

#[tokio::main(flavor = "current_thread")]
async fn main() -> anyhow::Result<()> {
	let subscriber = FmtSubscriber::builder()
		.with_max_level(Level::INFO)
		.finish();

	tracing::subscriber::set_global_default(subscriber).unwrap();

	// there is probably a better way do do this, but we guarantee in this loop,
	// (that should IMO be provided elsewhere?) that we won't touch the game on this thread
	// All interaction will be with the runner which uses the correct way to interact with the game
	let game = Box::new(RetroGame {});
	let box_leaked = Box::leak(game);

	let runner = runner::Runner::new(box_leaked);

	runner.run().await?;

	Ok(())
}
