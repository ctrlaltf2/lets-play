/**
 * @file LetsPlayServer.h
 *
 * @author ctrlaltf2
 *
 */
class LetsPlayServer;

#pragma once
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <iterator>
#include <memory>
#include <mutex>
#include <queue>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#define _WEBSOCKETPP_CPP11_THREAD_

#include <turbojpeg.h>

#include <websocketpp/common/connection_hdl.hpp>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include "nlohmann/json.hpp"

#include "md5.h"

#include "common/filesystem.h"
#include "common/typedefs.h"
#include "EmulatorController.h"
#include "LetsPlayConfig.h"
#include "LetsPlayProtocol.h"
#include "LetsPlayUser.h"
#include "Logging.hpp"
#include "Random.h"
#include "Scheduler.h"

typedef websocketpp::server<websocketpp::config::asio> wcpp_server;

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;

/**
 * @enum kCommandType
 *
 * Enum for message types. Includes internal messages.
 */
enum class kCommandType {
    /** A chat message */
        Chat,
    /** A username change request */
        Username,
    /** A list request */
        List,
    /** A button press message */
        Button,
    /** A turn request sent */
        Turn,
    /** A connect request sent */
        Connect,
    /** Response to a ping message */
        Pong,
    /** Admin upgrade login request */
        Admin,
    /** Add an emulator */
        AddEmu,
    /** Remove an emulator */
        RemoveEmu,
    /** Stop an emulator */
        StopEmu,
    /** Shutdown the whole server */
        Shutdown,
    /** Config change request */
        Config,
    /** Fast forward toggle */
            FastForward,
    /** Internal: Sends off previews to a user */
            Preview,
    Unknown,
};

/**
 * @enum kBinaryMessageType
 *
 * Enum for outgoing binary message types. Used clientside to differentiate binary payloads.
 */
enum kBinaryMessageType {
    /** Screen update message **/
            Screen,
    /** Emulator preview message **/
            Preview,
};

/**
 * @struct IPData
 *
 * POD class for keeping track of IP-specific data such as mutes and username ratelimits
 */
struct IPData {
    /**
     * Whether or not the IP is muted
     **/
    bool isMuted{false};

    /**
     * Timestamps for the last message
     **/
    std::vector<std::chrono::time_point<std::chrono::steady_clock>> messageTimestamps;

    /**
     * Timestamp for when the user's mute expires
     **/
    std::chrono::time_point<std::chrono::steady_clock> muteTime;

    /**
     * Timestamp for the last rename message
     */
    std::chrono::time_point<std::chrono::steady_clock> lastRename;
};

/**
 * @struct Command
 *
 * POD class for the action queue
 */
struct Command {
    /**
     * Enum representing the type of command it is (chat, rename, screen,
     * button, etc)
     */
    kCommandType type;

    /**
     * Parameters to that command (new name, buttons pressed, etc)
     */
    std::vector<std::string> params;

    /**
     * Handle to the connection of who generated the message
     */
    websocketpp::connection_hdl hdl;

    /**
     * ID of the emulator the user is connected to (if any) and sending the
     * message from
     */
    EmuID_t emuID;

    /**
     * Handle to the LetsPlayUser that generated the message
     */
    LetsPlayUserHdl user_hdl;
};

/**
 * @class LetsPlayServer
 *
 * Main class for the Let's Play server. Manages emulators and the websocket connection
 */
class LetsPlayServer {
    /**
     * Queue that holds the list of commands/actions to be executed
     */
    std::queue<Command> m_WorkQueue;

    /**
     * Mutex for accessing m_WorkQueue
     */
    std::mutex m_QueueMutex;

    /**
     * Thread that manages new commands/actions
     */
    std::thread m_QueueThread;

    /**
     * If true, the QueueThread will keep running
     */
    std::atomic<bool> m_QueueThreadRunning;

    /**
     * Condition variable that allows the queue to wait for new commands/actions
     */
    std::condition_variable m_QueueNotifier;

    /**
     * If true, the SaveThread will keep running
     */
    std::atomic<bool> m_SaveThreadRunning;

    /**
     * Thread that regularly saves emulators
     */
    std::thread m_SaveThread;

    /**
     * Map that maps connection_hdls to LetsPlayUsers
     */
    std::map<websocketpp::connection_hdl, std::shared_ptr<LetsPlayUser>, std::owner_less<websocketpp::connection_hdl>>
        m_Users;

    /**
     * Mutex for accessing m_Users
     */
    std::mutex m_UsersMutex;

    /**
     * All of the emulator controller threads
     */
    std::vector<std::thread> m_EmulatorThreads;

    /**
     * Mutex for accessing m_EmulatorThreads
     */
    std::mutex m_EmuThreadMutex;

    /**
     * Map that stores the id -> EmulatorController relation. Also how
     * EmulatorControllers are communicated with.
     */
    std::map<EmuID_t, EmulatorControllerProxy *> m_Emus;

    /**
     * Make m_emus thread-safe
     */
    std::mutex m_EmusMutex;

    /**
     * Object to store emulator previews
     */
    std::map<EmuID_t, std::vector<std::uint8_t>> m_Previews;

    /**
     * Mutex for accessing/modifying m_Previews
     */
    std::mutex m_PreviewsMutex;

    /**
     * IP -> IPData for mutes
     */
    std::map<std::string, IPData> m_Mutes;

    /**
     * Mutex for mutes
     */
     std::mutex m_MutesMutex;

public:
    LetsPlayConfig config;

    /**
     * Pointer to the websocket++ server
     */
    std::shared_ptr<wcpp_server> server;

    /**
     * Logger object
     *
     * @note thread-safe
     */
    Logger logger;

