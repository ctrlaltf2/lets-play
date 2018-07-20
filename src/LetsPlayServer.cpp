#include "LetsPlayServer.h"

LetsPlayServer::LetsPlayServer(std::filesystem::path& configFile) { config.LoadFrom(configFile); }

void LetsPlayServer::Run(std::uint16_t port) {
    if (port == 0) return;

    server.reset(new wcpp_server);

    try {
        server->set_access_channels(websocketpp::log::alevel::all);
        server->clear_access_channels(websocketpp::log::alevel::frame_payload);

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
    websocketpp::lib::error_code err;
    {
        std::unique_lock lk((m_UsersMutex));
        m_Users[hdl].setUsername("");
    }
}

void LetsPlayServer::OnDisconnect(websocketpp::connection_hdl hdl) {
    websocketpp::lib::error_code err;
    wcpp_server::connection_ptr cptr = server->get_con_from_hdl(hdl, err);

    LetsPlayUser* user{nullptr};
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

    if (user && user->connectedEmu() != "") {
        {
            std::unique_lock lk((m_EmusMutex));
            m_Emus[user->connectedEmu()]->userDisconnected(user);
        }
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
    else if (command == "webp")
        t = kCommandType::Webp;
    else if (command == "button")
        t = kCommandType::Button;
    else if (command == "add")
        t = kCommandType::AddEmu;
    else if (command == "shutdown")
        this->Shutdown();
    else
        return;

    EmuID_t emuID;
    LetsPlayUser* user{nullptr};
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
        while (m_WorkQueue.size()) m_WorkQueue.pop();
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
        for ([[maybe_unused]] const auto& [hdl, user] : m_Users)
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
                        if (command.user->username() == "") break;

                        // Message only has values in the range of typeable
                        // ascii characters excluding \n and \t
                        if (!LetsPlayServer::isAsciiStr(command.params[0])) break;

                        std::uint64_t maxMessageSize;
                        {
                            std::shared_lock lk(config.mutex);
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
                    } break;
                    case kCommandType::Username: {
                        // Username has only one param, the username
                        if (command.params.size() != 1) break;

                        const auto& newUsername = command.params.at(0);
                        const auto oldUsername = command.user->username();

                        std::uint64_t maxUsernameLen, minUsernameLen;
                        {
                            std::shared_lock lk(config.mutex);
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

                        command.user->setUsername(newUsername);

                        if (oldUsername == "")
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
                    } break;
                    case kCommandType::List: {
                        if (command.params.size() != 0) break;
                        std::vector<std::string> message;
                        message.push_back("list");

                        {
                            std::unique_lock lk((m_UsersMutex));
                            for (auto& [hdl, user] : m_Users)
                                if ((command.user->connectedEmu() == user.connectedEmu()) &&
                                    !hdl.expired())
                                    message.push_back(user.username());
                        }

                        // TODO: Fix
                        // BroadcastOne(LetsPlayServer::encode(message),
                        // command.hdl);
                    } break;
                    case kCommandType::Turn: {
                        if (command.params.size() != 0) break;
                        if (command.user->connectedEmu() == "" || command.user->requestedTurn)
                            break;

                        std::unique_lock lk((m_EmusMutex));
                        if (auto emu = m_Emus[command.emuID]; emu) {
                            command.user->requestedTurn = true;
                            emu->addTurnRequest(command.user);
                        }
                    } break;
                    case kCommandType::Shutdown:
                        break;
                    case kCommandType::Connect: {
                        if (command.params.size() != 1) break;

                        // Check if the emu that the connect thing that was
                        // sent exists
                        if (std::unique_lock lk((m_EmusMutex));
                            m_Emus.find(command.params[0]) == m_Emus.end()) {
                            if (websocketpp::lib::error_code ec; !command.hdl.expired()) {
                                server->send(
                                    command.hdl,
                                    LetsPlayServer::encode("error", error::connectInvalidEmu),
                                    websocketpp::frame::opcode::text, ec);
                            }
                            break;
                        }

                        if (command.user->username() == "")
                            break;  // Must have a username to connect to an
                                    // emu

                        // NOTE: Can remove check and allow on the fly
                        // switching once the transition between being
                        // connected to A and being connected to B is
                        // figured out
                        if (command.user->connectedEmu() != "") break;

                        command.user->setConnectedEmu(command.params[0]);
                        {
                            std::unique_lock lk((m_EmusMutex));
                            m_Emus[command.user->connectedEmu()]->userConnected(command.user);
                        }
                    } break;
                    case kCommandType::Button: {  // up/down, id
                        if (command.params.size() != 2) return;

                        if (command.params[0].front() == '-') return;

                        if (command.emuID != "") {
                            unsigned buttonID{0};
                            try {
                                buttonID = std::stoi(command.params[1]);
                            } catch (const std::invalid_argument& e) {
                                break;
                            } catch (const std::out_of_range& e) {
                                break;
                            }
                            if (buttonID > 15u) break;

                            std::unique_lock lk(m_EmusMutex);
                            if (command.params[0] == "up") {
                                m_Emus[command.emuID]->joypad->buttonRelease(buttonID);
                            } else if (command.params[0] == "down") {
                                m_Emus[command.emuID]->joypad->buttonPress(buttonID);
                            }
                        }
                    } break;
                    case kCommandType::AddEmu: {  // emu, libretro core
                                                  // path, rom path
                        // TODO:: Add file path checks and admin check
                        if (command.params.size() != 3) break;

                        auto& id = command.params[0];
                        const auto& corePath = command.params[1];
                        const auto& romPath = command.params[2];

                        {
                            std::unique_lock lk((m_EmuThreadMutex));
                            m_EmulatorThreads.emplace_back(
                                std::thread(EmulatorController::Run, corePath, romPath, this, id));
                        }
                    } break;
                    case kCommandType::Webp: {
                        if (command.params.size() != 0) break;
                        m_Users[command.hdl].supportsWebp = true;
                    } break;
                    case kCommandType::RemoveEmu:
                    case kCommandType::StopEmu:
                    case kCommandType::Admin:
                    case kCommandType::Config:
                    case kCommandType::Unknown:
                        // Unimplemented
                        break;
                    default:
                        break;
                }
                m_WorkQueue.pop();
            }
        }
    }
}

