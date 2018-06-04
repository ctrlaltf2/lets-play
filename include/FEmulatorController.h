#pragma once
#include "Server.h"
/*
 * Function to be run in a thread. Starts up a libretro core and reads a rom for
 * it. Once that's done, it links with the server parameter to do things such as
 * send screen updates or respond to input sends
 * @param corePath The path to the libretro core that should be loaded. NOTE: On
 * linux, the path must be either relative (./filename.so) or absolute
 * (/usr/lib/retroarch/core.so), filenames do not work
 * @param romPath The path to the rom that should be loaded and started. Same
 * filename rules apply as above.
 */
void EmulatorController(const char* corePath, const char* romPath);
