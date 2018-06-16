#include "LetsPlayServer.h"

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

        // Skip having to connect, change username, addemu
        {
            std::unique_lock<std::mutex> lk((m_QueueMutex));
            m_WorkQueue.push(
                Command{kCommandType::AddEmu,
                        {"emu1", "./vbam_libretro.so", "./smw.gba"},
                        {},
                        ""});
            m_QueueNotifier.notify_one();
        }

        server->start_accept();
        server->run();

        this->Shutdown();

    } catch (websocketpp::exception const& e) {
        std::cerr << e.what() << '\n';
    } catch (...) {
        throw;
    }
}

void LetsPlayServer::OnConnect(websocketpp::connection_hdl hdl) {
    websocketpp::lib::error_code err;
    {
        std::unique_lock<std::mutex> lk((m_UsersMutex));
        m_Users[hdl].setUsername("");
    }
    std::string payload = "\x01\x02\x03\x04hello";
    server->send(hdl, payload, websocketpp::frame::opcode::binary);
}

void LetsPlayServer::OnDisconnect(websocketpp::connection_hdl hdl) {
    websocketpp::lib::error_code err;
    wcpp_server::connection_ptr cptr = server->get_con_from_hdl(hdl, err);

    LetsPlayUser* user{nullptr};
    auto search = m_Users.end();
    {
        std::unique_lock<std::mutex> lk((m_UsersMutex));
        search = m_Users.find(hdl);
        if (search == m_Users.end()) {
            std::clog << "Couldn't find left user in list" << '\n';
            return;
        }
        user = &search->second;
    }

    if (user && user->connectedEmu() != "") {
        {
            std::unique_lock<std::mutex> lk((m_EmusMutex));
            m_Emus[user->connectedEmu()]->userDisconnected(user);
        }
    }

    {
        std::unique_lock<std::mutex> lk((m_UsersMutex));
        m_Users.erase(search);
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
    kCommandType t = kCommandType::Unknown;

    if (command == "list")  // No params
        t = kCommandType::List;
    else if (command == "chat")  // message
        t = kCommandType::Chat;
    else if (command == "username")  // newname
        t = kCommandType::Username;
    else if (command == "button")  // button id, 0/1 for keyup/keydown
        t = kCommandType::Button;
    else if (command == "connect")  // emuid
        t = kCommandType::Connect;
    else if (command == "turn")  // No params
        t = kCommandType::Turn;
    else if (command == "webp")
        t = kCommandType::Webp;
    else if (command == "add")
        t = kCommandType::AddEmu;
    else if (command == "shutdown")
        this->Shutdown();
    else
        return;

    std::vector<std::string> params(decoded.begin() + 1, decoded.end());
    {
        std::unique_lock<std::mutex> lk((m_QueueMutex));
        std::cout << command << '(';
        for (const auto& param : params) std::cout << param << ", ";
        std::cout << ')' << '\n';
        m_WorkQueue.push(Command{t, params, hdl, ""});
    }

    m_QueueNotifier.notify_one();
}

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
                                 websocketpp::connection_hdl(), ""});
    }

    std::clog << "Stopping listen..." << '\n';
    // Stop listening so the queue doesn't grow any more
    websocketpp::lib::error_code err;
    server->stop_listening(err);
    if (err) std::cerr << "Error stopping listen " << err.message() << '\n';
    // Wake up the turn and work threads
    std::clog << "Waking up work thread..." << '\n';
    m_QueueNotifier.notify_one();
    // Wait until they stop looping
    std::clog << "Waiting for work thread to stop..." << '\n';
    m_QueueThread.join();

    // Close every connection
    {
        std::clog << "Closing every connection..." << '\n';
        std::unique_lock<std::mutex> lk((m_UsersMutex));
        for ([[maybe_unused]] const auto& [hdl, user] : m_Users)
            server->close(hdl, websocketpp::close::status::normal, "Closing",
                          err);
    }
}

