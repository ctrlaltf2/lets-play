use bytes::{Bytes, BytesMut};
use futures::{
	stream::{SplitSink, SplitStream},
	Stream,
};
use letsplay_core::si_unit::MB;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio_util::codec::{Framed, LengthDelimitedCodec};

use crate::shared::proto::{ClientMessage, ServerMessage};

use futures_util::{SinkExt, StreamExt};

// FIXME(s):
//  - DO NOT USE ANYHOW! DO NOT! SIMPLY DO NOT
//		(Use thiserror instead probably)
//  - Instead of providing a static read_xxx helper
//	  I wonder if it would be slightly more ergonomic to
//	  return a futures map() or whatever which parses
//	  the message. It would allow us to handle hangup
//	  a BIT easier, and tackle the first fixme too.

/// The max frame size of a Let's Play RPC message; in this case 4 MB.
/// This may be lowered or bumped up; do not directly depend on this being stable (for now).
pub const MAX_FRAME_SIZE: usize = MB(4.0).in_bytes();

pub struct RpcReadEnd<RW>
where
	RW: AsyncReadExt + AsyncWriteExt + Unpin,
{
	read: SplitStream<Framed<RW, LengthDelimitedCodec>>,
}

pub struct RpcWriteEnd<RW>
where
	RW: AsyncReadExt + AsyncWriteExt + Unpin,
{
	write: SplitSink<Framed<RW, LengthDelimitedCodec>, Bytes>,
}

impl<RW> RpcReadEnd<RW>
where
	RW: AsyncReadExt + AsyncWriteExt + Unpin,
{
	/// Read a protobuf message.
	pub async fn read_message<T: Message>(&mut self) -> anyhow::Result<T> {
		if let Some(framed) = self.read.next().await {
			let bytes = framed?;
			Ok(T::parse(&bytes[..])?)
		} else {
			Err(anyhow::anyhow!("End of stream"))
		}
	}
}

impl<RW> RpcWriteEnd<RW>
where
	RW: AsyncReadExt + AsyncWriteExt + Unpin,
{
	/// Writes a single protobuf message.
	pub async fn write_message<T: Message>(&mut self, message: &T) -> anyhow::Result<()> {
		let bytes = Bytes::from(message.serialize()?);
		self.write.send(bytes).await?;
		Ok(())
	}
}

/// Creates split read/write ends for Let's Play RPC.
pub fn from_stream<RW: AsyncReadExt + AsyncWriteExt + Unpin>(
	stream: RW,
) -> (RpcWriteEnd<RW>, RpcReadEnd<RW>) {
	let framed = LengthDelimitedCodec::builder()
		.length_field_type::<u32>()
		.max_frame_length(MAX_FRAME_SIZE)
		.new_framed(stream);

	let (write, read) = framed.split();

	(RpcWriteEnd { write }, RpcReadEnd { read })
}
