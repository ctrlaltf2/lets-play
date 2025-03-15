use crate::VideoEncoder;
use letsplay_core::si_unit::*;
use letsplay_core::Size;

use std::sync::Arc;

#[cfg(feature = "nvidia")]
use cudarc::driver::CudaDevice;

use super::{Packet, PacketType};

/// Shared encoder thread stuff
pub struct EncoderSharedState {
	encoder: Option<VideoEncoder>,
	frame: ffmpeg::frame::Video,
	packet: ffmpeg::Packet,
}

impl EncoderSharedState {
	/// Creates this struct
	pub fn new() -> Self {
		Self {
			encoder: None,
			frame: ffmpeg::frame::Video::empty(),
			packet: ffmpeg::Packet::empty(),
		}
	}

	#[cfg(feature = "nvidia")]
	/// Initalizes a NVENC encoder
	pub fn init_nvenc(
		&mut self,
		cuda_device: &Arc<CudaDevice>,
		size: Size,
		max_bitrate: Mb,
	) -> anyhow::Result<()> {
		self.encoder = Some(VideoEncoder::new_h264_nvenc_hwframe(
			&cuda_device,
			size.clone(),
			60,
			max_bitrate.in_bytes(),
		)?);

		// replace packet
		self.packet = ffmpeg::Packet::empty();
		self.frame = self.encoder.as_mut().unwrap().create_frame()?;

		Ok(())
	}

	#[inline]
	pub fn frame(&mut self) -> &mut ffmpeg::frame::Video {
		&mut self.frame
	}

	/// Sends a frame. The frame must be in the correct format
	/// (for hardware encoding, this is assumed to be RGBX; for x264, this is YUV).
	///
	/// pts is the PTS of the frame
	/// force_keyframe will force a IDR frame if true, otherwise we assume a P frame
	pub fn send_frame(&mut self, pts: u64, force_keyframe: bool) -> Option<Packet> {
		let frame = &mut self.frame;
		let encoder = self.encoder.as_mut().unwrap();

		// set frame type data
		unsafe {
			if force_keyframe {
				(*frame.as_mut_ptr()).pict_type = ffmpeg::sys::AVPictureType::AV_PICTURE_TYPE_I;
				#[cfg(not(feature = "ancient-ffmpeg"))]
				{
					(*frame.as_mut_ptr()).flags = ffmpeg::sys::AV_FRAME_FLAG_KEY;
				}
				(*frame.as_mut_ptr()).key_frame = 1;
			} else {
				(*frame.as_mut_ptr()).pict_type = ffmpeg::sys::AVPictureType::AV_PICTURE_TYPE_P;
				(*frame.as_mut_ptr()).flags = 0i32;
				(*frame.as_mut_ptr()).key_frame = 0;
			}

			(*frame.as_mut_ptr()).pts = pts as i64;
		}

		encoder.send_frame(&*frame);
		encoder
			.receive_packet(&mut self.packet)
			.expect("Failed to recieve packet");

		// This looks sketchy (and sad, due to Clone::clone()), but
		// Packet really is reference-counted (internally, deep in the pits of ffmpeg).
		// So this just clones a pointer. I probably could have just said that.
		unsafe {
			if !self.packet.is_empty() {
				return Some(Packet {
					packet_type: if force_keyframe {
						PacketType::Idr
					} else {
						PacketType::Prev
					},

					packet: self.packet.clone(),
				});
			}
		}

		return None;
	}
}