void LetsPlayServer::QueueThread() {
    while (m_QueueThreadRunning) {
        {
            std::unique_lock<std::mutex> lk((m_QueueMutex));
            // Use std::condition_variable::wait Predicate?
            while (m_WorkQueue.empty()) m_QueueNotifier.wait(lk);

            if (!m_WorkQueue.empty()) {
                auto& command = m_WorkQueue.front();

                switch (command.type) {
                    case kCommandType::Chat: {
                        // Chat has only one, the message
                        if (command.params.size() > 1) break;
                        std::string username;
                        {
                            std::unique_lock<std::mutex> lk((m_UsersMutex));
                            LetsPlayUser* user = &(m_Users[command.hdl]);
                            if (user && (user->connectedEmu() == "")) break;
                            username = user->username();
                        }

                        if (username == "") break;

                        // Message only has values in the range of typeable
                        // ascii characters excluding \n and \t
                        if (!LetsPlayServer::isAsciiStr(command.params[0]))
                            break;

                        if (LetsPlayServer::escapedSize(command.params[0]) >
                            c_maxMsgSize)
                            break;

                        BroadcastAll(
                            command.emuID + ": " +
                                LetsPlayServer::encode(std::vector<std::string>{
                                    "chat", username, command.params[0]}),
                            websocketpp::frame::opcode::text);
                    } break;
                    case kCommandType::Username: {
                        // Username has only one param, the username
                        if (command.params.size() > 1) break;

                        const auto& newUsername = command.params.at(0);
                        if (newUsername.size() > c_maxUserName ||
                            newUsername.size() < c_minUserName)
                            break;
                        if (!LetsPlayServer::isAsciiStr(newUsername)) break;
                        if (newUsername.front() == ' ' ||
                            newUsername.back() == ' ' ||
                            (newUsername.find("  ") != std::string::npos))
                            break;

                        std::string oldUsername;
                        {
                            std::unique_lock<std::mutex> lk((m_UsersMutex));
                            oldUsername = m_Users[command.hdl].username();
                            m_Users[command.hdl].setUsername(newUsername);
                        }

                        if (oldUsername == "")
                            // TODO: Join?
                            BroadcastAll(newUsername + " has joined!",
                                         websocketpp::frame::opcode::text);
                        else
                            BroadcastAll(
                                LetsPlayServer::encode(std::vector<std::string>{
                                    "username", oldUsername, newUsername}),
                                websocketpp::frame::opcode::text);
                    } break;
                    case kCommandType::List: {
                        if (command.params.size() > 0) break;
                        std::vector<std::string> message;
                        message.push_back("list");

                        {
                            std::unique_lock<std::mutex> lk((m_UsersMutex));
                            for ([[maybe_unused]] auto& [hdl, user] : m_Users)
                                message.push_back(user.username());
                        }

                        BroadcastOne(LetsPlayServer::encode(message),
                                     command.hdl);
                    }
                    case kCommandType::Button:
                        // Broadcast none
                        break;
                    case kCommandType::Turn: {
                        LetsPlayUser* user{nullptr};
                        {
                            std::unique_lock<std::mutex> lk((m_UsersMutex));
                            user = &(m_Users[command.hdl]);
                            std::cout << user->connectedEmu() << '\n';
                            if ((user->connectedEmu() == "") ||
                                user->requestedTurn) {
                                std::cout << "User not connected to a vm or is "
                                             "already in queue"
                                          << '\n';
                                break;
                            }

                            user->requestedTurn = true;
                        }
                        std::cout << "User lookup success" << '\n';
                        {
                            std::unique_lock<std::mutex> lk((m_EmusMutex));
                            auto emu = m_Emus[user->connectedEmu()];
                            if (emu) {
                                std::cout << "Adding user to queue..." << '\n';
                                emu->addTurnRequest(user);
                            } else
                                std::cout << "Invalid emu in lookup in onTurn"
                                          << '\n';
                        }

                    } break;
                    case kCommandType::Shutdown:
                        break;
                    case kCommandType::Connect: {
                        if (command.params.size() != 1) break;

                        // Check if the emu that the connect
                        {
                            std::unique_lock<std::mutex> lk((m_EmusMutex));
                            if (m_Emus.find(command.params[0]) ==
                                m_Emus.end()) {
                                std::cout << '\'' << command.emuID
                                          << "' not a valid emu\n";
                                break;
                            }
                        }
                        // NOTE: only allow switching emus if
                        // emulatorcontrollers don't end up storing who's
                        // connected, this will just make for a mess to update
                        // the list
                        LetsPlayUser* user{nullptr};
                        {
                            std::unique_lock<std::mutex> lk((m_UsersMutex));
                            user = &m_Users[command.hdl];
                        }

                        if (user->username() == "") {
                            std::cout << "user wasn't username'd" << '\n';
                            break;
                        }

                        user->setConnectedEmu(command.params[0]);
                        {
                            std::unique_lock<std::mutex> lk((m_EmusMutex));
                            m_Emus[user->connectedEmu()]->userConnected(user);
                        }
                    } break;
                    case kCommandType::AddEmu: {  // emu, libretro core path,
                                                  // rom path
                        // TODO:: Add file path checks and admin check
                        if (command.params.size() != 3) break;

                        auto& id = command.params[0];
                        const auto& corePath = command.params[1];
                        const auto& romPath = command.params[2];

                        {
                            std::unique_lock<std::mutex> lk((m_EmuThreadMutex));
                            m_EmulatorThreads.emplace_back(
                                std::thread(EmulatorController::Run, corePath,
                                            romPath, this, id));
                        }
                    } break;
                    case kCommandType::Webp: {
                        if (command.params.size() != 0) break;

                        std::unique_lock<std::mutex> lk((m_UsersMutex));
                        m_Users[command.hdl].supportsWebp = true;
                    } break;
                    case kCommandType::RemoveEmu:
                    case kCommandType::StopEmu:
                    case kCommandType::Unknown:
                        // Unimplemented
                        break;
                }

                m_WorkQueue.pop();
            }
        }
    }
}

