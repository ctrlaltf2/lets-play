use letsplay_core::si_unit::MBNew;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio_util::codec::{Framed, LengthDelimitedCodec};

use crate::shared::proto::{ClientMessage, ServerMessage};

use futures_util::{SinkExt, StreamExt};

// FIXME: DO NOT USE ANYHOW! DO NOT! SIMPLY DO NOT

/// The max frame size of a Let's Play RPC message; in this case 4 MB.
/// This may be lowered or bumped up; do not directly depend on this being stable (for now).
pub const MAX_FRAME_SIZE: usize = MBNew(4).in_bytes();

struct Transport<RW>
where
	RW: AsyncReadExt + AsyncWriteExt + Unpin,
{
	transport: Framed<RW, LengthDelimitedCodec>,
}

impl<RW> Transport<RW>
where
	RW: AsyncReadExt + AsyncWriteExt + Unpin,
{
	pub fn from_stream(stream: RW) -> Self {
		Self {
			transport: LengthDelimitedCodec::builder()
				.length_field_type::<u32>()
				.max_frame_length(MAX_FRAME_SIZE)
				.new_framed(stream),
		}
	}

	#[cfg(feature = "client")]
	pub async fn read_server_message(&mut self) -> anyhow::Result<ServerMessage> {
		if let Some(framed) = self.transport.next().await {
			let bytes = framed?;
			Ok(ServerMessage::parse(&bytes[..])?)
		} else {
			Err(anyhow::anyhow!("End of stream"))
		}
	}
}
