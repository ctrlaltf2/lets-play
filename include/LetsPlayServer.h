class LetsPlayServer;

#pragma once
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
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

#include "EmulatorController.h"
#include "LetsPlayConfig.h"
#include "LetsPlayUser.h"
#include "Random.h"

typedef websocketpp::server<websocketpp::config::asio> wcpp_server;

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;

enum class kCommandType {
    Chat,
    Username,
    List,
    Button,
    Turn,
    Connect,
    Error,
    Admin,
    AddEmu,
    RemoveEmu,
    StopEmu,
    Shutdown,
    Config,
    Unknown,
};

namespace error {
enum {connectInvalidEmu };
}  // namespace error

/*
 * POD class for the action queue
 */
struct Command {
    /*
     * Enum representing the type of commamd it is (chat, rename, screen,
     * button, etc)
     */
    kCommandType type;

    /*
     * Parameters to tbat command (new name, buttons pressed, etc)
     */
    std::vector<std::string> params;

    /*
     * Handle to the connection / who sent it
     */
    websocketpp::connection_hdl hdl;

    /*
     * ID of the emulator the user is connected to (if any) and sending the
     * message from
     */
    EmuID_t emuID;

    /*
     * Pointer to the LetsPlayUser object inside of the hdl->user relation map
     * XXX: Possible data race for when the map internally reallocates, but
     * hasn't happened in tests
     */
    LetsPlayUser *user{nullptr};
};

class LetsPlayServer {
    /*
     * Queue that holds the list of commands/actions to be executed
     */
    std::queue<Command> m_WorkQueue;
    /*
     * Map that maps connection_hdls to LetsPlayUsers
     */
    std::map<websocketpp::connection_hdl, LetsPlayUser,
             std::owner_less<websocketpp::connection_hdl>>
        m_Users;

    /*
     * Mutex for accessing m_Users
     */
    std::mutex m_UsersMutex;

    /*
     * Thread that manages new commands/actions
     */
    std::thread m_QueueThread;

    /*
     * Thread with all of the emulator controllers
     */
    std::vector<std::thread> m_EmulatorThreads;

    /*
     * Mutex for accessing m_EmulatorThreads
     */
    std::mutex m_EmuThreadMutex;

    /*
     * If true, the m_QueueThread member's thread will keep running
     */
    std::atomic<bool> m_QueueThreadRunning;

    /*
     * Mutex for accessing m_WorkQueue
     */
    std::mutex m_QueueMutex;

    /*
     * Condition variable that allows the queue to wait for new commands/actions
     */
    std::condition_variable m_QueueNotifier;

    /*
     * Map that stores the id -> emulatorcontroller relation, also how
     * emulatorcontrollers are communicated with
     */
    std::map<EmuID_t, EmulatorControllerProxy *> m_Emus;

    /*
     * Make m_emus threadsafe
     */
    std::mutex m_EmusMutex;

  public:
    /*
     * Config object
     */
    LetsPlayConfig config;

    /*
     * Pointer to the websocketpp server
     */
    std::shared_ptr<wcpp_server> server;

    /*
     * Constructor
     * @param configFile Path to the config.json file (defaults to
     * ~/.config/letsplay/config)
     */
    explicit LetsPlayServer(std::filesystem::path& configFile);

    /*
     * Blocking function that starts the LetsPlayServer on the given port
     * @param port The port to attempt to start the server on
     */
    void Run(const std::uint16_t port);

    /*
     * Callback for new connections
     * @param hdl Who connected
     */
    void OnConnect(websocketpp::connection_hdl hdl);

    /*
     * Callback for disconnections
     * @param hdl Who disconnected
     */
    void OnDisconnect(websocketpp::connection_hdl hdl);

    /*
     * Callback for new messages
     * @param hdl Who sent the message
     * @param msg The message sent
     */
    void OnMessage(websocketpp::connection_hdl hdl, wcpp_server::message_ptr msg);

