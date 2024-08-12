mod game_thread;

use std::time::Duration;

use game_thread::{GameThread};
use tokio::sync::mpsc;
use tokio::time;

use thiserror::Error;

pub use game_thread::Game;

#[derive(Error, Debug)]
pub enum RunnerError {

}

// enum VideoFrame
//	- leased framebuffer (SW)
//	- opengl texture (or EGLImage? that is apparently easier to share between threads)
// probably could be accomplished by guarding accesses to the framebuffer, although i guess
// for more throughput the framebuffer state *could* be cloned. i don't really want to do that though

/// The core of all Let's Play runners.
pub struct Runner {
	game_thread: GameThread
}


impl Runner {
	pub fn new(game: &'static mut (dyn Game + Send)) -> Self{
		Self {
			game_thread: GameThread::spawn(game)
		}
	}

	// run on the main thread
	// spawns all other threads and handles shutdown
	//
	pub async fn run(&self) -> Result<(), RunnerError> {

		// just spin forever for now,
		// well have some logic to dick with everything here

		loop {
			time::sleep(Duration::from_secs(1)).await;
		}

		Ok(())
	}

}


