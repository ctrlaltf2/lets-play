//! Code used for runner clients (that connect to letsplayd)

mod game;
mod game_thread;
mod graphics_contexts;

pub use game::*;
pub use graphics_contexts::*;

use std::time::Duration;

use game_thread::GameThread;
use tokio::time;

use tracing::Level;
use tracing_subscriber::FmtSubscriber;

use thiserror::Error;

// TODO: Use this
#[derive(Error, Debug)]
pub enum RunnerError {}

/// The main Let's Play runners using the letsplay_runner_core crate utilize.
pub async fn main(game: Box<dyn Game + Send>) -> anyhow::Result<()> {
	let subscriber = FmtSubscriber::builder()
		.with_max_level(Level::INFO)
		.with_thread_names(true)
		.finish();

	tracing::subscriber::set_global_default(subscriber).unwrap();

	// DOGFOOD: 
	//	- Implement JSON configuration
	//	- Implement RPC client (including both local and remote modes)

	let game_thread = GameThread::spawn(game);

	// TEMP: Just for testing libretro runner bringup
	game_thread
		.set_property(
			"libretro.core".into(),
			"cores/swanstation_libretro.so".into(),
		)
		.await;
	game_thread
		.set_property(
			"libretro.rom".into(),
			"roms/merged/nmv2/ja/nmv2ja.cue".into(),
		)
		.await;

	// FIXME: Remove this when RPC client is implemented
	// (this is temporary for bringup)
	game_thread.set_suspend(false).await;

	loop {
		time::sleep(Duration::from_secs(1)).await;
	}

	game_thread.shutdown();

	Ok(())
}
