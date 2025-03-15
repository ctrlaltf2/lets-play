use std::io::BufReader;

use bytes::{Bytes, BytesMut};
use capnp::{message::ReaderOptions, traits::{FromPointerBuilder, FromPointerReader}};
use futures::{
	stream::{SplitSink, SplitStream},
	Stream,
};
use letsplay_core::si_unit::MB;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio_util::codec::{Framed, LengthDelimitedCodec};

use crate::{rpc_client_capnp::server_message, rpc_server_capnp::client_message};

// We use the Cap'n Proto packed encoding to squeeze precious bytes.
use capnp::serialize_packed;

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
	/// Read a capnp message.
	pub async fn read_message(
		&mut self,
		options: ReaderOptions,
	) -> anyhow::Result<capnp::message::Reader<capnp::serialize::OwnedSegments>> {
		if let Some(framed) = self.read.next().await {
			let bytes = framed?;
			let reader = serialize_packed::read_message(BufReader::new(&bytes[..]), options)?;
			Ok(reader)
		} else {
			Err(anyhow::anyhow!("End of stream"))
		}
	}
}

impl<RW> RpcWriteEnd<RW>
where
	RW: AsyncReadExt + AsyncWriteExt + Unpin,
{
	/// Writes a single capnp message.
	pub async fn write_message<A: capnp::message::Allocator>(&mut self, message: &capnp::message::Builder<A>) -> anyhow::Result<()> {
		use bytes::BufMut;
		let output = BytesMut::new();
		let mut writer = output.writer();
		serialize_packed::write_message(&mut writer, message)?;
		self.write.send(Bytes::from(writer.into_inner())).await?;
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
