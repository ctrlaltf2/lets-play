use bytes::{Bytes, BytesMut};
use futures::{
	stream::{SplitSink, SplitStream},
	Stream,
};
use letsplay_core::si_unit::MB;
use protobuf::{Message, Serialize};
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
	read: SplitStream<Framed<RW, LengthDelimitedCodec>>,
	write: SplitSink<Framed<RW, LengthDelimitedCodec>, Bytes>,
}

impl<RW> RpcTransport<RW>
where
	RW: AsyncReadExt + AsyncWriteExt + Unpin,
{
	pub fn from_stream(stream: RW) -> Self {
		let framed = LengthDelimitedCodec::builder()
			.length_field_type::<u32>()
			.max_frame_length(MAX_FRAME_SIZE)
			.new_framed(stream);

		let (write, read) = framed.split();
		Self { read, write }
	}

	/* this MIGHT be better but doesnt compile
	pub async fn sink_server_messages(&self) -> impl Stream<Item = anyhow::Result<ServerMessage>> {
		self.read.map(|res| {
			let bytes = res?;
			Ok(ServerMessage::parse(&bytes[..])?)
		})
	}
	*/

	/// Writes a single protobuf message.
	pub async fn write_message<T: Message>(
		&mut self,
		message: &T,
	) -> anyhow::Result<()> {
		let bytes = Bytes::from(message.serialize()?);
		self.write.send(bytes).await?;
		Ok(())
	}

	/// Read a protobuf message.
	pub async fn read_message<T: Message>(
		&mut self
	) -> anyhow::Result<T> {
		if let Some(framed) = self.read.next().await {
			let bytes = framed?;
			Ok(T::parse(&bytes[..])?)
		} else {
			Err(anyhow::anyhow!("End of stream"))
		}
	}
}
