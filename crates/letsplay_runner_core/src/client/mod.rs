//! Code used for runner clients (that connect to letsplayd)
mod game_thread;

use std::time::Duration;

use game_thread::GameThread;
use tokio::sync::mpsc;
use tokio::time;

use tracing::Level;
use tracing_subscriber::FmtSubscriber;

use thiserror::Error;

pub use game_thread::Game;

#[derive(Error, Debug)]
pub enum RunnerError {}

// enum VideoFrame
//	- leased framebuffer (SW)
//	- opengl texture (or EGLImage? that is apparently easier to share between threads)
// probably could be accomplished by guarding accesses to the framebuffer, although i guess
// for more throughput the framebuffer state *could* be cloned. i don't really want to do that though

/// The core of all Let's Play runners.
struct Runner {
	game_thread: GameThread,
}

impl Runner {
	fn new(game: &'static mut (dyn Game + Send)) -> Self {
		Self {
			game_thread: GameThread::spawn(game),
		}
	}

	async fn shutdown(&self) {
		self.game_thread.shutdown().await;
	}

	// run on the main thread
	// spawns all other threads and handles shutdown
	//
	async fn run(&self) -> Result<(), RunnerError> {
		// just spin forever for now
		//
		// TODO: implement RPC client

		loop {
			time::sleep(Duration::from_secs(1)).await;
		}

		Ok(())
	}
}

pub async fn main(game: Box<dyn Game + Send>) -> Result<(), RunnerError> {
	let subscriber = FmtSubscriber::builder()
		.with_max_level(Level::INFO)
		.with_thread_names(true)
		.finish();

	tracing::subscriber::set_global_default(subscriber).unwrap();

	// There is probably a better way do do this, but we guarantee in this loop
	// that we won't touch the game on this thread.
	//
	// All interaction will be with the runner which uses the correct way to interact with the game.
	let box_leaked = Box::leak(game);

	// Start the runner
	let runner = Runner::new(box_leaked);
	runner.run().await
}
