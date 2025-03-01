use std::{
	io::Write,
	thread::{self, JoinHandle},
	time::{Duration, Instant},
};

use letsplay_core::si_unit::Mb;
// This is used by async code, so we have to use
// Tokio's channels.
use tokio::sync::{
	mpsc::{self, error::TryRecvError},
	oneshot,
};

use crate::client::ConfigurationState;

use super::{Game, GraphicsContexts};

use letsplay_av_ffmpeg::encoder_thread::{self, PacketWaiter};

enum GameThreadMessage {
	/// Shut down the game thread.
	Shutdown,

	Suspend {
		suspend: bool,
		tx: oneshot::Sender<()>,
	},

	Reset {
		tx: oneshot::Sender<()>,
	},

	SetProperty {
		key: String,
		value: String,

		tx: oneshot::Sender<()>,
	},
}

fn main(
	mut message_rx: mpsc::UnboundedReceiver<GameThreadMessage>,
	video_packet_waiter_tx: oneshot::Sender<PacketWaiter>,
	mut game: Box<dyn Game>,
) {
	// true if the loop is suspended.
	// Games start suspended, and should be unsuspended when they are fully configured.
	// FIXME: This should probably be a enum? I mean, it's fine, but if we really wanted this to be
	// better (and properly handle states or whatever) we should probably just like... Do so?
	let mut game_currently_suspended = true;

	let contexts = GraphicsContexts::create(0);

	let (video_encoder_control, packet_waiter) = {
		#[cfg(feature = "av-nvidia")]
		{
			encoder_thread::hardware::nvenc::spawn(
				&contexts.cuda_context.clone(),
				&contexts.cuda_interop_context.clone(),
				&contexts.egl_device_context.clone(),
				false,
				// TODO: Provide configuration in runner json
				// for now, moving the hardcoding to here is good enough
				Mb(2.0),
			)
		}
	};

	// if this fails and returns the packet waiter, then we are probably completely screwed anyways,
	// since the client main should never hang up until after it has gotten this
	match video_packet_waiter_tx.send(packet_waiter) {
		Ok(_) => {}
		Err(_) => panic!("Calling thread did not recieve the packet waiter; we probably died"),
	}

	game.init(&contexts, &video_encoder_control);

	loop {
		match message_rx.try_recv() {
			Ok(message) => {
				match message {
					GameThreadMessage::Shutdown => break,
					GameThreadMessage::Suspend { suspend, tx } => {
						if game_currently_suspended != suspend {
							// TODO: This is temporary, since we probably should instead shutdown or something
							// since a unconfigured game indicates a JSON misconfiguration.
							if suspend == false
								&& game.get_configuration_state()
									== ConfigurationState::ConfigurationNeeded
							{
								tracing::error!("Attempting to unsuspend a game that hasn't been fully configured!");
								continue;
							}

							// Force the next frame output to be an IDR frame when leaving suspend.
							if game_currently_suspended == true {
								video_encoder_control
									.send_command(encoder_thread::EncoderCommand::ForceKeyframe);
							}

							game_currently_suspended = suspend;
						}

						let _ = tx.send(());
					}

					GameThreadMessage::Reset { tx } => {
						game.reset();
						let _ = tx.send(());
					}

					GameThreadMessage::SetProperty { key, value, tx } => {
						// TODO: we should send the error result to the given tx
						// so that we can propegate errors to the main thread
						match game.set_property(&key, &value) {
							Ok(_) => {}
							Err(err) => {
								tracing::error!("Error setting property {key} to {value}: {}", err);
							}
						};
						let _ = tx.send(());
					}
				}
			}

			Err(TryRecvError::Empty) => {}
			Err(TryRecvError::Disconnected) => break,
		}

		// If the runner is currently suspended, do not call the run function,
		// and instead just wait. We will start running again when the RPC layer
		// tells us to leave suspension.
		if game_currently_suspended {
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

	// Shut down the encoder thread.
	video_encoder_control.shutdown();
}

/// Handle to a spawned game thread
pub struct GameThread {
	tx: mpsc::UnboundedSender<GameThreadMessage>,
	join_handle: JoinHandle<()>,
}

impl GameThread {
	/// Spawns the game thread.
	pub fn spawn(
		game: Box<dyn Game + Send>,
		video_packet_waiter_tx: oneshot::Sender<PacketWaiter>,
	) -> GameThread {
		let (tx, rx) = mpsc::unbounded_channel();

		// Spawn the game thread
		let join_handle = thread::Builder::new()
			.name("letsplay_runner_game".into())
			.spawn(move || {
				main(rx, video_packet_waiter_tx, game);
			})
			.expect("Failed to spawn game thread");

		GameThread { tx, join_handle }
	}

	pub async fn reset(&self) {
		let (tx, rx) = oneshot::channel();
		let _ = self.tx.send(GameThreadMessage::Reset { tx });
		let _ = rx.await;
	}

	pub async fn set_property(&self, key: String, value: String) {
		// TODO: This should be failable
		let (tx, rx) = oneshot::channel();
		let _ = self.tx.send(GameThreadMessage::SetProperty {
			key: key.clone(),
			value: value.clone(),
			tx,
		});
		let _ = rx.await;
	}

	pub async fn set_suspend(&self, suspend: bool) {
		let (tx, rx) = oneshot::channel();
		let _ = self.tx.send(GameThreadMessage::Suspend {
			suspend: suspend,
			tx,
		});
		let _ = rx.await;
	}

	/// Shuts down and waits for the game thread to exit.
	pub fn shutdown(self) {
		let _ = self.tx.send(GameThreadMessage::Shutdown);
		self.join_handle.join().expect("Failed to join game thread");
	}
}