void LetsPlayServer::BroadcastAll(const std::string& data, websocketpp::frame::opcode::value op) {
    std::unique_lock lk(m_UsersMutex, std::try_to_lock);
    for (auto& [hdl, user] : m_Users) {
        if (user.username() != "" && !hdl.expired()) server->send(hdl, data, op);
    }
}

void LetsPlayServer::BroadcastOne(const std::string& data, websocketpp::connection_hdl hdl) {
    server->send(hdl, data, websocketpp::frame::opcode::text);
}

void LetsPlayServer::AddEmu(const EmuID_t& id, EmulatorControllerProxy* emu) {
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

        if (length >= 1'000) {
            return std::vector<std::string>();
        }

        if (iss.peek() != '.') return std::vector<std::string>();

        iss.get();  // remove the period

        std::string content(length, 'x');
        iss.read(content.data(), length);
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
    thread_local static std::uint8_t* jpegData{nullptr};
    // TODO: webp and png differentiation
    // TODO: send the smaller of the two, webp or png (doing this requires
    // clientside being able to read the first byte of the frame and from
    // that determine the file type, webp or png. Should be easy because png
    // starts with 0x89, webp with 0x52)
    Frame frame = [&]() {
        std::unique_lock lk(m_EmusMutex);
        auto emu = m_Emus[id];
        return emu->getFrame();
    }();

    // currentBuffer was nullptr
    if (frame.width == 0 || frame.height == 0) return;

    /*
    auto webpStart = std::chrono::steady_clock::now();
    std::uint8_t* webpData{nullptr};
    size_t webpWritten = WebPEncodeLosslessRGB(
        &frame.data[0], frame.width, frame.height, frame.width * 3,
    &webpData); auto webpEnd = std::chrono::steady_clock::now();

    auto pngStart = std::chrono::steady_clock::now();
    std::vector<std::uint8_t> pngData;
    std::ostringstream out;
    if (frame.palette.size() <= 256) {  // Use indexed color png
        png::image<png::index_pixel> image(frame.width, frame.height);
        png::palette pal(frame.palette.size());
        unsigned i = 0;
        for (auto paletteColor = frame.palette.begin();
             paletteColor != frame.palette.end(); ++paletteColor, ++i) {
            pal[i] =
                png::color(paletteColor->r, paletteColor->g,
    paletteColor->b);
        }
        image.set_palette(pal);
        for (std::uint32_t y = 0, j = 0; y < image.get_height(); ++y) {
            for (std::uint32_t x = 0; x < image.get_width(); ++x, j += 3) {
                png::color color = png::color{frame.data[j], frame.data[j +
    1], frame.data[j + 2]}; image[y][x] = std::distance( pal.begin(),
    std::find(pal.begin(), pal.end(), color));
            }
        }
        image.write_stream(out);
    } else {
        png::image<png::rgb_pixel> image(frame.width, frame.height);
        for (std::uint32_t y = 0, i = 0; y < image.get_height(); ++y) {
            for (std::uint32_t x = 0; x < image.get_width(); ++x, i += 3) {
                image[y][x] = png::rgb_pixel(frame.data[i], frame.data[i +
    1], frame.data[i + 2]);
            }
        }
        image.write_stream(out);
    }
    // ree
    std::string data = out.str();
    for (const char c : data) pngData.push_back(static_cast<unsigned
    char>(c)); auto pngEnd = std::chrono::steady_clock::now();*/

    unsigned quality = LetsPlayConfig::defaultConfig["serverConfig"]["jpegQuality"];
    if (std::shared_lock lk(config.mutex); config.config["serverConfig"].count("jpegQuality")) {
        nlohmann::json& value = config.config["serverConfig"]["jpegQuality"];
        if (value.is_number() && (value <= 100) && (value >= 1)) quality = value;
    }
    auto jpegStart = std::chrono::steady_clock::now();
    long unsigned int jpegSize = _jpegBufferSize;
    tjCompress2(_jpegCompressor, frame.data.get(), frame.width, frame.width * 3, frame.height,
                TJPF_RGB, &jpegData, &jpegSize, TJSAMP_420, quality, TJFLAG_ACCURATEDCT);

    _jpegBufferSize = _jpegBufferSize >= jpegSize ? _jpegBufferSize : jpegSize;
    auto jpegEnd = std::chrono::steady_clock::now();

    std::clog /*<< "PNG:\t\t" << pngData.size() << " bytes\t\t"
              << (pngEnd - pngStart).count() << "ms\n"
              << "WebP:\t\t" << webpWritten << " bytes\t\t"
              << (webpEnd - webpStart).count() << "ms\n"*/
        << "JPEG:\t\t" << jpegSize << " bytes\t\t"
        << std::chrono::duration_cast<std::chrono::nanoseconds>(jpegEnd - jpegStart).count()
        << "ms\n";
    //<< '\n';

    // std::clog << webpWritten << " bytes\n";

    // Do png encoding

    // TODO: once png encoding implemented, make png be the backup if webp
    // fails for whatever reason
    // if (!webpData || !webpWritten) return;

    std::unique_lock lk(m_UsersMutex);
    for (auto& [hdl, user] : m_Users) {
        if (user.connectedEmu() == id && !hdl.expired()) {
            websocketpp::lib::error_code ec;
            server->send(hdl, jpegData, jpegSize, websocketpp::frame::opcode::binary, ec);
        }
    }

    // WebPFree(webpData);
}

std::string LetsPlayServer::escapeTilde(std::string str) {
    if (str.front() == '~') {
        const char* homePath = std::getenv("HOME");
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