void LetsPlayServer::BroadcastAll(const std::string& data,
                                  websocketpp::frame::opcode::value op) {
    std::unique_lock<std::mutex> lk(m_UsersMutex, std::try_to_lock);
    for (auto& [hdl, user] : m_Users) {
        if (user.username() != "" && !hdl.expired())
            server->send(hdl, data, op);
    }
}

void LetsPlayServer::BroadcastOne(const std::string& data,
                                  websocketpp::connection_hdl hdl) {
    server->send(hdl, data, websocketpp::frame::opcode::text);
}

void LetsPlayServer::AddEmu(const EmuID_t& id, EmulatorControllerProxy* emu) {
    std::unique_lock<std::mutex> lk((m_EmusMutex));
    m_Emus[id] = emu;
}

std::string LetsPlayServer::encode(const std::vector<std::string>& input) {
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
    std::vector<std::string> output;

    if (input.back() != ';') return output;

    std::istringstream iss{input};
    while (iss) {
        unsigned long long length{0};
        // if length is greater than -1ull then length will just be equal to
        // -1ull, no overflows here
        iss >> length;

        if (length >= 1'000) {
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

bool LetsPlayServer::isAsciiStr(const std::string& str) {
    return std::all_of(str.begin(), str.end(),
                       [](const char c) { return (c >= ' ') && (c <= '~'); });
}

size_t LetsPlayServer::escapedSize(const std::string& str) {
    // matches \uXXXX, \xXX, and \u{1XXXX}
    static const std::regex re{
        R"((\\x[\da-f]{2}|\\u[\da-f]{4}|\\u\{1[\da-f]{4}\}))"};
    const std::string output = std::regex_replace(str, re, "X");
    return output.size();
}

void LetsPlayServer::SendFrame(const EmuID_t& id, frametype::value type) {
    // TODO: webp and png differentiation
    // TODO: send the smaller of the two, webp or png (doing this requires
    // clientside being able to read the first byte of the frame and from that
    // determine the file type, webp or png. Should be easy because png starts
    // with 0x89, webp with 0x52)
    Frame frame;
    {
        std::unique_lock<std::mutex> lk(m_EmusMutex);
        auto emu = m_Emus[id];
        switch (type) {
            case frametype::key:
                frame = emu->getFrame(KeyFrame{true});
                break;
            case frametype::delta:
                frame = emu->getFrame(KeyFrame{false});
                break;
        }
        // If frame is uninitialized (screen buffer was probably nullptr, this
        // function fired before the libretro core setup the screeni, or the
        // video mode changed and the video refresh callback hasn't gone off)
        if (frame.height == 0 && frame.width == 0) return;
    }

    std::uint8_t* webpData{nullptr};
    size_t webpWritten;
    if (frame.alphaChannel) {
        std::clog << "Delta frame -- wrote ";
        auto& RGBAVec = std::get<1>(frame.colors);
        webpWritten = WebPEncodeLosslessRGBA(
            reinterpret_cast<const std::uint8_t*>(RGBAVec.data()), frame.width,
            frame.height, frame.width * 4, &webpData);
    } else {
        std::clog << "Key frame -- wrote ";
        auto& RGBVec = std::get<0>(frame.colors);
        webpWritten = WebPEncodeLosslessRGB(
            reinterpret_cast<const std::uint8_t*>(RGBVec.data()), frame.width,
            frame.height, frame.width * 3, &webpData);
    }
    std::clog << webpWritten << " bytes\n";

    // Do png encoding

    // TODO: once png encoding implemented, make png be the backup if webp fails
    // for whatever reason
    if (!webpData || !webpWritten) return;

    std::unique_lock<std::mutex> lk(m_UsersMutex);
    for (auto& [hdl, user] : m_Users) {
        if (user.connectedEmu() == id) {
            server->send(hdl, webpData, webpWritten,
                         websocketpp::frame::opcode::binary);
        }
    }

    WebPFree(webpData);
}
