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

	Reset,

	SetProperty {
		key: String,
		value: String,
	},

	/// Initalize the game.
	Initialize,
}

/// A game running on the game thread.
/// With game and game accesories.
pub trait Game {
	fn init(&self);

	fn reset(&self);

	// Shutdown (clean up all resources)
	// Not needed per se since we will just exit after shutdown,
	// but cleaning up after ourselves isn't bad programming practice

	fn set_property(&mut self, key: &str, value: &str);

	// We'll need input + video frame stuff too

	fn run_one(&mut self);
}

fn game_thread_main<'a>(
	mut rx: mpsc::UnboundedReceiver<GameThreadMessage>,
	game: &'a mut dyn Game,
) {
	// true if the loop is suspended.
	// Games start suspended, and should be unsuspended when they are fully configured.
	// FIXME: This should probably be a enum? I mean, it's fine, but if we really wanted this to be
	// better (and properly handle states or whatever) we should probably just like... Do so?
	let mut suspended = true;

	// Spawn the video thread here

	loop {
		match rx.try_recv() {
			Ok(message) => match message {
				GameThreadMessage::Shutdown => break,
				GameThreadMessage::Suspend { suspend } => {
					if suspended != suspend {
						suspended = suspend
					}
				}

				GameThreadMessage::Initialize => {
					game.init();
				}

				GameThreadMessage::Reset => {
					game.reset();
				}

				GameThreadMessage::SetProperty { key, value } => {
					game.set_property(&key, &value);
				}

				// NB: There will be more so I'm leaving this here
				_ => {}
			},

			Err(TryRecvError::Empty) => {}
			Err(TryRecvError::Disconnected) => break,
		}

		if suspended {
			// If the runner is currently suspended, do not call the run function,
			// and instead just wait. We will start running again when the RPC layer
			// tells us to leave suspension.

			thread::sleep(Duration::from_millis(500));
		} else {
			// Call the game's running function. It is expected that it will pace properly on its own.
			game.run_one();

			// FIXME: Submit rendered frame to video thread,
			// unless a frame is duplicated. (this allows us to hold output/do
			// dynamic fps for static/mostly static scenes, which will *heavily* decrease bandwidth consumption
			// on both the server and player ends)
			//
			// Audio should always be submitted and output (Opus supports DTX which would give us similar wins to frame duplication,
			// but I'm not sure if the latency trade off is that worth it for a few kpbs less bandwidth)
		}
	}

	// Cancel and join the video thread (the main thread will wait for us to send our end message before terminating)
}

/// Handle to a spawned game thread
pub struct GameThread {
	tx: mpsc::UnboundedSender<GameThreadMessage>,
}

impl GameThread {
	/// Spawns the game thread.
	pub fn spawn<'a: 'static>(game: &'a mut (dyn Game + Send)) -> GameThread {
		let (tx, rx) = mpsc::unbounded_channel();

		// Spawn the game thread
		let _ = thread::Builder::new()
			.name("letsplay_runner_game".into())
			.spawn(move || {
				game_thread_main(rx, game);
			})
			.expect("Failed to spawn game thread");

		GameThread { tx }
	}

	pub async fn reset(&self) {
		// TODO
		let _ = self.tx.send(GameThreadMessage::Reset);
	}

	pub async fn set_property(&self, key: String, value: String) {
		// TODO
		let _ = self.tx.send(GameThreadMessage::SetProperty {
			key: key.clone(),
			value: value.clone(),
		});
	}

	pub async fn shutdown(&self) {
		let _ = self.tx.send(GameThreadMessage::Shutdown);
		// TODO join thread
	}
}
