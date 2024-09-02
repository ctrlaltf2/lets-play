use std::time::Duration;

use letsplay_runner_core::*;

/// Libretro game. very much TODO
pub struct RetroGame {}

impl RetroGame {
	fn new() -> Self {
		Self {}
	}
}

impl client::Game for RetroGame {
	fn init(&self) {
		tracing::info!("Init game");
	}

	fn reset(&self) {}

	fn set_property(&mut self, key: &str, value: &str) {
		match key {
			"libretro.core" => {
				tracing::info!("Core is {value}");
			}

			_ => {}
		}
	}

	fn run_one(&mut self) {
		tracing::info!("Game run");
		std::thread::sleep(Duration::from_millis(66));
	}
}

client_main!(RetroGame);
