class EmulatorController;
struct EmulatorControllerProxy;
struct RGBColor;
struct VideoFormat;
#pragma once
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>

#include "Config.h"
#include "LetsPlayServer.h"
#include "LetsPlayUser.h"
#include "RetroCore.h"

// Because you can't pass a pointer to a static instance of a class...
struct EmulatorControllerProxy {
    std::function<void(LetsPlayUser*)> addTurnRequest, userDisconnected,
        userConnected;
};

struct RGBColor {
    /*
     * 0-255 value
     */
    std::atomic<std::uint8_t> r{0}, g{0}, b{0};

    /*
     * Color visible or not
     */
    std::atomic<bool> a{0};
};

struct VideoFormat {
    /*
     * Masks for red, green, blue, and alpha
     */
    std::atomic<std::uint16_t> rMask{0b111110000000000000000},
        gMask{0b000001111100000000000}, bMask{0b000000000011111000000},
        aMask{0b000000000000000000000};

    /*
     * Bit shifts for red, green, blue, and alpha
     */
    std::atomic<std::uint8_t> rShift{10}, gShift{5}, bShift{0}, aShift{15};

    /*
     * How many bits per pixel
     */
    std::atomic<std::uint8_t> bitsPerPel{16};

    /*
     * Width, height
     */
    std::atomic<std::uint32_t> width{0}, height{0};
};

/*
 * Class to be used once per thread, manages a libretro core, smulator, and its
 * own turns through callbacks
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

    /*
     * Stores the masks and shifts required to generate a rgb 0xRRGGBB) vector
     * from the video_refresh callback data
     */
    static VideoFormat m_videoFormat;

    /*
     * Vector representing the full screen, only used on sync commands and to
     * keep track of differences
     */
    static std::vector<std::vector<RGBColor>> m_screen;

    /*
     * The frame that is to be sent off to clients, only the changes since the
     * last frame, sent on update commands
     */
    static std::vector<std::vector<RGBColor>> m_nextFrame;

    /*
     * Mutex for accessing m_screen or m_nextFrame
     */
    std::mutex m_screenMutex;

   public:
    /*
     * Pointer to some functions that the managing server needs to call
     */
    static EmulatorControllerProxy proxy;

    /*
     * The object that manages the libretro lower level functions. Used mostly
     * for loading symbols and storing function pointers.
     */
    static RetroCore Core;

    /*
     * Kind of the constructor. Blocks when called.
     */
    static void Run(const std::string& corePath, const std::string& romPath,
                    LetsPlayServer* server, EmuID_t t_id);

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

    /*
     * Called when the emulator requests/announces a change in the pixel format
     */
    static bool setPixelFormat(const retro_pixel_format fmt);

    static void overlay(std::vector<std::vector<RGBColor>>& fg,
                        std::vector<std::vector<RGBColor>>& bg);
};
