# Scratch

this is a scratch for the configuration letsplay_runner_core will parse
and configure a runner with

all runners should accept the following arguments:

-   `--config /path/to/config.json` to specify configuration file
-   `--emu-id myps1` to specify emulator ID to provide to letsplayd

local runners would probably stuff this in

./data/runners/[id].json

```json
{
	// Property stuff. This is processed by all runners, but the keys are namespaced
	// (except for properties that may be made generic)
	"game_properties": {
		"libretro.core": "/path/to/cores/swanstation_libretro.so",
		"libretro.rom": "/path/to/my/definitely_legal/ps1_game.cue"
	},

	// Metadata overrides. Any specified metadata keys override
	// whatever the runner suggests.
	"metadata_override": {
		"system": "Sony PlayStation",
		"game": "My Totally Definitely Super Legal PS1 Game",
	}
}
```
