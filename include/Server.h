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

#include "Scheduler.h"

constexpr int c_syncInterval{5 /* seconds */};
constexpr unsigned c_maxMsgSize{100 /* characters */};
constexpr unsigned c_maxUserName{15 /* characters */};
constexpr unsigned c_minUserName{3 /* characters */};

typedef websocketpp::server<websocketpp::config::asio> wcpp_server;

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;

enum class kCommandType {
    Chat,
    Username,
    List,
    Button,
    Turn,
    Shutdown,
    Unknown,
};

struct Command {
    kCommandType type;
    std::vector<std::string> params;
    websocketpp::connection_hdl hdl;
};

struct LetsPlayUser {
    websocketpp::connection_hdl hdl;
    std::string username;
    bool hasTurn;
    std::chrono::time_point<std::chrono::steady_clock> lastHeartbeat;
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
     * Thread that manages turns
     */
    std::thread m_TurnThread;

    /*
     * If true, the m_QueueThread member's thread will keep running
     */
    std::atomic<bool> m_QueueThreadRunning;

    /*
     * If true, the m_TurnThread will keep running
     */
    std::atomic<bool> m_TurnThreadRunning;

    /*
     * Mutex for accessing m_WorkQueue
     */
    std::mutex m_QueueMutex;

    /*
     * Condition variable that allows the queue to wait for new commands/actions
     */
    std::condition_variable m_QueueNotifier;

    /*
     * Scheduler to manage periodic tasks
     */
    // Scheduler scheduler;

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
     * Function (to be run in a thread) that manages the turns
     */
    void TurnThread();

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
