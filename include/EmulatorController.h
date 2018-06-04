#pragma once
#include <cstdint>
#include <iostream>
#include <string>

#include "RetroCore.h"
#include "Server.h"

/*
 * Class to be used once per thread, manages a libretro core and emulator, and
 * in the future will manage turns and votes on its own
 */
class EmulatorController {
    /*
     * Pointer to the server managing the emulator controller
     */
    static LetsPlayServer* m_server;

    /*
     * ID of the emulator controller / emulator
     */
    static EmuID_t id;

   public:
    /*
     * The object that manages the libretro lower level functions. Used mostly
     * for loading symbols and storing function pointers.
     */
    static RetroCore Core;

    /*
     * Kind of the constructor. Blocks when called.
     */
    static void Run(const char* corePath, const char* romPath);

    // libretro_core -> Controller ?> Server
    /*
     * Callback for when the libretro core sends eztra info about the
     * environment
     * @return (?) Possibly if the command was recognized
     */
    static bool OnEnvironment(unsigned cmd, void* data);
    // Either:
    //  1) libretro_core -> Controller
    static void OnVideoRefresh(const void* data, unsigned width,
                               unsigned height, size_t stride);
    // Controller -> Server.getInput (input is TOGGLE)
    static void OnPollInput();
    // Controller -> libretro_core
    static std::int16_t OnGetInputState(unsigned port, unsigned device,
                                        unsigned index, unsigned id);
    // Controller -> Server
    static void OnLRAudioSample(std::int16_t left, std::int16_t right);
    // Controller -> Server
    static size_t OnBatchAudioSample(const std::int16_t* data, size_t frames);
};
