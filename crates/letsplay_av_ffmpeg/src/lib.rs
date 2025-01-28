//! A fun set of helpers used in Let's Play runners
//! to make A/V encoding less painful and *fast*.
//! 
//! # Notes
//! Documentation may be a bit lackluster at the moment,
//! the best way to learn how to use this crate is probably
//! to look at the letsplay_runner_core crate.
pub mod video_encoder;

/// Re-export of `ffmpeg` crate.
pub use ffmpeg as ffmpeg;

pub mod hwdevice;
pub mod hwframe;

#[allow(unused)] // FIXME
pub mod encoder_thread;

#[cfg(feature = "nvidia")]
pub mod cuda_gl;

// from hgaiser/moonshine
pub(crate) fn check_ret(error_code: i32) -> Result<(), ffmpeg::Error> {
	if error_code != 0 {
		return Err(ffmpeg::Error::from(error_code));
	}

	Ok(())
}
