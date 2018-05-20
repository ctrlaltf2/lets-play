# Lets Play
A collaborative libretro emulator frontend (server)

# Building
Have websocketpp and Boost installed then run `cmake .` in the project directory then `make`. To do parallel builds (recommended), do `make -j#` where # is the number of cores your machine has + 1.

# Todo
 - Hunterize (Currently a problem with the websocketpp package and C ABI detection not working)
 - Move to Beast (if its even possible at this point)