    /**
     * Scheduler for periodic tasks
     */
    Scheduler scheduler;

    /*
     * ---- Filesystem constants ----
     */

    /**
     * Data dir / emulators
     */
    lib::filesystem::path emuDirectory;

    /**
     * Data dir / system
     */
    lib::filesystem::path systemDirectory;

    /**
     * Data dir / roms
     */
    lib::filesystem::path romDirectory;

    /**
     * Data dir / cores
     */
    lib::filesystem::path coreDirectory;


    /**
     * Constructor
     * @param configFile Path to the config.json file (defaults to
     * ~/.config/letsplay/config)
     */
    explicit LetsPlayServer(lib::filesystem::path& configFile);

    /**
     * Blocking function that starts the LetsPlayServer on the given port
     * @param port The port to attempt to start the server on
     */
    void Run(const std::uint16_t port);

    /**
     * Callback for validates
     */

    bool OnValidate(websocketpp::connection_hdl hdl);

    /**
     * Callback for new connections
     * @param hdl Who connected
     */
    void OnConnect(websocketpp::connection_hdl hdl);

    /**
     * Callback for disconnects
     * @param hdl Who disconnected
     */
    void OnDisconnect(websocketpp::connection_hdl hdl);

    /**
     * Callback for HTTP requests
     * @param hdl handle to the HTTP connection
     */
    void OnHTTP(websocketpp::connection_hdl hdl);

    /**
     * Callback for new messages
     * @param hdl Who sent the message
     * @param msg The message sent
     */
    void OnMessage(websocketpp::connection_hdl hdl, wcpp_server::message_ptr msg);

    /**
     * Stops the main loop, closes all connections, and unbinds to the port.
     */
    // FIXME: LetsPlayServer::Run still blocks after Shutdown even though
    // everything shuts down right. Asio still running?
    void Shutdown();

    /**
     * Thread function that manages the queue and all of the incoming commands
     */
    void QueueThread();

    /**
     * Task function that manages the ping sends and disconnects for users not responding with a pong
     */
    void PingTask();

    /**
     * Task function that periodically generates preview thumbnails for the join view of the client.
     */
    void PreviewTask();

    /**
     * Send a message to all connected users
     * @param message The message to send (isn't modified or encoded on the way out)
     * @param op The type of message to send
     */
    void BroadcastAll(const std::string& message, websocketpp::frame::opcode::value op);

    /**
     * Send a message to all users connected to an emu
     * @param emu ID of the emulator to broadcast to
     * @param message The message to send
     * @param op The type of frame to send
     */
    void BroadcastToEmu(const EmuID_t& emu, const std::string& message,
                        websocketpp::frame::opcode::value op);

    /**
     * Send a message to just one user
     * @param message The message to send (isn't modified or encoded on the way out)
     * @param hdl Who to send it to
     */
    void BroadcastOne(const std::string&& message, websocketpp::connection_hdl hdl);

    /**
     * Generate and send a guest username for a user. Doesn't tell everyone on emu about join/username change.
     * @param hdl Websocket++ handle for sending
     * @param uuid User to give guest
     */
    // TODO: uuid -> hdl?
    void GiveGuest(websocketpp::connection_hdl hdl, LetsPlayUserHdl user);

    /**
     * Check if a username is taken.
     * @param username The username to check
     * @param uuid the UUID of the user being checked
     *
     * @return Whether or not the username is taken
     */
    bool UsernameTaken(const std::string& username, const std::string& uuid);

    /**
     * Sets up directories listed in the config file so that they exist and can be written to.
     *
     * @note Called only on server run
     */
    void SetupLetsPlayDirectories();

    /**
     * Periodic save task that pushes a save command on all emulators
     */
    void SaveTask();

    /**
     * Periodic backup task that pushes a backup command on all emulators
     */
    void BackupTask();


    // --- Functions called only by emulator controllers --- //
    /**
     * Called when an emulator controller is spawned, updates m_Emus
     * @param id The id of the emulator to add
     * @param emu Pointer to the POD struct containing the information necessary
     * for interacting with the newly added emulator controller
     *
     * @note Only called by EmulatorControllers
     */
    void AddEmu(const EmuID_t& id, EmulatorControllerProxy *emu);

    /**
     * Called when an emulator controller has a frame update
     * @param id The id of the caller
     *
     * @note Only called by EmulatorControllers
     */
    void SendFrame(const EmuID_t& id);

    /**
     * Generate preview thumbnails
     */
    void GeneratePreview(const EmuID_t &id);

    /**
     * Generates a jpeg from the display currently on an emulator
     */
    std::vector<std::uint8_t> GenerateEmuJPEG(const EmuID_t &id);

    /**
     * Replaces ~ in file paths with the path to the current user's home directory.
     * @param str
     *
     * @return The full path without the ~
     */
    static std::string escapeTilde(std::string str);

  private:
    /**
     * Helper function that checks if all characters in the string are ones
     * that can be typed on a common 101 to 104 key keyboard
     * @param str The string to validate
     *
     * @return Whether or not the string is valid
     */
    static bool isAsciiStr(const std::string& str);

    /**
     * Helper function that gives the size of the input string if all of the
     * unicode and hex escaped substrings (\uXXXX, \u1{XXXX}, \xXX) were
     * replaced with their respective character.
     * @param str The string to count
     *
     * @return The size of the string if all things were escaped
     */
    static size_t escapedSize(const std::string& str);

    /**
     * Helper function that sends a file over HTTP
     * @param con Who to send to
     * @param file_path Path to the file to send
     */
    static void sendHTTPFile(wcpp_server::connection_ptr& cptr, lib::filesystem::path file_path, websocketpp::http::status_code::value);
};
