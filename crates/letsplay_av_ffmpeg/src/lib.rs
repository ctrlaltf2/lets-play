pub mod h264_encoder;
//pub mod lc_muxer;

/// Re-export of `ffmpeg` crate.
pub use ffmpeg as ffmpeg;

pub mod hwdevice;
pub mod hwframe;

#[allow(unused)] // FIXME
pub mod encoder_thread;

pub mod cuda_gl;

// from hgaiser/moonshine
pub(crate) fn check_ret(error_code: i32) -> Result<(), ffmpeg::Error> {
	if error_code != 0 {
		return Err(ffmpeg::Error::from(error_code));
	}

	Ok(())
}
