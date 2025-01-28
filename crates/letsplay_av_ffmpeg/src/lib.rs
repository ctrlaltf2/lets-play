//! A fun set of helpers used in Let's Play runners
//! to make A/V encoding less painful and *fast*.
//!
//! # Notes
//! Documentation may be a bit lackluster at the moment,
//! the best way to learn how to use this crate is probably
//! to look at the letsplay_runner_core crate.

mod helpers;

mod video_encoder;

/// Re-export of `ffmpeg` crate.
pub use ffmpeg;

mod hwdevice;
mod hwframe;

pub mod encoder_thread;

#[cfg(feature = "nvidia")]
pub mod cuda_gl;

pub use video_encoder::*;
