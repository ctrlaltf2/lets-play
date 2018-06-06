# Lets Play
A collaborative libretro emulator frontend

# What is a "Collaborative libretro emulator frontend"?
Why thanks for asking, a collaborative libretro emulator frontend is just a fancy phrase for a video game you can play online, collaboratively, with strangers by taking turns at the controller (so to speak). Users connect using [the web portion](https://github.com/ctrlaltf2/lets-play-client) which connects to this, the backend, and are then able to interact with an emulator that (usually) runs a retro video game system of some sort.

# Building
Have websocketpp and Boost installed then run `cmake .` in the project directory then `make`. To do parallel builds (recommended), do `make -j#` where # is the number of cores your machine has + 1.

# Todo
 - Hunterize (Currently a problem with the websocketpp package and C ABI detection not working)
 - ~~Move to Beast (if its even possible at this point)~~ that's never happening
