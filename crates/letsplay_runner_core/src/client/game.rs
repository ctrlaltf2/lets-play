use letsplay_av_ffmpeg::encoder_thread::Control;

use super::GraphicsContexts;
use std::time::Instant;

/// A game that should be run by the runner core.
/// A runner implementation implements this trait.
pub trait Game {
	/// Do any initalization tasks. Graphics contexts are provided
	fn init(&mut self, graphics_contexts: &GraphicsContexts, encoder_control: &Control);

	fn reset(&mut self);

	// Shutdown (clean up all resources)
	// Not needed per se since we will just exit after shutdown,
	// but cleaning up after ourselves isn't bad programming practice

	/// Set a named property. Failable.
	fn set_property(&mut self, key: &str, value: &str) -> anyhow::Result<()>;

	// We'll need input + video frame stuff too

	/// Runs a single frame of the game. Should not sleep, [Game::wait_for_next_frame]
	/// will sleep until the next frame (if required).
	fn run_frame(&mut self);

	/// Wait for the next frame/emulation tick, relative to [start].
	fn wait_for_next_frame(&mut self, start: Instant);
}
