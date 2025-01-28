use super::ffmpeg;
use super::hwframe::HwFrameContext;
use anyhow::Context;
use cudarc::driver::CudaDevice;
use ffmpeg::error::EAGAIN;

use ffmpeg::codec as lavc; // lavc

use letsplay_core::Size;

/// this is required for libx264 to like. Work
pub fn create_context_from_codec(codec: ffmpeg::Codec) -> Result<lavc::Context, ffmpeg::Error> {
	unsafe {
		let context = ffmpeg::sys::avcodec_alloc_context3(codec.as_ptr());
		if context.is_null() {
			return Err(ffmpeg::Error::Unknown);
		}

		let context = lavc::Context::wrap(context, None);
		Ok(context)
	}
}

fn create_context_and_set_common_parameters(
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

	video_encoder_context.set_bit_rate(bitrate / 4);
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

/// A simple abstraction/interface over ffmpeg.
pub enum VideoEncoder {
	Software {
		encoder: ffmpeg::encoder::video::Encoder,
	},

	/// Hardware encoding, with frames already on or uploaded to the GPU.
	HardwareWithHardwareFrame {
		encoder: ffmpeg::encoder::video::Encoder,
		hw_context: HwFrameContext,
	},
}

impl VideoEncoder {
	/// Creates a new software H.264 encoder.
	pub fn new_h264_software(
		size: Size,
		max_framerate: u32,
		bitrate: usize,
	) -> anyhow::Result<Self> {
		// Create the libx264 context
		let (encoder, mut video_encoder_context) =
			create_context_and_set_common_parameters("libx264", &size, max_framerate, bitrate)?;

		video_encoder_context.set_format(ffmpeg::format::Pixel::YUV420P);

		// FIXME: Allow the client to specify how many threads it wants in a Option<usize>
		// If they do not specify one go to this formula
		let threads = std::thread::available_parallelism().expect("ggg").get() / 8;

		// FIXME: tracing please.
		tracing::info!(
			"VideoEncoder::new_h264_software(): Using {threads} worker threads to encode"
		);

		// Frame-level threading causes [N] frames of latency
		// so we use slice-level threading to reduce the latency
		// as much as possible while still allowing threading
		video_encoder_context.set_threading(ffmpeg::threading::Config {
			kind: ffmpeg::threading::Type::Slice,
			count: threads,
		});

		// Set libx264 applicable dictionary options
		let mut dict = ffmpeg::Dictionary::new();
		dict.set("tune", "zerolatency");
		dict.set("preset", "veryfast");

		// This could probably be moved but then it would mean returning the dictionary too
		// which is fine I guess it just seems a bit rickity
		dict.set("profile", "main");

		// TODO:
		dict.set("crf", "43");
		dict.set("crf_max", "48");

		dict.set("forced-idr", "1");

		let encoder = video_encoder_context
			.open_as_with(encoder, dict)
			.with_context(|| "While opening x264 video codec")?;

		Ok(Self::Software { encoder: encoder })
	}

	/// Creates a new NVIDIA NVENC H.264 encoder,
	///  which encodes frames from GPU memory, via CUDA.
	/// You are expected to handle uploading or otherwise working with a frame on the GPU.
	pub fn new_h264_nvenc_hwframe(
		cuda_device: &CudaDevice,
		size: Size,
		max_framerate: u32,
		bitrate: usize,
	) -> anyhow::Result<Self> {
		let cuda_device_context = super::hwdevice::DeviceContextBuilder::new(
			ffmpeg::sys::AVHWDeviceType::AV_HWDEVICE_TYPE_CUDA,
		)?
		.set_cuda_context((*cuda_device.cu_primary_ctx()) as *mut _)
		.build()
		.with_context(|| "while trying to create CUDA device context")?;

		let mut hw_frame_context = super::hwframe::HwFrameContextBuilder::new(cuda_device_context)?
			.set_width(size.width)
			.set_height(size.height)
			.set_sw_format(ffmpeg::format::Pixel::ZBGR32)
			.set_format(ffmpeg::format::Pixel::CUDA)
			.build()
			.with_context(|| "while trying to create CUDA frame context")?;

		let (encoder, mut video_encoder_context) =
			create_context_and_set_common_parameters("h264_nvenc", &size, max_framerate, bitrate)
				.with_context(|| "while trying to create encoder")?;

		video_encoder_context.set_format(ffmpeg::format::Pixel::CUDA);

		unsafe {
			// FIXME: this currently breaks the avbufferref system a bit
			(*video_encoder_context.as_mut_ptr()).hw_frames_ctx =
				ffmpeg::sys::av_buffer_ref(hw_frame_context.as_raw_mut());
			(*video_encoder_context.as_mut_ptr()).hw_device_ctx =
				ffmpeg::sys::av_buffer_ref(hw_frame_context.as_device_context_mut());
		}

		// set h264_nvenc options
		let mut dict = ffmpeg::Dictionary::new();

		dict.set("tune", "ull");
		dict.set("preset", "p1");

		dict.set("profile", "main");

		// TODO:
		dict.set("rc", "vbr");
		dict.set("qp", "35");

		//dict.set("forced-idr", "1");

		dict.set("delay", "0");
		dict.set("zerolatency", "1");

		let encoder = video_encoder_context
			.open_as_with(encoder, dict)
			.with_context(|| "While opening h264_nvenc video codec")?;

		Ok(Self::HardwareWithHardwareFrame {
			encoder: encoder,
			hw_context: hw_frame_context,
		})
	}

	// NOTE: It's a bit pointless to have this have a mut borrow,
	// but you'll probably have a mutable borrow on this already..
	pub fn is_hardware(&mut self) -> bool {
		match self {
			Self::Software { .. } => false,
			Self::HardwareWithHardwareFrame { .. } => true,
		}
	}

	//pub fn get_hw_context(&mut self) -> &mut HwFrameContext {
	//    match self {
	//        Self::Nvenc { encoder: _, hw_context } => hw_context,
	//        _ => panic!("should not use H264Encoder::get_hw_context() on a Software encoder")
	//    }
	//}

	pub fn create_frame(&mut self) -> anyhow::Result<ffmpeg::frame::Video> {
		match self {
			Self::Software { encoder } => {
				return Ok(ffmpeg::frame::Video::new(
					encoder.format(),
					encoder.width(),
					encoder.height(),
				));
			}

			Self::HardwareWithHardwareFrame {
				encoder,
				hw_context,
			} => {
				let mut frame = ffmpeg::frame::Video::empty();

				// FIXME: This should be a method on the frame context
				// so we don't need to do this constantly.
				unsafe {
					(*frame.as_mut_ptr()).format = ffmpeg::format::Pixel::CUDA as i32;
					(*frame.as_mut_ptr()).width = encoder.width() as i32;
					(*frame.as_mut_ptr()).height = encoder.height() as i32;
					(*frame.as_mut_ptr()).hw_frames_ctx = hw_context.as_raw_mut();

					hw_context.get_buffer(&mut frame)?;

					(*frame.as_mut_ptr()).linesize[0] = (*frame.as_ptr()).width * 4;

					return Ok(frame);
				}
			}
		}
	}

	pub fn send_frame(&mut self, frame: &ffmpeg::Frame) {
		match self {
			Self::Software { encoder } => {
				encoder.send_frame(frame).unwrap();
			}

			Self::HardwareWithHardwareFrame {
				encoder,
				hw_context: _,
			} => {
				encoder.send_frame(frame).unwrap();
			}
		}
	}

	pub fn send_eof(&mut self) {
		match self {
			Self::Software { encoder } => {
				encoder.send_eof().unwrap();
			}

			Self::HardwareWithHardwareFrame {
				encoder,
				hw_context: _,
			} => {
				encoder.send_eof().unwrap();
			}
		}
	}

	fn receive_packet_impl(&mut self, packet: &mut ffmpeg::Packet) -> Result<(), ffmpeg::Error> {
		return match self {
			Self::Software { encoder } => encoder.receive_packet(packet),
			Self::HardwareWithHardwareFrame {
				encoder,
				hw_context: _,
			} => encoder.receive_packet(packet),
		};
	}

	// Shuold this return a Result<ControlFlow> so we can make it easier to know when to continue?
	pub fn receive_packet(&mut self, packet: &mut ffmpeg::Packet) -> anyhow::Result<()> {
		loop {
			match self.receive_packet_impl(packet) {
				Ok(_) => break,
				Err(ffmpeg::Error::Other { errno }) => {
					if errno != EAGAIN {
						return Err(ffmpeg::Error::Other { errno: errno }.into());
					} else {
						// EAGAIN is not fatal, and simply means
						// we should just try again
						break;
					}
				}
				Err(e) => return Err(e.into()),
			}
		}

		Ok(())
	}
}