    /*
     * Stops the main loop, closes all connections, and unbinds to the port
     */
    // FIXME: LetsPlayServer::Run still blocks after Shutdown even though
    // everything shuts down right. Asio still running?
    void Shutdown();

    /*
     * Function (to be run in a thread) that manages the queue and all of the
     * incoming commands
     */
    void QueueThread();

    /*
     * Send a message to all connected users
     * @param message The message to send (isn't modified or encoded on the way
     * out)
     * @param op The type of message to send
     */
    void BroadcastAll(const std::string& message, websocketpp::frame::opcode::value op);

    /*
     * Send a message to all users connected to an emu
     * @param emu ID of the emulator to broadcast to
     * @param message The message to send
     * @param op The type of frame to send
     */
    void BroadcastToEmu(const EmuID_t& emu, const std::string& message,
                        websocketpp::frame::opcode::value op);

    /*
     * Send a message to just one user
     * @param message The message to send (isn't modified or encoded on the way
     * out)
     * @param hdl Who to send it to
     */
    void BroadcastOne(const std::string&& message, websocketpp::connection_hdl hdl);

    /*
     * Generate and send a guest username for a user. Doesn't tell everyone on emu about join/username change.
     * @param hdl Websocket handle for sending
     * @param uuid User to give guest
     */
    // TODO: uuid -> hdl?
    void GiveGuest(websocketpp::connection_hdl hdl, LetsPlayUser* user);

    /*
     * Check if a username is taken.
     * @param username The username to check
     * @param uuid the UUID of the user being checked
     * @return (bool) Whether or not the username is taken
     */
    bool UsernameTaken(const std::string& username, const std::string& uuid);

    // --- Functions called only by emulator controllers --- //
    /*
     * Called when an emulator controller is spawned, updates m_Emus
     * @param id The id of the emulator to add
     * @param emu Pointer to the POD struct containing the information necessary
     * for interacting with the newly added emulator controller
     */
    void AddEmu(const EmuID_t& id, EmulatorControllerProxy *emu);

    /*
     * Called when an emulator controller has a frame update
     * @param id The id of the caller
     */
    void SendFrame(const EmuID_t& id);

    static std::string escapeTilde(std::string str);

  private:
    /*
     * Vector function for encoding messages
     */
    static std::string encode(const std::vector<std::string>& chunks);

    /*
     * Variadic function for encoding messages
     */
    template<typename Head, typename... Tail>
    static std::string encode(Head h, Tail... t) {
        std::string encoded;
        encodeImpl(encoded, h, t...);
        encoded.back() = ';';
        return encoded;
    }

    /*
     * Variadic implementation of the encoding function
     */

    // Base
    template<typename T>
    static void encodeImpl(std::string& encoded, const T& item) {
        // Convert item to a string
        std::ostringstream toString;
        toString << item;
        const std::string itemAsString = toString.str();

        // Append the item string size to the output
        encoded += std::to_string(itemAsString.size());
        encoded += '.';
        // Append the item to the output
        encoded += itemAsString;
        encoded += ',';
    }

    // Recursive impl
    template<typename Head, typename... Tail>
    static void encodeImpl(std::string& encoded, Head h, Tail... t) {
        encodeImpl(encoded, h);
        encodeImpl(encoded, t...);
    }

    /*
     * Helper function for decoding messages
     * @param input The encoded string to decode into multiple strings
     */
    static std::vector<std::string> decode(const std::string& input);

    /*
     * Helper function that checks if all characters in the string are ones
     * that can be typed on a common 101 to 104 key keyboard
     * @param str The string to validate
     */
    static bool isAsciiStr(const std::string& str);

    /*
     * Helper function that gives the size of the input string if all of the
     * unicode and hex escaped substrings (\uXXXX, \u1{XXXX}, \xXX) were
     * replaced with their respective character.
     * @param str The string to count
     */
    static size_t escapedSize(const std::string& str);
};
