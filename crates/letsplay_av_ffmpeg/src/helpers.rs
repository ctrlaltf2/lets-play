use super::ffmpeg;

use ffmpeg::codec as lavc; // lavc

use letsplay_core::Size;

fn create_context_from_codec(codec: ffmpeg::Codec) -> Result<lavc::Context, ffmpeg::Error> {
	unsafe {
		let context = ffmpeg::sys::avcodec_alloc_context3(codec.as_ptr());
		if context.is_null() {
			return Err(ffmpeg::Error::Unknown);
		}

		let context = lavc::Context::wrap(context, None);
		Ok(context)
	}
}

/// Creates a context and sets some parameters we don't
/// need to repeat all over the place.
pub(crate) fn create_context_and_set_common_parameters(
	codec: &str,
	size: &Size,
	max_framerate: u32,
	bitrate: usize,
) -> anyhow::Result<(ffmpeg::Codec, ffmpeg::encoder::video::Video)> {
	let encoder = match ffmpeg::encoder::find_by_name(codec) {
		Some(c) => c,
		None => return Err(anyhow::anyhow!("could not find the codec \"{codec}\"")),
	};

	let mut video_encoder_context = create_context_from_codec(encoder)?.encoder().video()?;

	video_encoder_context.set_width(size.width);
	video_encoder_context.set_height(size.height);
	video_encoder_context.set_frame_rate(Some(ffmpeg::Rational(1, max_framerate as i32)));

	// This probably would be a good idea to keep configurable.
	video_encoder_context.set_bit_rate(bitrate / 2);
	video_encoder_context.set_max_bit_rate(bitrate);

	// qp TODO:
	video_encoder_context.set_qmin(30);
	video_encoder_context.set_qmax(28);

	video_encoder_context.set_time_base(ffmpeg::Rational(1, max_framerate as i32).invert());
	video_encoder_context.set_format(ffmpeg::format::Pixel::YUV420P);

	// The GOP here is setup to balance keyframe retransmission with bandwidth.
	//video_encoder_context.set_gop((max_framerate * 4) as u32);
	video_encoder_context.set_gop(i32::MAX as u32);
	video_encoder_context.set_max_b_frames(0);

	unsafe {
		(*video_encoder_context.as_mut_ptr()).delay = 0;
		(*video_encoder_context.as_mut_ptr()).refs = 0;
	}

	Ok((encoder, video_encoder_context))
}

/// Makes a result from a ffmpeg return value.
pub(crate) fn result_from_ffmpeg_return(error_code: i32) -> Result<(), ffmpeg::Error> {
	if error_code != 0 {
		return Err(ffmpeg::Error::from(error_code));
	}

	Ok(())
}
