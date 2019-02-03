/**
 * @file EmulatorController.h
 *
 * @author ctrlaltf2
 *
 * @section LICENSE
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  @section DESCRIPTION
 *  Class that serves as the connection from the LetsPlayServer to the
 *  RetroArch core.
 */

class EmulatorController;

struct EmulatorControllerProxy;
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
#include <memory>
#include <mutex>
#include <new>
#include <queue>
#include <set>
#include <shared_mutex>
#include <string>
#include <variant>

#include <websocketpp/frame.hpp>

#include "libretro.h"

#include "common/typedefs.h"
#include "LetsPlayProtocol.h"
#include "LetsPlayServer.h"
#include "LetsPlayUser.h"
#include "RetroCore.h"
#include "RetroPad.h"

/**
 * @struct EmulatorControllerProxy
 *
 * Serves as a 'proxy' for EmulatorController objects which are in their own thread.
 */
struct EmulatorControllerProxy {
    std::function<void(LetsPlayUserHdl)> addTurnRequest, userDisconnected, userConnected;
    std::function<Frame()> getFrame;
    bool isReady{false};
    RetroPad *joypad{nullptr};
};

/**
 * @struct VideoFormat
 *
 * Stores the information required to take a RetroArch video buffer and
 * translate it into a vector representing the RGB colors.
 */
struct VideoFormat {
    /*--- Bit masks ---*/

    /**
     * Red mask for the current video format
     */
    std::atomic<std::uint32_t> rMask{0b1111100000000000};

    /**
     * Green mask for the current video format
     */
    std::atomic<std::uint32_t> gMask{0b0000011111000000};

    /**
     * Blue mask for the current video format
     */
    std::atomic<std::uint32_t> bMask{0b0000000000111110};

    /**
     * Alpha mask for the current video format
     *
     * @note This is typically not used by the RetroArch cores
     */
    std::atomic<std::uint32_t> aMask{0b0};

    /*--- Bit shifts ---*/

    /**
     * Red bit shift
     */
    std::atomic<std::uint8_t> rShift{10};

    /**
     * Green bit shift
     */
    std::atomic<std::uint8_t> gShift{5};

    /**
     * Blue bit shift
     */
    std::atomic<std::uint8_t> bShift{0};

    /**
     * Alpha bit shift
     * @note This typically isn't used by RetroArch cores
     */
    std::atomic<std::uint8_t> aShift{15};

    /**
     * How many bits per pixel
     */
    std::atomic<std::uint8_t> bitsPerPel{16};

    /**
     * Width of the current video buffer
     */
    std::atomic<std::uint32_t> width{0};

    /**
     * Height of the current video buffer
     */
    std::atomic<std::uint32_t> height{0};

    /**
     * Pitch for the current video buffer
     */
    std::atomic<std::uint32_t> pitch{0};
};

/**
 * @struct Frame
 *
 * Represents a video frame form the RetroArch core.
 */
struct Frame {
    /**
     * Width of the frame in px
     */
    std::uint32_t width{0};

    /**
     * Height of the frame in px
     */
    std::uint32_t height{0};

    /**
     * Packed RGB array containing the data of the frame
     * @note Doesn't have a pitch
     */
    std::shared_ptr<std::uint8_t[]> data;
};

/**
 * @class EmulatorController
 *
 * Class to be used once per thread, manages a RetroArch emulator and its
 * own turns through the use of callbacks.
 *
 * @note The reason that the class is all static is because the callback functions
 * for RetroArch have to be plain old functions. This means there has to be some sort
 * of global piece of data keeping track of information for the core.
 *
 * @todo The one-thread-per-controller rule might be able to be worked around by
 * using a thread-safe class that stores information based on the thread ID. Server to
 * controller communication would need reworked.
 */
class EmulatorController {
    /**
     * Pointer to the server managing the emulator controller
     */
    static LetsPlayServer *m_server;

    /**
     * Turn queue for this emulator
     */
    static std::vector<LetsPlayUserHdl> m_TurnQueue;

    /**
     * Mutex for accessing the turn queue
     */
    static std::mutex m_TurnMutex;

    /**
     * Condition variable for waking up/sleeping the turn thread.
     */
    static std::condition_variable m_TurnNotifier;

    /**
     * Keep the turn thread running.
     */
    static std::atomic<bool> m_TurnThreadRunning;

    /**
     * Thread that runs EmulatorController::TurnThread.
     */
    static std::thread m_TurnThread;

    /**
     * Stores the masks and shifts required to generate a rgb 0xRRGGBB
     * vector from the video_refresh callback data.
     */
    static VideoFormat m_videoFormat;

