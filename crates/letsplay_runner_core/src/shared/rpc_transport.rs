use letsplay_core::si_unit::MB;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio_util::codec::{Framed, LengthDelimitedCodec};

use crate::shared::proto::{ClientMessage, ServerMessage};

use futures_util::{SinkExt, StreamExt};

// FIXME(s): 
//  - DO NOT USE ANYHOW! DO NOT! SIMPLY DO NOT
//  - Instead of providing a static read_xxx helper
//	  I wonder if it would be slightly more ergonomic to 
//	  return a futures map() or whatever which parses
//	  the message. It would allow us to handle hangup
//	  a BIT easier, and tackle the first fixme too.

/// The max frame size of a Let's Play RPC message; in this case 4 MB.
/// This may be lowered or bumped up; do not directly depend on this being stable (for now).
pub const MAX_FRAME_SIZE: usize = MB(4).in_bytes();

/// A Let's Play RPC transport.
pub struct RpcTransport<RW>
where
	RW: AsyncReadExt + AsyncWriteExt + Unpin,
{
	transport: Framed<RW, LengthDelimitedCodec>,
}

impl<RW> RpcTransport<RW>
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
