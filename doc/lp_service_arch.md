# scratch

## `letsplayd` - main server

-   public HTTP/3 endpoint for the web client
-   handles relaying video from emulators to users
    -   figure out something for CDN usage...
-   handles control in emulators yada yada
-   manages local runners (remote later!)

### Runners

-   letsplayd is a runner server.

    -   It will provide QUIC transport for runner clients to connect to
    -   For local runners, letsplayd will create a pipe and pass it to the runner process when starting an emulator (using command line arguments to tell it what fd the pipe is at)

-   `letsplay_runner_*` executables (runners) are runner clients
    -   They connect to a runner server, and the runner server tells them what to do
        -   Runner servers configure the runner before starting the game.
    -   Runner clients are in charge of encoding A/V data to send to the runner server

## `letsplay_runner_core` - library crate for implementing runner clients and servers, holds most of the core logic

-   Handles most of the details like the runner protobuf protocol for the user, so they just need to implement `letsplay_runner_core::client::Game` and use the macro to implement `main()`.
-   runs multithreaded, where:

    -   main thread is a singlethread tokio runtime (for async io/events)

        -   spawns other threads
        -   connects to QUIC RPC (for external runners)
        -   speaks pipe rpc (for local runners)

    -   Game thread (runs the game code as sync. handles input and such)

    -   a/v threads (encodes a/v from runner thread)

        -   can pick either SW (slowest!), ffmpeg vaapi (best for open source drivers), or gpu specific accessed via ffmpeg (nvenc)
            -   for non-nvenc HW it might be a good idea to write gpu kernels for rgb -> yuv
                since that's what most HW encode engines like. for vaapi we can do opencl.
                -   nvenc it can take rgba directly, so we don't need any conversion for that.

### IPC type

IPC messages are just sent like so:

```c
struct MessageHeader {
	// Size of message.
	u64 size;

	// Protobuf message, either ClientMessage or ServerMessage
	// root.
	//u8 buffer[size];
};
```

Is it bleh? Probably. However, IPC messages _won't_ be directly controlled by users, so this should be "okay" enough. It might suck, but it's not bad.

#### Local

Local IPC just uses a Unix stream socketpair between letsplayd and the runner process (either as fd 3 or if that is "bad" we can allocate a fd number and pass it with like `--local-pairfd=[FD]` I guess).

Simple, yet effective.

#### Remote

Remote IPC should be quic or something.
