# Let's Play
A web-based collaborative RetroArch frontend

![screenshot](https://raw.githubusercontent.com/ctrlaltf2/lets-play-server/master/screenshot.png)

# What is a "web-based collaborative RetroArch frontend"?
Why thanks for asking, a A web-based collaborative RetroArch frontend is just a fancy phrase for a video game you can play online, collaboratively, with strangers by taking turns at the controller (so to speak). Users connect using [the web portion](https://github.com/ctrlaltf2/lets-play-client) which connects to this, the backend, and are then able to interact with an emulator that (usually) runs a retro video game system of some sort.

# Requirements
 - Compiler with full support for `std::filesystem` (g++ >= 8.0.1)
 - websocketpp
 - Boost
    - program_options
    - UUID
    - Asio (for websocketpp)
 - turbojpeg

# Building
To build, simply type `cmake .` in the top level directory, then type `make`. To do parallel builds (recommended), type `make -j#` where `#` is the number of cores you have on your machine. After the build, the binary will be in `./bin/` as `letsplay`.

# Note
The default admin password is `LetsPlay`. **This should be changed for your own security**. Change the password by directly modifying the config. The values that should be modified are `config["serverConfig"]["salt"]` and `config["serverConfig"]["adminHash"]`. The hash should be generated by taking your password, appending the salt value, and md5 hashing it.
