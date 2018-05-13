#include "Server.h"

void LetsPlayServer::Run(std::uint16_t port) {
    if (port == 0) return;

    server.reset(new wcpp_server);

    try {
        server->set_access_channels(websocketpp::log::alevel::all);
        server->clear_access_channels(websocketpp::log::alevel::frame_payload);

        server->init_asio();

        server->set_message_handler(
            std::bind(&LetsPlayServer::OnMessage, this, ::_1, ::_2));

        server->set_open_handler(
            std::bind(&LetsPlayServer::OnConnect, this, ::_1));

        server->set_close_handler(
            std::bind(&LetsPlayServer::OnDisconnect, this, ::_1));

        websocketpp::lib::error_code err;
        server->listen(port, err);

        if (err)
            throw std::runtime_error(std::string("Failed to listen on port ") +
                                     std::to_string(port));

        m_QueueThreadRunning = true;

        m_QueueThread = std::thread{[&]() { this->QueueThread(); }};

        server->start_accept();
        server->run();

    } catch (websocketpp::exception const& e) {
        std::cerr << e.what() << '\n';
    } catch (...) {
        throw;
    }
}

void LetsPlayServer::OnConnect(websocketpp::connection_hdl hdl) {
    websocketpp::lib::error_code err;
    {
        std::unique_lock<std::mutex> lk((m_ConnectionsMutex));
        m_Connections.push_back(server->get_con_from_hdl(hdl, err));
    }
}

void LetsPlayServer::OnDisconnect(websocketpp::connection_hdl hdl) {
    websocketpp::lib::error_code err;
    wcpp_server::connection_ptr cptr = server->get_con_from_hdl(hdl, err);

    {
        std::unique_lock<std::mutex> lk((m_ConnectionsMutex));
        for (std::size_t i = 0; i < m_Connections.size(); ++i)
            if (m_Connections[i].get() == cptr.get())
                m_Connections.erase(m_Connections.begin() + i);
    }
}

void LetsPlayServer::OnMessage(websocketpp::connection_hdl hdl,
                               wcpp_server::message_ptr msg) {
    std::clog << "OnMessage()" << '\n';
    const std::string& data = msg->get_payload();
    const auto decoded = decode(data);

    if (decoded.empty()) return;

    // TODO: that switch case hash thing, its faster
    const auto& command = decoded.at(0);
    kCommandType t;

    if (command == "chat")  // user, message
        t = kCommandType::Chat;
    else if (command == "button")  // mask
        t = kCommandType::Button;
    //    else if (command == "screen")
    //        t = kCommandType::Screen;
    //    else if (command == "sync")
    //        t = kCommandType::Sync;
    else if (command == "turn")  // user
        t = kCommandType::Turn;
    else if (command == "shutdown")
        this->Shutdown();
    else
        return;

    std::vector<std::string> params(decoded.begin() + 1, decoded.end());

    {
        std::unique_lock<std::mutex> lk((m_QueueMutex));
        m_WorkQueue.push(Command{t, params, hdl});
    }

    m_QueueNotifier.notify_one();
}

