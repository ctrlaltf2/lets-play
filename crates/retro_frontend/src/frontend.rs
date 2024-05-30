//! The primary frontend API.
//! This is a singleton API, not by choice, but due to Libretro's design.
//!
//! # Safety
//! Don't even think about using this across multiple threads. If you want to run multiple frontends,
//! it's easier to just host this crate in a runner process and fork off those runners.
use crate::frontend_impl::FRONTEND_IMPL;
use crate::joypad::Joypad;
use crate::libretro_sys_new::*;
use crate::result::Result;
use std::cell::RefCell;
use std::rc::Rc;

/// Sets the callback used to update video.
pub fn set_video_update_callback(cb: impl FnMut(&[u32]) + 'static) {
	unsafe {
		FRONTEND_IMPL.set_video_update_callback(cb);
	}
}

/// Sets the callback for video resize.
pub fn set_video_resize_callback(cb: impl FnMut(u32, u32) + 'static) {
	unsafe {
		FRONTEND_IMPL.set_video_resize_callback(cb);
	}
}

/// Sets the callback for audio samples.
pub fn set_audio_sample_callback(cb: impl FnMut(&[i16], usize) + 'static) {
	unsafe {
		FRONTEND_IMPL.set_audio_sample_callback(cb);
	}
}

/// Sets the callback for input polling.
pub fn set_input_poll_callback(cb: impl FnMut() + 'static) {
	unsafe {
		FRONTEND_IMPL.set_input_poll_callback(cb);
	}
}

/// Sets the given port's input device. This takes a implementation of the [crate::joypad::Joypad]
/// trait, which will provide the information needed for libretro to work and all that.
pub fn set_input_port_device(port: u32, device: Rc<RefCell<dyn Joypad>>) {
	unsafe {
		FRONTEND_IMPL.set_input_port_device(port, device);
	}
}

/// Loads a core from the given path into the global frontend state.
///
/// ```rust
/// use retro_frontend::frontend;
/// frontend::load_core("./cores/gbasp.so");
/// ```
pub fn load_core<P: AsRef<std::path::Path>>(path: P) -> Result<()> {
	unsafe { FRONTEND_IMPL.load_core(path) }
}

/// Unloads the core currently running in the global frontend state.
///
/// ```rust
/// use retro_frontend::frontend;
/// frontend::unload_core();
/// ```
pub fn unload_core() -> Result<()> {
	unsafe { FRONTEND_IMPL.unload_core() }
}

/// Loads a ROM into the given core. This function requires that [load_core] has been called and has succeeded first.
///
/// ```rust
/// use retro_frontend::frontend;
/// frontend::load_game("./roms/sma2.gba");
/// ```
pub fn load_game<P: AsRef<std::path::Path>>(path: P) -> Result<()> {
	unsafe { FRONTEND_IMPL.load_game(path) }
}

/// Unloads a ROM from the given core.
pub fn unload_game() -> Result<()> {
	unsafe { FRONTEND_IMPL.unload_game() }
}

/// Gets the core's current AV information.
pub fn get_av_info() -> Result<SystemAvInfo> {
	unsafe { FRONTEND_IMPL.get_av_info() }
}

/// Gets the current framebuffer width and height as a tuple.
pub fn get_size() -> (u32, u32) {
	unsafe { FRONTEND_IMPL.get_size() }
}

/// Runs the currently loaded core for one video frame.
pub fn run_frame() {
	unsafe { FRONTEND_IMPL.run() }
}
