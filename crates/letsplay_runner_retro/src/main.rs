use std::time::{Duration, Instant};

use letsplay_core::sleep;
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

	fn run_frame(&mut self) {
		tracing::info!("Game run");
		// Sleep for a bit to mimic actual work
		std::thread::sleep(Duration::from_millis(33));
	}

	fn wait_for_next_frame(&mut self, start: Instant) {
		let next_frame = start.checked_add(Duration::from_millis(66)).unwrap();
		sleep::sleep_until(next_frame);
	}
}

client_main!(RetroGame);