// TODO: Fix the crash when this is run (should gracefully shutdown, not
// shutdown and break the whole program)
void LetsPlayServer::Shutdown() {
    std::clog << "Shutdown()" << '\n';
    // Run this function once
    static bool shuttingdown = false;

    if (!shuttingdown)
        shuttingdown = true;
    else
        return;

    // Stop the work thread loop
    m_QueueThreadRunning = false;
    std::clog << "Stopping work thread..." << '\n';
    {
        std::clog << "Emptying queue..." << '\n';
        // Empty the queue ...
        std::unique_lock<std::mutex> lk((m_QueueMutex));
        while (m_WorkQueue.size()) m_WorkQueue.pop();
        // ... Except for a shutdown command
        m_WorkQueue.push(Command{kCommandType::Shutdown,
                                 std::vector<std::string>(),
                                 websocketpp::connection_hdl()});
    }
    std::clog << "Stopping listen..." << '\n';
    // Stop listening so the queue doesn't grow any more
    websocketpp::lib::error_code err;
    server->stop_listening(err);
    if (err) std::cout << "Error stopping listen " << err.message() << '\n';
    // Wake up the work thread
    std::clog << "Waking up work thread..." << '\n';
    m_QueueNotifier.notify_one();
    // Wait until it stops looping
    std::clog << "Waiting for work thread to stop..." << '\n';
    m_QueueThread.join();

    // Close every connection
    {
        std::clog << "Closing every connection..." << '\n';
        std::unique_lock<std::mutex> lk((m_ConnectionsMutex));
        std::for_each(m_Connections.begin(), m_Connections.end(),
                      [&](auto& hdl) {
                          server->close(hdl, websocketpp::close::status::normal,
                                        "Closing", err);
                      });
    }
}

void LetsPlayServer::QueueThread() {
    while (m_QueueThreadRunning) {
        {
            std::unique_lock<std::mutex> lk((m_QueueMutex));
            // Use std::condition_variable::wait Predicate?
            while (m_WorkQueue.empty()) m_QueueNotifier.wait(lk);

            if (!m_WorkQueue.empty()) {
                std::clog << "New command receieved" << '\n';
                auto& command = m_WorkQueue.front();

                switch (command.type) {
                    case kCommandType::Chat: {
                        // Chat has only two params
                        if (command.params.size() > 1) break;

                        if (command.params[0].size() > c_maxMsgSize) break;

                        // TODO: Remove when user system
                        websocketpp::lib::error_code err;
                        std::ostringstream oss;
                        {
                            wcpp_server::connection_ptr cptr =
                                server->get_con_from_hdl(command.hdl, err);

                            oss << cptr.get();
                        }

                        BroadcastAll(
                            LetsPlayServer::encode(std::vector<std::string>{
                                "chat", oss.str(), command.params[0]}));
                        break;
                    }
                    case kCommandType::Button:
                        // Broadcast one
                    case kCommandType::Turn:
                        break;
                        // Broadcast all
                    case kCommandType::Shutdown:
                        break;
                }

                m_WorkQueue.pop();
            }
        }
    }
}

void LetsPlayServer::BroadcastAll(const std::string& data) {
    std::clog << "BroadcastAll()" << '\n';
    std::unique_lock<std::mutex> lk((m_ConnectionsMutex));
    for (const auto& hdl : m_Connections)
        server->send(hdl, data, websocketpp::frame::opcode::text);
}

void LetsPlayServer::BroadcastOne(const std::string& data,
                                  websocketpp::connection_hdl hdl) {
    std::clog << "BroadcastOne()" << '\n';
    server->send(hdl, data, websocketpp::frame::opcode::text);
}

std::string LetsPlayServer::encode(const std::vector<std::string>& input) {
    std::clog << "encode()" << '\n';
    std::ostringstream oss;

    for (const auto& s : input) {
        oss << s.size();
        oss << '.';
        oss << s;
        oss << ',';
    }

    auto output = oss.str();
    output.back() = ';';

    return output;
}

std::vector<std::string> LetsPlayServer::decode(const std::string& input) {
    std::clog << "decode()" << '\n';
    std::vector<std::string> output;

    if (input.back() != ';') return output;

    std::istringstream iss{input};
    while (iss) {
        unsigned long long length{0};
        iss >> length;

        if (length == -1ull) {
            std::cout << "Overflow detected" << '\n';
            return std::vector<std::string>();
        }

        if (iss.peek() != '.') return std::vector<std::string>();

        iss.get();  // remove the period

        std::string content(length, 'x');
        iss.read(content.data(), length);
        output.push_back(content);

        if (iss.peek() != ',') {
            if (iss.peek() == ';') return output;

            return std::vector<std::string>();
        }

        iss.get();
    }
    return std::vector<std::string>();
}
