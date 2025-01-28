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

pub use game_thread::GraphicsContexts;

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
	fn new(game: Box<dyn Game + Send>) -> Self {
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

		// Once we exit the loop, we should shutdown the game thread
		// and all our resources

		Ok(())
	}
}

pub async fn main(game: Box<dyn Game + Send>) -> Result<(), RunnerError> {
	let subscriber = FmtSubscriber::builder()
		.with_max_level(Level::INFO)
		.with_thread_names(true)
		.finish();

	tracing::subscriber::set_global_default(subscriber).unwrap();

	// Start the runner
	let runner = Runner::new(game);
	runner.run().await
}
