//! Code used for runner clients (that connect to letsplayd)

mod game;
mod game_thread;
mod graphics_contexts;

pub use game::*;
pub use graphics_contexts::*;

use std::{collections::HashMap, time::Duration};

use game_thread::GameThread;
use tokio::time;

use tracing::Level;
use tracing_subscriber::FmtSubscriber;

use thiserror::Error;

use serde::Deserialize;

use clap::*;


// TODO: Use this
#[derive(Error, Debug)]
pub enum RunnerError {}


#[derive(Deserialize)]
struct RunnerMetadataOverride {
	system: Option<String>,
	game: Option<String>,
}

#[derive(Deserialize)]
struct RunnerConfiguration {
	/// Game Properties
	game_properties: HashMap<String, String>,
	metadata_override: RunnerMetadataOverride
}



/// The main Let's Play runners using the letsplay_runner_core crate utilize.
pub async fn main(game: Box<dyn Game + Send>) -> anyhow::Result<()> {
	// FIXME: Add
	// --rpc-local-pair-fd=[FD]
	// --rpc-quic-address=quic://[addr]
	let name = Box::leak(Box::new(std::env::args().next().unwrap())); // :( fix this too

	let matches = Command::new(name.as_str())
		.about(game.game_desc())
		.arg(
			arg!(--config <CONFIG_FILE>)
				.help("Configuration file")
				.required(true),
		)
		.get_matches();

	let config_path = matches.get_one::<String>("config").unwrap();

	if !std::fs::exists(config_path)? {
		tracing::error!("give a good config file next time");
		std::process::exit(1);
	}

	let config_data = std::fs::read_to_string(config_path)?;
	let config = serde_json::from_str::<RunnerConfiguration>(&config_data)?;

	let subscriber = FmtSubscriber::builder()
		.with_max_level(Level::INFO)
		.with_thread_names(true)
		.finish();

	tracing::subscriber::set_global_default(subscriber).unwrap();

	// DOGFOOD:
	//	- Implement RPC client (including both local and remote modes)

	let game_thread = GameThread::spawn(game);

	// Set properties
	// Make this not suck later.
	for (key, value) in config.game_properties.iter() {
		game_thread.set_property(key.clone(), value.clone()).await;
	}

	// FIXME: Remove this when RPC client is implemented
	// (this is temporary for bringup)
	game_thread.set_suspend(false).await;

	loop {
		time::sleep(Duration::from_secs(1)).await;
	}

	game_thread.shutdown();

	// Remove when sad code is made no longer sad
	drop(Box::from(&mut name as *mut _));

	Ok(())
}
