#include "LetsPlayServer.h"

LetsPlayServer::LetsPlayServer(std::filesystem::path& configFile) { config.LoadFrom(configFile); }

void LetsPlayServer::Run(std::uint16_t port) {
    if (port == 0) return;

    server.reset(new wcpp_server);

    try {
        server->set_access_channels(websocketpp::log::alevel::connect | websocketpp::log::alevel::disconnect);
        server->clear_access_channels(websocketpp::log::alevel::frame_payload | websocketpp::log::alevel::frame_header);

        server->init_asio();

        server->set_message_handler(std::bind(&LetsPlayServer::OnMessage, this, ::_1, ::_2));
        server->set_open_handler(std::bind(&LetsPlayServer::OnConnect, this, ::_1));
        server->set_close_handler(std::bind(&LetsPlayServer::OnDisconnect, this, ::_1));

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
            m_WorkQueue.push(Command{kCommandType::AddEmu, {"emu1", "./core", "./rom"}, {}, ""});
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
    {
        std::unique_lock lk((m_UsersMutex));
        m_Users[hdl].setUsername("");
    }
}

void LetsPlayServer::OnDisconnect(websocketpp::connection_hdl hdl) {
    websocketpp::lib::error_code err;
    wcpp_server::connection_ptr cptr = server->get_con_from_hdl(hdl, err);

    LetsPlayUser *user{nullptr};
    decltype(m_Users)::iterator search;
    {
        std::unique_lock lk((m_UsersMutex));
        search = m_Users.find(hdl);
        if (search == m_Users.end()) {
            std::clog << "Couldn't find user who left in list" << '\n';
            return;
        }
        user = &search->second;
    }

    if (user && !user->connectedEmu().empty()) {
        {
            std::unique_lock lk((m_EmusMutex));
            m_Emus[user->connectedEmu()]->userDisconnected(user);
        }
        BroadcastToEmu(user->connectedEmu(), LetsPlayServer::encode("leave", user->username()),
                       websocketpp::frame::opcode::text);
    }

    {
        std::unique_lock lk((m_UsersMutex));
        // Double check is on purpose
        search = m_Users.find(hdl);
        if (search != m_Users.end()) m_Users.erase(search);
    }
}

void LetsPlayServer::OnMessage(websocketpp::connection_hdl hdl, wcpp_server::message_ptr msg) {
    const std::string& data = msg->get_payload();
    const auto decoded = decode(data);

    if (decoded.empty()) return;
    // TODO: that switch case compile-time hash thing, its faster
    const auto& command = decoded.at(0);
    std::cout << '\n' << command << '\n';
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
    else if (command == "button")
        t = kCommandType::Button;
    else if (command == "add")
        t = kCommandType::AddEmu;
    else if (command == "shutdown")
        this->Shutdown();
    else
        return;

    EmuID_t emuID;
    LetsPlayUser *user{nullptr};
    if (std::unique_lock lk(m_UsersMutex); m_Users.find(hdl) != m_Users.end()) {
        user = &m_Users[hdl];
        emuID = user->connectedEmu();
    }

    Command c{t, std::vector<std::string>(), hdl, emuID, user};
    if (decoded.size() > 1) c.params = std::vector<std::string>(decoded.begin() + 1, decoded.end());

    {
        std::unique_lock lk(m_QueueMutex);
        m_WorkQueue.push(c);
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
        std::unique_lock lk((m_QueueMutex));
        while (!m_WorkQueue.empty()) m_WorkQueue.pop();
        // ... Except for a shutdown command
        m_WorkQueue.push(Command{kCommandType::Shutdown, std::vector<std::string>(),
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
        std::unique_lock lk((m_UsersMutex));
        for ([[maybe_unused]] const auto&[hdl, user] : m_Users)
            server->close(hdl, websocketpp::close::status::normal, "Closing", err);
    }
}

void LetsPlayServer::QueueThread() {
    while (m_QueueThreadRunning) {
        {
            std::unique_lock lk((m_QueueMutex));
            // Use std::condition_variable::wait Predicate?
            while (m_WorkQueue.empty()) m_QueueNotifier.wait(lk);

            if (!m_WorkQueue.empty()) {
                auto& command = m_WorkQueue.front();

                switch (command.type) {
                    case kCommandType::Chat: {
                        // Chat has only one, the message
                        if (command.params.size() != 1) break;
                        if (command.user->username().empty()) break;

                        // Message only has values in the range of typeable
                        // ascii characters excluding \n and \t
                        if (!LetsPlayServer::isAsciiStr(command.params[0])) break;

                        std::uint64_t maxMessageSize;
                        {
                            std::shared_lock lkk(config.mutex);
                            nlohmann::json& data = config.config["serverConfig"]["maxMessageSize"];

                            // TODO: Warning on invalid data type (logging
                            // system implemented)
                            if (!data.is_number_unsigned()) {
                                maxMessageSize =
                                    LetsPlayConfig::defaultConfig["serverConfig"]["maxMessageSize"];
                            } else {
                                maxMessageSize = data;
                            }
                        }

                        if (LetsPlayServer::escapedSize(command.params[0]) > maxMessageSize) break;

                        BroadcastAll(LetsPlayServer::encode("chat", command.user->username(),
                                                            command.params[0]),
                                     websocketpp::frame::opcode::text);
                    }
                        break;
                    case kCommandType::Username: {
                        // Username has only one param, the username
                        if (command.params.size() != 1) break;

                        const auto& newUsername = command.params.at(0);
                        const auto oldUsername = command.user->username();

                        // Ignore no change
                        if (newUsername == oldUsername) break;

                        std::uint64_t maxUsernameLen, minUsernameLen;
                        {
                            std::shared_lock lkk(config.mutex);
                            nlohmann::json& max =
                                config.config["serverConfig"]["maxUsernameLength"];
                            nlohmann::json& min =
                                config.config["serverConfig"]["minUsernameLength"];

                            // TODO: Warning on invalid data type (logging
                            // system implemented)
                            if (!max.is_number_unsigned()) {
                                maxUsernameLen = LetsPlayConfig::defaultConfig["serverConfig"]
                                ["maxUsernameLength"];
                            } else {
                                maxUsernameLen = max;
                            }

                            if (!min.is_number_unsigned()) {
                                minUsernameLen = LetsPlayConfig::defaultConfig["serverConfig"]
                                ["minUsernameLength"];
                            } else {
                                minUsernameLen = min;
                            }
                        }

                        auto usernameValid = [](const bool isValid,
                                                const std::string& currentUsername) {
                            return LetsPlayServer::encode("username", isValid, currentUsername);
                        };

                        // Size based checks
                        if (newUsername.size() > maxUsernameLen ||
                            newUsername.size() < minUsernameLen) {
                            BroadcastOne(usernameValid(false, oldUsername), command.hdl);
                            break;
                        }

                        // Content based checks
                        if (newUsername.front() == ' ' || newUsername.back() == ' ' ||
                            !LetsPlayServer::isAsciiStr(newUsername) ||
                            (newUsername.find("  ") != std::string::npos)) {
                            BroadcastOne(usernameValid(false, oldUsername), command.hdl);
                            break;
                        }

                        // Check if username isn't already taken
                        bool taken{false};
                        {
                            std::unique_lock lkk(m_UsersMutex);
                            for (auto&[hdl, user] : m_Users) {
                                std::cout << user.uuid() << ' ' << command.user->uuid() << '\n';
                                std::cout << user.username() << ' ' << newUsername << '\n';
                                std::cout << std::boolalpha << !hdl.expired() << '\n';
                                if (user.uuid() != command.user->uuid() &&
                                    user.username() == newUsername && !hdl.expired()) {
                                    BroadcastOne(usernameValid(false, oldUsername), command.hdl);
                                    taken = true;
                                    break;
                                }
                            }
                        }
                        if (taken) break;

                        command.user->setUsername(newUsername);

                        if (oldUsername.empty())
                            // Join
                            ;
                        else {
                            BroadcastOne(usernameValid(true, newUsername), command.hdl);

                            // Tell everyone someone changed their username
                            // TODO: BroadcastToEmu
                            BroadcastAll(
                                LetsPlayServer::encode("username", oldUsername, newUsername),
                                websocketpp::frame::opcode::text);
                        }
                    }
                        break;
                    case kCommandType::List: {
                        if (!command.params.empty()) break;
                        std::vector<std::string> message;
                        message.emplace_back("list");

                        {
                            std::unique_lock lkk((m_UsersMutex));
                            for (auto&[hdl, user] : m_Users)
                                if ((command.user->connectedEmu() == user.connectedEmu()) &&
                                    !hdl.expired())
                                    message.push_back(user.username());
                        }

                        BroadcastOne(LetsPlayServer::encode(message), command.hdl);
                    }
                        break;
                    case kCommandType::Turn: {
                        if (!command.params.empty()) break;
                        if (command.user->connectedEmu().empty() || command.user->requestedTurn)
                            break;

                        std::unique_lock lkk(m_EmusMutex);
                        if (auto emu = m_Emus[command.emuID]; emu) {
                            command.user->requestedTurn = true;
                            emu->addTurnRequest(command.user);
                        }
                    }
                        break;
                    case kCommandType::Shutdown:break;
                    case kCommandType::Connect: {
                        if (command.params.size() != 1) break;
                        if (command.user->username().empty())
                            break;  // Must have a username to connect to an emu

                        // Check if the emu that the connect thing that was sent exists
                        if (std::unique_lock lkk(m_EmusMutex);
                            m_Emus.find(command.params[0]) == m_Emus.end()) {
                            if (websocketpp::lib::error_code ec; !command.hdl.expired()) {
                                server->send(command.hdl,
                                             LetsPlayServer::encode("error", error::connectInvalidEmu),
                                             websocketpp::frame::opcode::text,
                                             ec);
                            }
                            break;
                        }

                        // NOTE: Can remove check and allow on the fly
                        // switching once the transition between being
                        // connected to A and being connected to B is
                        // figured out
                        if (!command.user->connectedEmu().empty()) break;

                        command.user->setConnectedEmu(command.params[0]);
                        {
                            std::unique_lock lkk(m_EmusMutex);
                            m_Emus[command.user->connectedEmu()]->userConnected(command.user);
                        }
                        BroadcastToEmu(command.user->connectedEmu(),
                                       LetsPlayServer::encode("join", command.user->username()),
                                       websocketpp::frame::opcode::text);
                    }
                        break;
                    case kCommandType::Button: {  // up/down, id
                        if (command.params.size() != 2) return;

                        if (command.params[0].front() == '-') return;

                        if (!command.emuID.empty()) {
                            unsigned buttonID{0};
                            try {
                                buttonID = std::stoi(command.params[1]);
                            } catch (const std::invalid_argument& e) {
                                break;
                            } catch (const std::out_of_range& e) {
                                break;
                            }
                            if (buttonID > 15u) break;

                            std::unique_lock lkk(m_EmusMutex);
                            if (command.params[0] == "up") {
                                m_Emus[command.emuID]->joypad->buttonRelease(buttonID);
                            } else if (command.params[0] == "down") {
                                m_Emus[command.emuID]->joypad->buttonPress(buttonID);
                            }
                        }
                    }
                        break;
                    case kCommandType::AddEmu: {  // emu, libretro core
                        // path, rom path
                        // TODO:: Add file path checks and admin check
                        if (command.params.size() != 3) break;

                        auto& id = command.params[0];
                        const auto& corePath = command.params[1];
                        const auto& romPath = command.params[2];

                        {
                            std::unique_lock lkk(m_EmuThreadMutex);
                            m_EmulatorThreads.emplace_back(
                                std::thread(EmulatorController::Run, corePath, romPath, this, id));
                        }
                    }
                        break;
                    case kCommandType::RemoveEmu:
                    case kCommandType::StopEmu:
                    case kCommandType::Admin:
                    case kCommandType::Config:
                    case kCommandType::Unknown:
                        // Unimplemented
                        break;
                    default:break;
                }
                m_WorkQueue.pop();
            }
        }
    }
}

void LetsPlayServer::BroadcastAll(const std::string& data, websocketpp::frame::opcode::value op) {
    std::unique_lock lk(m_UsersMutex, std::try_to_lock);
    for (auto&[hdl, user] : m_Users) {
        if (websocketpp::lib::error_code ec; !user.username().empty() && !hdl.expired())
            server->send(hdl, data, op, ec);
    }
}

void LetsPlayServer::BroadcastOne(const std::string&& data, websocketpp::connection_hdl hdl) {
    websocketpp::lib::error_code ec;
    server->send(hdl, data, websocketpp::frame::opcode::text, ec);
}

void LetsPlayServer::BroadcastToEmu(const EmuID_t& id, const std::string& message,
                                    websocketpp::frame::opcode::value op) {
    std::unique_lock lk(m_UsersMutex, std::try_to_lock);
    for (auto&[hdl, user] : m_Users) {
        if (websocketpp::lib::error_code ec; user.connectedEmu() == id && !user.username().empty() && !hdl.expired())
            server->send(hdl, message, op, ec);
    }
}

void LetsPlayServer::AddEmu(const EmuID_t& id, EmulatorControllerProxy *emu) {
    std::unique_lock lk(m_EmusMutex);
    m_Emus[id] = emu;
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

        // TODO: Make the max received size equal to maxMessageSize (config) multiplied by len("\u{1AAAA}') plus len('4.chat,') + len(';'). Pass as a parameter to decode for keeping it static.
        if (length >= 1'000) {
            return std::vector<std::string>();
        }

        if (iss.peek() != '.') return std::vector<std::string>();

        iss.get();  // remove the period

        std::string content(length, 'x');
        iss.read(content.data(), static_cast<std::streamsize>(length));
        output.push_back(content);

        const char& separator = iss.peek();
        if (separator != ',') {
            if (separator == ';') return output;

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

void LetsPlayServer::SendFrame(const EmuID_t& id) {
    thread_local static tjhandle _jpegCompressor = tjInitCompress();
    thread_local static long unsigned int _jpegBufferSize = 0;
    thread_local static std::uint8_t *jpegData{nullptr};
    Frame frame = [&]() {
        std::unique_lock lk(m_EmusMutex);
        auto emu = m_Emus[id];
        return emu->getFrame();
    }();

    // currentBuffer was nullptr
    if (frame.width == 0 || frame.height == 0) return;

    unsigned quality = LetsPlayConfig::defaultConfig["serverConfig"]["jpegQuality"];
    if (std::shared_lock lk(config.mutex); config.config["serverConfig"].count("jpegQuality")) {
        nlohmann::json& value = config.config["serverConfig"]["jpegQuality"];
        if (value.is_number() && (value <= 100) && (value >= 1)) quality = value;
    }
    long unsigned int jpegSize = _jpegBufferSize;
    tjCompress2(_jpegCompressor, frame.data.get(), frame.width, frame.width * 3, frame.height,
                TJPF_RGB, &jpegData, &jpegSize, TJSAMP_420, quality, TJFLAG_ACCURATEDCT);

    _jpegBufferSize = _jpegBufferSize >= jpegSize ? _jpegBufferSize : jpegSize;

    std::unique_lock lk(m_UsersMutex);
    for (auto&[hdl, user] : m_Users) {
        if (user.connectedEmu() == id && !hdl.expired()) {
            websocketpp::lib::error_code ec;
            server->send(hdl, jpegData, jpegSize, websocketpp::frame::opcode::binary, ec);
        }
    }
}

std::string LetsPlayServer::escapeTilde(std::string str) {
    if (str.front() == '~') {
        const char *homePath = std::getenv("HOME");
        if (!homePath) {
            std::cerr << "Tilde path was specified but couldn't retrieve "
                         "actual home path. Check if $HOME was declared.\n";
            return ".";
        }

        str.erase(0, 1);
        str.insert(0, homePath);
    }
    return str;
}

std::string LetsPlayServer::encode(const std::vector<std::string>& chunks) {
    std::ostringstream oss;
    for (const auto& chunk : chunks) {
        oss << chunk.size();
        oss << '.';
        oss << chunk;
        oss << ',';
    }

    std::string out = oss.str();
    out.back() = ';';
    return out;
}
