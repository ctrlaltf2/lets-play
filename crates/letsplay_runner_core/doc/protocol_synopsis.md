# RPC protocol synopsis

The letsplay runners use a protocol on top of protocol buffers for communicating between runner and letsplayd (and vice versa).

It is transport independent, with the following specific transports:

- For bringup and development, a Node test server implements a SOCK_STREAM UDS named socket.
	This will *not* be used once bringup is going smoothly.

- A local SOCK_STREAM socket pair between letsplayd and the runner is used
	for locally managed runners.
- A QUIC client/server connection is used for remote runners.

# Packet

Every RPC message arrives in a packet data structure, which essentially is a buffer with a size.

```cpp
struct PacketHeader {
	uint32_t packetSize{};
	//uint8_t packetData[packetSize];
};
```

A limit on packet size should probably be put in place once we know a good mean/max for packet sizes, although it will depend on configuration. If it is exceeded the RPC connection should be severed.

# Protobuf

Once a packet has been recieved, depending on the end, we decode the data as a given protobuf root

- For client (game) to server (letsplayd), the server decodes a `ClientMessage` (rpc_server.proto).
- For server to client, the client decodes a `ServerMessage` (rpc_client.proto)


If a packet does not decode to a valid protobuf, we should sever the connection immediately without question.


## Severing connection

For a local runner, we can just close() the socket and then kill the process.

For non-local runners we can shutdown the stream and then the connection I think. The runner will then nicely exit.
