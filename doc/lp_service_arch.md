# scratch

- `letsplayd` - main server
	- public HTTP/3 endpoint
	- handles relaying video from the runners
		- figure out something for CDN usage...
	- handles control in emulators yada yada
	- manages local runners (remote later!)

- `letsplay_runner_core` - library crate for implementing runners, holds most of the core logic
	- handles graceful shutdown
	- runs multithreaded, where:
		- main thread is a singlethread tokio runtime (for io/events)
			- spawns other threads
			- connects to QUIC RPC (for external runners)
			- speaks pipe rpc (for local runners)

		- runner thread (runs the game code as sync. handles input and such)

		- a/v thread (encodes a/v from runner thread)
			- can pick either SW (slowest!), vaapi (best for open source drivers), or gpu specific accessed via ffmpeg (nvenc, amf, mmal)
				- for HW it might be a good idea to write gpu kernels for rgb -> yuv
				since that's what most HW encode engines like. for vaapi we can do opencl,
				for nvenc it can take rgba so we should be fine. for videocore mmal uhh Good Luck

	- does IPC with letsplayd over QUIC for local

- `letsplay_runner_retro` - libretro runner (default)
	- uses retro_frontend to run cores


# Runners

- letsplayd is a runner server
	- It will provide QUIC transport for runner clients to connect to
	- For local runners, letsplayd will create a pipe and pass it to the runner process

- `letsplay_runner_*` are runner clients
	- They connect to a runner server, and the runner server tells them what to do
		- Runner servers configure the runner before starting the game.
	- Runner clients are in charge of encoding A/V data to send to the runner server
		- A runner 
