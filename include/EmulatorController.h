/**
 * @file EmulatorController.h
 *
 * @author ctrlaltf2
 *
 *  @section DESCRIPTION
 *  'Class' that serves as the connection from the LetsPlayServer to the
 *  RetroArch core.
 */

struct EmulatorControllerProxy;
struct EmuCommand;
struct VideoFormat;
struct Frame;
#pragma once
#include <algorithm>
#include <bitset>
#include <condition_variable>
#include <cstdint>
#include <ctime>
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

#include <websocketpp/frame.hpp>

#include <boost/filesystem.hpp>
#include <boost/optional.hpp>

#include <tmmintrin.h>
#include <emmintrin.h>
#include <mmintrin.h>
#include <smmintrin.h>

#include "libretro.h"

#include "common/typedefs.h"

#include "LetsPlayProtocol.h"
#include "LetsPlayServer.h"
#include "LetsPlayUser.h"
#include "RetroCore.h"
#include "RetroPad.h"
#include "Scheduler.h"



/**
 * @enum kEmuCommandType
 *
 * Enum for the internal work queue commands
 */
enum class kEmuCommandType {
    /** Save command, updates history folder **/
            Save,
    /** Backup command, updates permanent backups **/
            Backup,
    /** Generate preview command, generates a thumbnail and gives it to the server **/
            GeneratePreview,
    /** Turn request **/
            TurnRequest,
    /** User disconnect **/
            UserDisconnect,
    /** User connect **/
            UserConnect,
    /** Fast forward request **/
            FastForward,
};


/**
 * @struct EmuCommand
 *
 * POD struct for the work queue. Stores relevant information for a specific command.
 */
struct EmuCommand {
    /**
     * The type of command
     */
    kEmuCommandType command;

    /**
     *  Who, if anyone, generated the command
     */
    boost::optional<LetsPlayUserHdl> user_hdl;
};

/**
 * @struct EmulatorControllerProxy
 *
 * Serves as a 'proxy' for EmulatorController objects which are in their own thread.
 */
struct EmulatorControllerProxy {
    /**
     * Pointer to the work queue
     */
    std::queue<EmuCommand> *queue;

    /**
     * Mutex for the work queue
     */
    std::mutex *queueMutex;

    /**
     * CV for notifying the queue
     */
    std::condition_variable *queueNotifier;

    /**
     * Callback for getFrame, used in LetsPlayServer::GenerateEmuJPEG and similar when called by emulator
     */
    std::function<Frame()> getFrame;

    /**
     * Pointer to the joypad object
     */
    RetroPad /*-*/*joypad{nullptr};

    /**
     * Emulator description
     */
    std::string /*-*/description;

    /**
     * Pointer to the forbidden combos list
     */
    std::vector<std::bitset<16>>* forbiddenCombos;
};

/**
 * @struct VideoFormat
 *
 * Stores the information required to take a RetroArch video buffer and
 * translate it into a vector representing the RGB colors.
 */
struct VideoFormat {
    /*--- Bit masks ---*/

    /* -- 0RGB1555 by default -- */

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
    std::atomic<std::uint32_t> aMask{0b0000000000111110};

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

    /**
     * RetroArch format
     */
    retro_pixel_format fmt{RETRO_PIXEL_FORMAT_0RGB1555};

    /**
     * Stride for the video format
     */
    std::atomic<std::uint32_t> stride{0};

    /**
     * Buffer for the video data output
     */
    std::vector<std::uint8_t> buffer;
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
     * Stride of the frame in px
     */
     std::uint32_t pitch{0};

    /**
     * Packed RGB array containing the data of the frame
     */
    const std::uint8_t* data{nullptr};
};

/**
 * @union SSE128i
 *
 * Union to allow simpler access to m128i items
 */
union SSE128i {
    /**
     * Underlying vector represented by this data
     */
    __m128i vec128i;

    /**
     * Thing you'd use to access it as if it were a bunch of 16 bit uints
     */
    std::uint16_t data16[8];

    /**
     * Similar to data16
     */
    std::uint8_t data8[16];
};

/**
 * @namespace EmulatorController
 *
 * Namespace with TLS, manages a RetroArch emulator and its
 * own turns through the use of callbacks.
 *
 * @note The reason that the namespace is all static thread_local is because the callback functions
 * for RetroArch have to be plain old functions. This means there has to be some sort
 * of global piece of data keeping track of information for the core.
 */
namespace EmulatorController {
    /**
     * Called as a constructor. Blocks when called and runs retro_run.
     *
     * @param corePath The file path to the libretro dynamic library
     * that is to be loaded.
     * @param romPath The file path to the rom that is to be loaded
     * by the emulator.
     * @param server Pointer to the server that manages this EmulatorController.
     * @param t_id The ID that is to be assigned to the EmulatorController instance.
     * @param description The description of the emulator. Used as the emulator title in the join view.
     */
    void Run(const std::string &corePath, const std::string &romPath, LetsPlayServer *server,
             EmuID_t t_id, const std::string &description);

    /**
     * Callback for when the libretro core sends extra info about the
     * environment.
     *
     * @param cmd RETRO_ENVIRONMENT command.
     * @param data Extra data that might be used by the RetroArch core.
     *
     * @return If the command was recognized.
     */
    bool OnEnvironment(unsigned cmd, void *data);

    /**
     * Called by the RetroArch core when the video updates.
     *
     * @param data Data for the video frame.
     * @param width Width for the frame.
     * @param height Height for the frame.
     * @param stride In-memory stride for the frame.
     */
    void OnVideoRefresh(const void *data, unsigned width, unsigned height, size_t stride);

    /**
     * Pretty much unused but called by the RetroArch core.
     */
    void OnPollInput();

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
    std::int16_t OnGetInputState(unsigned port, unsigned device, unsigned index,
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
    void OnLRAudioSample(std::int16_t left, std::int16_t right);

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
    size_t OnBatchAudioSample(const std::int16_t *data, size_t frames);

    /**
     * Adds a user to the turn request queue, invoked by parent LetsPlayServer
     *
     * @param user_hdl Who to add to the turn queue
     */
    void AddTurnRequest(LetsPlayUserHdl user_hdl);

    /**
     * Send the turn list to the connected users
     */
    void SendTurnList();

    /**
     * Called when a user disconnects.
     *
     * @param user_hdl Who disconnected
     */
    void UserDisconnected(LetsPlayUserHdl user_hdl);

    /**
     * Called when a user connects.
     *
     * @param user_hdl Who connected
     */
    void UserConnected(LetsPlayUserHdl user_hdl);

    /**
     * Called when the emulator requests/announces a change in the pixel format
     */
    bool SetPixelFormat(const retro_pixel_format fmt);

    /**
     * Gets a frame based on the current video buffer.
     *
     * @return The frame representing the current video buffer.
     */
    Frame GetFrame();

    /**
     * Called by the server periodically to add to the emulator history
     */
    void Save();

    /**
     * Called by the server periodically to create a backup of saves and a single history state
     */
    void Backup();

    /**
     * Called by the server. Toggles fast forward state.
     */
    void FastForward();

    /**
     * Called on emulator controller startup, tries to load save state if possible
     */
    void Load();
}
