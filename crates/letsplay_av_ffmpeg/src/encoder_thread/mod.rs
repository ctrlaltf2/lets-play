pub mod hardware;

// FIXME: (Re-)implement software sad path, as well as
// CUDA-less hardware encoding. (Maybe use OpenCL? Oh god)
// For CUDA-less hardware encoding I think we can use
// egl images? We already should lock the context, so that's fine
//pub mod software_frame;

use std::sync::{Arc, Condvar, Mutex, MutexGuard};
use std::time::Duration;

use letsplay_core::Size;

/// A command for the encoder thread.
#[derive(Debug)]
pub enum EncoderCommand {
	/// (Re)-initalize encoding for the given resolution
	Init { size: Size },

	/// Shut down the encoder thread.
	Shutdown,

	/// Force the next frame, output via a [EncoderCommand::SendFrame]
	/// command, to be a key frame (IDR frame).
	ForceKeyframe,

	/// Encode a frame.
	SendFrame,
}

/// Shared control for the encoder thread.
#[derive(Clone)]
pub struct Control {
	// input
	/// NOTE: Only signal. Do not wait
	input_updated_cv: Arc<Condvar>,
	input: Arc<Mutex<EncoderCommand>>,

	processed: Arc<Mutex<bool>>,
	processed_cv: Arc<Condvar>,
}

/// Allows waiting for the video encoder thread
/// to produce packets.
#[derive(Clone)]
pub struct PacketWaiter {
	packet_updated_cv: Arc<Condvar>,
	packet: Arc<Mutex<ffmpeg::Packet>>,
}

// FIXME for these impls:
// DO NOT .expect(). PLEASE.

impl PacketWaiter {
	/// Wait for a packet without timeout.
	pub fn wait_for_packet(&self) -> MutexGuard<'_, ffmpeg::Packet> {
		let mut lk = self.packet.lock().expect("failed to lock packet");
		let mut waited_lk = self
			.packet_updated_cv
			.wait(lk)
			.expect("failed to wait for encoder thread to update packet");
		waited_lk
	}

	/// Wait for a packet with timeout.
	pub fn wait_for_packet_timeout(
		&self,
		timeout: Duration,
	) -> Option<MutexGuard<'_, ffmpeg::Packet>> {
		let mut lk = self.packet.lock().expect("failed to lock packet");
		let mut wait_result = self
			.packet_updated_cv
			.wait_timeout(lk, timeout)
			.expect("failed to wait");

		if wait_result.1.timed_out() {
			None
		} else {
			Some(wait_result.0)
		}
	}
}

impl Control {
	/// Sends a command to the encoder thread.
	pub fn send_command(&self, cmd: EncoderCommand) {
		{
			let mut lk = self.input.lock().expect("failed to lock input");
			//println!("Sent {:?}", cmd);
			*lk = cmd;
			self.input_updated_cv.notify_one();
		}

		// Wait for the encoder thread to notify completion.
		{
			let mut lklk = self
				.processed
				.lock()
				.expect("failed to lock processed flag");
			*lklk = false;

			while *lklk == false {
				lklk = self
					.processed_cv
					.wait(lklk)
					.expect("failed to wait for encoder thread to signal completion");
			}
		}
	}

	/// Shorthand to shutdown the encoder
	pub fn shutdown(&self) {
		self.send_command(EncoderCommand::Shutdown);
	}
}
