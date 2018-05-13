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
#define ASIO_STANDALONE

#include <websocketpp/common/connection_hdl.hpp>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

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
};

class LetsPlayServer {
    std::queue<Command> m_WorkQueue;

    std::map<websocketpp::connection_hdl, std::string,
             std::owner_less<websocketpp::connection_hdl>>
        m_Users;

    std::mutex m_UsersMutex;

    std::thread m_QueueThread;

    std::atomic<bool> m_QueueThreadRunning;

    std::mutex m_QueueMutex;

    std::condition_variable m_QueueNotifier;

   public:
    std::shared_ptr<wcpp_server> server;

    void Run(const std::uint16_t port);

    void OnConnect(websocketpp::connection_hdl hdl);

    void OnDisconnect(websocketpp::connection_hdl hdl);

    void OnMessage(websocketpp::connection_hdl hdl,
                   wcpp_server::message_ptr msg);

    void Shutdown();

    void QueueThread();

    void BroadcastAll(const std::string& message);

    void BroadcastOne(const std::string& message,
                      websocketpp::connection_hdl hdl);

   private:
    static std::string encode(const std::vector<std::string>& input);

    static std::vector<std::string> decode(const std::string& input);

    static bool isAsciiStr(const std::string& str);

    static size_t escapedSize(const std::string& str);
};
