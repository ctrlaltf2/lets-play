class LetsPlayServer;
#pragma once
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#define _WEBSOCKETPP_CPP11_THREAD_

#include <websocketpp/common/connection_hdl.hpp>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include "Config.h"
#include "EmulatorController.h"
#include "LetsPlayUser.h"

typedef websocketpp::server<websocketpp::config::asio> wcpp_server;

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;

enum class kCommandType {
    // External -- only generated from clients sending messages
    Chat,
    Username,
    List,
    Button,
    Turn,
    Connect,
    // External -- requires admin
    AddEmu,
    RemoveEmu,
    StopEmu,
    Shutdown,
    // Internal -- only generated inside program
    Unknown,
};

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
     * (User provided) ID of the emulator the user is connected to and
     * (apparently) sending the message from
     */
    EmuID_t emuID;
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
    std::map<EmuID_t, EmulatorControllerProxy*> m_Emus;

    std::mutex m_EmusMutex;

   public:
    /*
     * Pointer to the websocketpp server
     */
    std::shared_ptr<wcpp_server> server;

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
    void OnMessage(websocketpp::connection_hdl hdl,
                   wcpp_server::message_ptr msg);

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
     * @param The message to send (isn't modified or encoded on the way out)
     */
    void BroadcastAll(const std::string& message);

    /*
     * Send a message to just one user
     * @param The message to send (isn't modified or encoded on the way out)
     */
    void BroadcastOne(const std::string& message,
                      websocketpp::connection_hdl hdl);

    /*
     * Callback when an emulator controller is spawned, updates m_Emus
     */
    void addEmu(const EmuID_t& id, EmulatorControllerProxy* emu);

   private:
    /*
     * Helper function for encoding messages
     * @param input The list of strings to encode
     */
    static std::string encode(const std::vector<std::string>& input);

    /*
     * Helper function for decoding messages
     * @param input The encoded string to decode into multiple strings
     */
    static std::vector<std::string> decode(const std::string& input);

    /*
     * Helper function that checks if all characters in the string are ones that
     * can be typed on a standard keyboard
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
