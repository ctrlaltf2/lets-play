use std::{
	io::Write, sync::{Arc, Mutex}, thread::{self, JoinHandle}, time::{Duration, Instant}
};

// This is used by async code, so we have to use
// Tokio's channels.
use tokio::sync::mpsc::{self, error::TryRecvError};

use super::{Game, GraphicsContexts};

use letsplay_av_ffmpeg::encoder_thread;

enum GameThreadMessage {
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
}

fn main(mut rx: mpsc::UnboundedReceiver<GameThreadMessage>, mut game: Box<dyn Game>) {
	// true if the loop is suspended.
	// Games start suspended, and should be unsuspended when they are fully configured.
	// FIXME: This should probably be a enum? I mean, it's fine, but if we really wanted this to be
	// better (and properly handle states or whatever) we should probably just like... Do so?
	let mut suspended = true;

	// bring up EGL/CUDA/whatever

	let contexts = GraphicsContexts::create(0);

	// Spawn the video thread here

	let video_encoder_control = {
		#[cfg(feature = "av-nvidia")]
		{
			encoder_thread::hardware::nvenc::spawn(
				&contexts.cuda_context.clone(),
				&contexts.cuda_interop_context.clone(),
				&contexts.egl_device_context.clone(),
				false,
			)
		}
	};

	game.init(&contexts, &video_encoder_control);

	// TEMP CODE: This accepts a single tcp connection and broadcasts packets to it
	// this is temporary as all hell
	// yes I know sync io but its running on another thread anyways so its fine.
	let server = std::net::TcpListener::bind("0.0.0.0:6040").expect("rrr");
	let mut clients = Vec::new();

	while clients.len() != 2 {
		let client = server.accept().expect("baned");
		clients.push(client.0);
	}

	tracing::info!("all clients accepted - unblocking and completing intialization");


	let clone = video_encoder_control.clone();
	std::thread::spawn(move || {
		loop {
			let frame = clone.wait_for_packet();
			for client in &mut clients {
				let _ = client.write_all(frame.data().unwrap());
			}
		}
	});

	loop {
		match rx.try_recv() {
			Ok(message) => match message {
				GameThreadMessage::Shutdown => break,
				GameThreadMessage::Suspend { suspend } => {
					if suspended != suspend {
						suspended = suspend
					}
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

		// If the runner is currently suspended, do not call the run function,
		// and instead just wait. We will start running again when the RPC layer
		// tells us to leave suspension.
		if suspended {
			thread::sleep(Duration::from_millis(100));
			continue;
		}

		let now = Instant::now();

		// Do a game frame, temporairly locking the EGL context and making it current.
		// We do this so that the context can be leased to another thread (the encoder thread!).
		{
			let lk = contexts.egl_device_context.lock().expect("???");
			lk.make_current();

			game.run_frame();

			lk.release();
		}

		// Tell the encoder thread to encode the frame we just ran
		video_encoder_control.send_command(encoder_thread::EncoderCommand::SendFrame);

		// FIXME: Output audio
		// Audio should always be submitted and output (Opus supports DTX which would give us similar wins to frame duplication,
		// but I'm not sure if the latency trade off is that worth it for a few kpbs less bandwidth)

		// Let the game pace the game thread.
		game.wait_for_next_frame(now);
	}

	// Cancel and join the video thread (the main thread will wait for us to send our end message before terminating)
}

/// Handle to a spawned game thread
pub struct GameThread {
	tx: mpsc::UnboundedSender<GameThreadMessage>,
	join_handle: JoinHandle<()>,
}

impl GameThread {
	/// Spawns the game thread.
	pub fn spawn(game: Box<dyn Game + Send>) -> GameThread {
		let (tx, rx) = mpsc::unbounded_channel();

		// Spawn the game thread
		let join_handle = thread::Builder::new()
			.name("letsplay_runner_game".into())
			.spawn(move || {
				main(rx, game);
			})
			.expect("Failed to spawn game thread");

		GameThread { tx, join_handle }
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

	pub async fn set_suspend(&self, suspend: bool) {
		let _ = self
			.tx
			.send(GameThreadMessage::Suspend { suspend: suspend });
	}

	/// Shuts down the game thread.
	pub async fn shutdown(&self) {
		let _ = self.tx.send(GameThreadMessage::Shutdown);
		// TODO: join thread (or make it easier for this to be consuming)
	}
}
