class EmulatorController;

struct EmulatorControllerProxy;
struct RGBColor;
struct VideoFormat;
struct Frame;
#pragma once
#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <mutex>
#include <new>
#include <queue>
#include <set>
#include <shared_mutex>
#include <string>
#include <variant>

#include <websocketpp/frame.hpp>

#include "libretro.h"

#include "LetsPlayServer.h"
#include "LetsPlayUser.h"
#include "RetroCore.h"
#include "RetroPad.h"

// Because you can't pass a pointer to a static instance of a class...
struct EmulatorControllerProxy {
    std::function<void(LetsPlayUser *)> addTurnRequest, userDisconnected, userConnected;
    std::function<Frame()> getFrame;
    bool isReady{false};
    RetroPad *joypad{nullptr};
};

struct VideoFormat {
    /*
     * Masks for red, green, blue, and alpha
     */
    std::atomic<std::uint32_t> rMask{0b1111100000000000}, gMask{0b0000011111000000},
        bMask{0b0000000000111110}, aMask{0b0};

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
    std::atomic<std::uint32_t> width{0}, height{0}, pitch{0};
};

struct RGBColor {
    std::uint8_t r{0}, g{0}, b{0};
};

inline bool operator<(const RGBColor& a, const RGBColor& b) {
    return (a.r | (a.g << 8) | (a.b << 16)) < (b.r | (b.g << 8) | (b.b << 16));
}

struct Frame {
    /*
     * Width/height of the frame (px)
     */
    std::uint32_t width{0}, height{0};

    // Packed array, RGB
    std::shared_ptr<std::uint8_t[]> data;
};

/*
 * Class to be used once per thread, manages a libretro core, emulator, and its
 * own turns through callbacks
 */
class EmulatorController {
    /*
     * Pointer to the server managing the emulator controller
     */
    static LetsPlayServer *m_server;

    /*
     * Turn queue for this emulator
     */
    static std::vector<LetsPlayUser *> m_TurnQueue;

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
     * Stores the masks and shifts required to generate a rgb 0xRRGGBB vector
     * from the video_refresh callback data
     */
    static VideoFormat m_videoFormat;

    /*
     * Key frame, a frame similar to a video compression key frame that contains
     * all the information for the last used frame. This is only updated when a
     * keyframe or deltaframe are requested.
     */
    static Frame m_keyFrame;

    /*
     * Pointer to the current video buffer
     */
    static const void *m_currentBuffer;

    /*
     * Mutex for accessing m_screen or m_nextFrame or updating the buffer
     */
    static std::mutex m_videoMutex;

    static retro_system_av_info m_avinfo;

    /*
     * General mutex for things that won't really go off at once and get blocked
     */
    static std::shared_mutex m_generalMutex;

  public:
    /*
     * ID of the emulator controller / emulator
     */
    static EmuID_t id;

    /*
     * Name of the library that is loaded (mGBA, Snes9x, bsnes, etc)
     */
    static std::string coreName;

    /*
     * Location of the save directory, loaded from config
     */
    static std::string saveDirectory;

    /*
     * Location of the system directory, loaded from config
     */
    static std::string systemDirectory;

    /*
     * Rom data if loaded from file
     */
    static char *romData;

    /*
     * The joypad object storing the button state
     */
    static RetroPad joypad;

    /*
     * Pointer to some functions that the managing server needs to call
     */
    static EmulatorControllerProxy proxy;

    /*
     * The object that manages the libretro lower level functions. Used mostly
     * for loading symbols and storing function pointers.
     */
    static RetroCore Core;

    static std::atomic<std::uint64_t> usersConnected;

    /*
     * Kind of the constructor. Blocks when called.
     */
    static void Run(const std::string& corePath, const std::string& romPath, LetsPlayServer *server,
                    EmuID_t t_id);

    // libretro_core -> Controller ?> Server
    /*
     * Callback for when the libretro core sends eztra info about the
     * environment
     * @return (?) Possibly if the command was recognized
     */
    static bool OnEnvironment(unsigned cmd, void *data);
    // Either:
    //  1) libretro_core -> Controller
    static void OnVideoRefresh(const void *data, unsigned width, unsigned height, size_t stride);
    // Controller -> Server.getInput (input is TOGGLE)
    static void OnPollInput();
    // Controller -> libretro_core
    static std::int16_t OnGetInputState(unsigned port, unsigned device, unsigned index,
                                        unsigned id);
    // Controller -> Server
    static void OnLRAudioSample(std::int16_t left, std::int16_t right);
    // Controller -> Server
    static size_t OnBatchAudioSample(const std::int16_t *data, size_t frames);

    /*
     * Thread that manages the turns for the users that are connected to the
     * emulator
     */
    static void TurnThread();

    /*
     * Adds a user to the turn request queue, invoked by parent LetsPlayServer
     */
    static void AddTurnRequest(LetsPlayUser *user);

    /*
     * Called when a user disonnects, updates turn queue if applicable
     */
    static void UserDisconnected(LetsPlayUser *user);

    /*
     * Called when a user connects
     */
    static void UserConnected(LetsPlayUser *user);

    /*
     * Called when the emulator requests/announces a change in the pixel format
     */
    static bool SetPixelFormat(const retro_pixel_format fmt);

    /*
     * Gets a key frame based on the current video buffer, updates m_keyFrame
     */
    static Frame GetFrame();
};
