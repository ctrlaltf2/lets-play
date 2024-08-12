use std::{thread, time::Duration};

// This is used by async code, so we have to use
// Tokio's channels.
use tokio::sync::mpsc::{self, error::TryRecvError};

// We do use the standard library portions for communicating with the video thread,
// which is *not* async, however.
use std::sync::mpsc as sync_mpsc;

pub enum GameThreadMessage {
	/// Shut down the game thread.
	Shutdown,

	Suspend {
		suspend: bool,
	},
}

/// A game running on the game thread.
/// With game and game accesories.
pub trait Game {
	fn init(&self);

	// We'll need input + video frame stuff too

	fn run_one(&mut self);
}

fn game_thread_main<'a>(
	mut rx: mpsc::UnboundedReceiver<GameThreadMessage>,
	game: &'a mut dyn Game,
) {
	// true if the loop is suspended
	let mut suspended = false;

	// Call the init function
	game.init();

	loop {
		match rx.try_recv() {
			Ok(message) => match message {
				GameThreadMessage::Shutdown => break,
				GameThreadMessage::Suspend { suspend } => {
					if suspended != suspend {
						suspended = suspend
					}
				}

				// NB: There will be more so I'm leaving this here
				_ => {}
			},

			Err(TryRecvError::Empty) => {}
			Err(TryRecvError::Disconnected) => break,
		}

		// If the runner is currently suspended, do not call the loop function,
		// and instead just wait.
		if suspended {
			thread::sleep(Duration::from_millis(250));
		} else {
			// It is expected that the loop can pace itself.
			game.run_one();
		}
	}
}

pub struct GameThread {
	tx: mpsc::UnboundedSender<GameThreadMessage>,
	join: thread::JoinHandle<()>,
}

impl GameThread {
	/// Spawns the game thread.
	pub fn spawn<'a: 'static>(game: &'a mut (dyn Game + Send)) -> GameThread {
		let (tx, rx) = mpsc::unbounded_channel();

		// Spawn the game thread
		let join = thread::Builder::new()
			.name("letsplay_runner_game".into())
			.spawn(move || {
				game_thread_main(rx, game);
			})
			.expect("Failed to spawn game thread");

		GameThread { tx, join }
	}
}
