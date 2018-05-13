#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "Util.h"

#define _WEBSOCKETPP_CPP11_THREAD_
#define ASIO_STANDALONE

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

typedef websocketpp::server<websocketpp::config::asio> wcpp_server;

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;

class Server {
    // Work queue
    std::queue<Command> m_WorkQueue;

    // Keep track of the handles of connected clients
    std::vector<wcpp_server::connection_ptr> m_Connections;

    // Thread to process the command queue
    std::thread m_WorkThread;
    // Gracefully stop the command queue thread's loop
    std::atomic<bool> m_WorkThreadRunning{false};
    // Mutex for modifying the command queue (as well as connections list)
    std::mutex m_QueueMutex;
    // Mutex for modifying the connection list
    std::mutex m_ConnectionMutex;
    // To sleep/wakeup command queue processing thread
    std::condition_variable m_QueueCV;

   public:
    std::shared_ptr<wcpp_server> server;

    void Run(std::uint16_t port);

    void OnConnect(websocketpp::connection_hdl hdl);

    void OnDisconnect(websocketpp::connection_hdl hdl);

    void OnMessage(websocketpp::connection_hdl hdl,
                   wcpp_server::message_ptr msg);

    void Shutdown();

    void QueueThread();
};
