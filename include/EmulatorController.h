#pragma once
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>

#include "Config.h"
#include "LetsPlayServer.h"
#include "RetroCore.h"

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
     * Turn queue for this emulator
     */
    static std::vector<LetsPlayUser*> m_TurnQueue;

    /*
     * Mutex for accessing the turn queue
     */
    static std::mutex m_TurnMutex;

    /*
     * Condition variable for waking up/sleeping the turn thread
     */
    static std::condition_variable m_TurnNotifier;

    /*
     * Keep the turn thread running
     */
    static std::atomic<bool> m_TurnThreadRunning;

    /*
     * Thread that runs EmulatorController::TurnThread()
     */
    static std::thread m_TurnThread;

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

    /*
     * Thread that manages the turns for the users that are connected to the
     * emulator
     */
    static void TurnThread();

    /*
     * Adds a user to the turn request queue, invoked by parent LetsPlayServer
     */
    static void addTurnRequest(LetsPlayUser* user);

    /*
     * Called when a user disonnects, updates turn queue if applicable
     */
    static void userDisconnected(LetsPlayUser* user);

    /*
     * Called when a user connects
     */
    static void userConnected(LetsPlayUser* user);
};
