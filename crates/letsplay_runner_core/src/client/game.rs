use letsplay_av_ffmpeg::encoder_thread::Control;

use super::GraphicsContexts;
use std::time::Instant;

#[repr(u32)]
#[derive(Eq, PartialEq, PartialOrd, Ord)]
pub enum ConfigurationState {
	ConfigurationNeeded,
	ConfigurationComplete
}

/// A game that should be run by the runner core.
/// 
/// # Implementation notes
/// `letsplay_runner_core` spawns off another OS thread to run implementations of this trait on.
/// Note that games only run on that thread, nothing else.
pub trait Game {
	/// Do any initalization tasks. Graphics contexts are provided
	fn init(&mut self, graphics_contexts: &GraphicsContexts, encoder_control: &Control);

	fn reset(&mut self);

	// Shutdown (clean up all resources)
	// Not needed per se since we will just exit after shutdown,
	// but cleaning up after ourselves isn't bad programming practice

	/// Gets the configuration state of the game.
	fn get_configuration_state(&mut self) -> ConfigurationState;

	/// Set a named property. Failable.
	fn set_property(&mut self, key: &str, value: &str) -> anyhow::Result<()>;

	// We'll need input + video frame stuff too

	/// Runs a single frame of the game. Should not sleep, since [Game::wait_for_next_frame]
	/// will sleep until the next frame (if required).
	fn run_frame(&mut self);

	/// Wait for the next frame/emulation tick. The start must
	/// be a Instant provided that was initalized before the game
	/// was ran for the frame.
	fn wait_for_next_frame(&mut self, start: Instant);
}
