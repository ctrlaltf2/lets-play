# scratch

- `letsplayd` - main server
	- public QUIC endpoint (MoQ over webtransport, maybe MoQT too?)
	- handles relaying video from the runners
		- figure out something for CDN usage...
	- manages local runners (remote later!)
	- Also exports local ipc for runners

- `letsplay_runner_core` - library crate for implementing runners, holds most of the core logic
	- runs multithreaded, where:
		- main thread is a singlethread tokio runtime (for io/events)
			- spawns other threads

		- runner thread (runs runner code as sync, pulls out messages)

		- a/v thread (encodes a/v from runner thread)
			- can have pick either SW (slowest!), vaapi (best for open source drivers), or gpu specific accessed via ffmpeg (nvenc, amf, mmal)
				- for HW it might be a good idea to write gpu kernels for rgb -> yuv
				since that's what most HW decode engines like. for vaapi we can do opencl,
				for nvenc it might be wise to just use cuda. for videocore mmal uhh Good Luck

	- does IPC with letsplayd (local UDS for now, can be expanded later)
	- video can be local UDS for now, maybe later on once remote runners exist
		we can do internal MoQ or something???

- `letsplay_retro_runner` - libretro runner (default)
	- uses retro_frontend to run cores
