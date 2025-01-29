//
// struct Transport<RW>
// where
//	RW: AsyncRead + AsyncWrite + ...
//
// - generic over literally anything that can be read or wtitten as a stream
//
// Use tokio util length delimited codec
// https://docs.rs/tokio-util/latest/tokio_util/codec/length_delimited/index.html
//
// Have two helpers to pull out/parse protos of the each end's roots
// (or fail cleanly)
//
// guess if we *really* wanted to (I don't see it being needed since we'll only own it in
// a fairly flat way) we could have ReadTransport<R> and WriteTransport<W> but idk