    /**
     * Pointer to the current video buffer.
     */
    static const void *m_currentBuffer;

    /**
     * Mutex for accessing m_screen or m_nextFrame or updating the buffer.
     */
    static std::mutex m_videoMutex;

    /**
     * libretro API struct that stores audio-video information.
     */
    static retro_system_av_info m_avinfo;

    /**
     * General mutex for things that won't really go off at once and get blocked.
     */
    static std::shared_mutex m_generalMutex;

  public:
    /**
     * ID of the emulator controller / emulator.
     */
    static EmuID_t id;

    /**
     * Name of the library that is loaded (mGBA, Snes9x, bsnes, etc).
     *
     * @todo Grab the info from the loaded core and populate this field.
     */
    static std::string coreName;

    /**
     * Location of the save directory, loaded from config.
     */
    static std::string saveDirectory;

    /**
     * Location of the system directory, loaded from config.
     */
    static std::string systemDirectory;

    /**
     * Rom data if loaded from file.
     */
    static char *romData;

    /**
     * The joypad object storing the button state.
     */
    static RetroPad joypad;

    /**
     * Pointer to some functions that the managing server needs to call.
     */
    static EmulatorControllerProxy proxy;

    /**
     * The object that manages the libretro lower level functions. Used mostly
     * for loading symbols and storing function pointers.
     */
    static RetroCore Core;

    /**
     * Called as a constructor. Blocks when called and runs retro_run.
     *
     * @param corePath The file path to the libretro dynamic library
     * that is to be loaded.
     * @param romPath The file path to the rom that is to be loaded
     * by the emulator.
     * @param server Pointer to the server that manages this EmulatorController.
     * @param t_id The ID that is to be assigned to the EmulatorController instance.
     */
    static void Run(const std::string& corePath, const std::string& romPath, LetsPlayServer *server,
                    EmuID_t t_id);

    /**
     * Callback for when the libretro core sends extra info about the
     * environment.
     *
     * @param cmd RETRO_ENVIRONMENT command.
     * @param data Extra data that might be used by the RetroArch core.
     *
     * @return If the command was recognized.
     */
    static bool OnEnvironment(unsigned cmd, void *data);

    /**
     * Called by the RetroArch core when the video updates.
     *
     * @param data Data for the video frame.
     * @param width Width for the frame.
     * @param height Height for the frame.
     * @param stride In-memory stride for the frame.
     */
    static void OnVideoRefresh(const void *data, unsigned width, unsigned height, size_t stride);

    /**
     * Pretty much unused but called by the RetroArch core.
     */
    static void OnPollInput();

    /**
     *
     * @param port ?
     * @param device What kind of device it is (A RETRO_DEVICE macro).
     * @param index ?
     * @param id ?
     *
     * @return The value of the button being pressed. Without analog support
     * being added, this is just a 1 or 0 value.
     */
    static std::int16_t OnGetInputState(unsigned port, unsigned device, unsigned index,
                                        unsigned id);

    /**
     * Audio callback for RetroArch. Superseded by the batch.
     * audio callback.
     *
     * @note Currently unused.
     *
     * @param left Audio data for the left side.
     * @param right Audio data for the right side.
     */
    static void OnLRAudioSample(std::int16_t left, std::int16_t right);

    /**
     * Batch audio callback for RetroArch.
     *
     * @note Currently unused.
     *
     * @param data Batch audio data.
     * @param frames How many frames are in data.
     *
     * @return How many samples were used (?)
     */
    static size_t OnBatchAudioSample(const std::int16_t *data, size_t frames);

    /**
     * Thread that manages the turns for the users that are connected to the
     * emulator
     */
    static void TurnThread();

    /**
     * Adds a user to the turn request queue, invoked by parent LetsPlayServer
     *
     * @param user_hdl Who to add to the turn queue
     */
    static void AddTurnRequest(LetsPlayUserHdl user_hdl);

    /**
     * Send the turn list to the connected users
     */
    static void SendTurnList();

    /**
     * Called when a user disconnects.
     *
     * @param user_hdl Who disconnected
     */
    static void UserDisconnected(LetsPlayUserHdl user_hdl);

    /**
     * Called when a user connects.
     *
     * @param user_hdl Who connected
     */
    static void UserConnected(LetsPlayUserHdl user_hdl);

    /**
     * Called when the emulator requests/announces a change in the pixel format
     */
    static bool SetPixelFormat(const retro_pixel_format fmt);

    /**
     * Gets a frame based on the current video buffer.
     *
     * @return The frame representing the current video buffer.
     */
    static Frame GetFrame();
};
