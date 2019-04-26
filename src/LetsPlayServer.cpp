#include "LetsPlayServer.h"

LetsPlayServer::LetsPlayServer(lib::filesystem::path& configFile) { config.LoadFrom(configFile); }

void LetsPlayServer::Run(std::uint16_t port) {
    if (port == 0) return;

    server.reset(new wcpp_server);

    SetupLetsPlayDirectories();

    try {
        server->set_access_channels(websocketpp::log::alevel::connect | websocketpp::log::alevel::disconnect);
        server->clear_access_channels(websocketpp::log::alevel::frame_payload | websocketpp::log::alevel::frame_header);

        server->init_asio();

        server->set_validate_handler(std::bind(&LetsPlayServer::OnValidate, this, ::_1));
        server->set_message_handler(std::bind(&LetsPlayServer::OnMessage, this, ::_1, ::_2));
        server->set_open_handler(std::bind(&LetsPlayServer::OnConnect, this, ::_1));
        server->set_close_handler(std::bind(&LetsPlayServer::OnDisconnect, this, ::_1));
        server->set_http_handler(std::bind(&LetsPlayServer::OnHTTP, this, ::_1));

        websocketpp::lib::error_code err;
        server->listen(port, err);

        if (err)
            throw std::runtime_error(std::string("Failed to listen on port ") +
                std::to_string(port));

        m_QueueThreadRunning = true;

        m_QueueThread = std::thread{[&]() { this->QueueThread(); }};

        // Schedule periodic tasks
        auto savePeriod = std::chrono::minutes(
                config.get<std::uint64_t>(nlohmann::json::value_t::number_unsigned, "serverConfig",
                                          "backups", "historyInterval"));
        auto backupPeriod = std::chrono::minutes(
                config.get<std::uint64_t>(nlohmann::json::value_t::number_unsigned, "serverConfig",
                                          "backups", "backupInterval"));

        // TODO: Queue on emulator controller, expose queue related stuff through one interface, use pointers n shit to expose it
        std::function<void()> previewFunc = [&]() { this->PreviewTask(); };
        std::function<void()> saveFunc = [&]() { this->SaveTask(); };
        std::function<void()> backupFunc = [&]() { this->BackupTask(); };
        std::function<void()> pingFunc = [&]() { this->PingTask(); };

        scheduler.Schedule(saveFunc, savePeriod);
        scheduler.Schedule(backupFunc, backupPeriod);
        scheduler.Schedule(previewFunc, std::chrono::seconds(20));
        scheduler.Schedule(pingFunc, std::chrono::seconds(5));

        // Skip having to connect, change username, addemu
        /*{
            std::unique_lock<std::mutex> lk(m_QueueMutex);
            //m_WorkQueue.push(
            //        Command{kCommandType::AddEmu, {"emu1", "./core", "./rom", "Super Mario World (SNES)"}, {}, ""});
            //m_WorkQueue.push(
            //        Command{kCommandType::AddEmu, {"emu2", "./snes9x.so", "./Earthbound.smc", "Earthbound (SNES)"}, {}, ""});
            m_QueueNotifier.notify_one();
        }*/

        server->start_accept();
        server->run();

        this->Shutdown();
    } catch (websocketpp::exception const& e) {
        logger.err(e.what(), '\n');
    } catch (...) {
        throw;
    }
}

bool LetsPlayServer::OnValidate(websocketpp::connection_hdl hdl) {
    // TODO: Do bans here

    websocketpp::lib::error_code err;
    wcpp_server::connection_ptr cptr = server->get_con_from_hdl(hdl, err);

    boost::system::error_code ec;
    const auto& ep = cptr->get_raw_socket().remote_endpoint(ec);
    if (ec)
        return false;

    const boost::asio::ip::address& addr = ep.address();

    logger.log('[', addr.to_string(), "] <", hdl.lock(), "> validate");
    return true;
}

void LetsPlayServer::OnConnect(websocketpp::connection_hdl hdl) {
    {
        std::unique_lock<std::mutex> lk(m_UsersMutex);
        std::shared_ptr<LetsPlayUser> user{new LetsPlayUser};
        user->setUsername("");

        websocketpp::lib::error_code err;
        wcpp_server::connection_ptr cptr = server->get_con_from_hdl(hdl, err);

        boost::system::error_code ec;
        const auto& ep = cptr->get_raw_socket().remote_endpoint(ec);
        if (!ec) {
            const boost::asio::ip::address& addr = ep.address();
            user->setIP(addr.to_string());
        }

        logger.log('[', user->IP(), "] <", hdl.lock(), "> connect");
        logger.log('<', hdl.lock(), "> -> ", user->uuid(), " -> [", user->IP(), ']');

        m_Users[hdl] = user;
    }

    // Send available emulators
    // TODO?: Make this an internal work queue message?

    LetsPlayUserHdl user_hdl;
    decltype(m_Users)::iterator search;
    {
        std::unique_lock<std::mutex> lk(m_UsersMutex);
        search = m_Users.find(hdl);
        if (search == m_Users.end()) {
            logger.log("Couldn't find user who just joined in list\n");
            return;
        }
        user_hdl = search->second;
    }

    std::vector<EmuID_t> listMessage{"emus"};
    {
        std::unique_lock<std::mutex> lk(m_EmusMutex);
        for (const auto &emu : m_Emus) {
            listMessage.push_back(emu.first);
            listMessage.push_back(emu.second->description);
        }

        BroadcastOne(LetsPlayProtocol::encode(listMessage), hdl);
    }

    // Put a preview send request on queue
    {
        std::unique_lock<std::mutex> lk(m_QueueMutex);
        m_WorkQueue.push(Command{kCommandType::Preview, {}, hdl, ""});
        m_QueueNotifier.notify_one();
    }
}

void LetsPlayServer::OnDisconnect(websocketpp::connection_hdl hdl) {
    LetsPlayUserHdl user_hdl;
    decltype(m_Users)::iterator search;
    {
        std::unique_lock<std::mutex> lk(m_UsersMutex);
        search = m_Users.find(hdl);
        if (search == m_Users.end()) {
            logger.log("Couldn't find user who left in list\n");
            return;
        }
        user_hdl = search->second;
    }

    {
        auto user = user_hdl.lock();
        if (user && !user->connectedEmu().empty()) {
            {
                std::unique_lock<std::mutex> lk(m_EmusMutex);
                // TODO: Check if emu exists
                auto &emu = m_Emus[user->connectedEmu()];

                EmuCommand c{kEmuCommandType::UserDisconnect, user_hdl};
                {
                    std::unique_lock<std::mutex> lkk(*(emu->queueMutex));
                    emu->queue->push(c);
                }

                emu->queueNotifier->notify_one();
            }
            BroadcastToEmu(user->connectedEmu(),
                           LetsPlayProtocol::encode("leave", user->username()),
                           websocketpp::frame::opcode::text);

            logger.log(user->uuid(), " (", user->username(), ") left.");
        }
    }

    {
        std::unique_lock<std::mutex> lk(m_UsersMutex);

        // Double check is on purpose
        search = m_Users.find(hdl);
        if (search != m_Users.end()) m_Users.erase(search);
    }
}

void LetsPlayServer::sendHTTPFile(wcpp_server::connection_ptr& cptr, lib::filesystem::path file_path) {
    // TODO: Rewrite this to be not dartzcode (not that its bad or anything)
    using std::ifstream;
    ifstream file(file_path.string(), ifstream::in | ifstream::binary | ifstream::ate);
    if (file.is_open())
    {
        // Read the entire file into a string
        std::string resp_body;
        size_t size = file.tellg();
        if (size >= 0)
        {
            if (size)
            {
                resp_body.reserve();
                file.seekg(0, ifstream::beg);
                resp_body.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            }
            cptr->set_body(resp_body);
            cptr->set_status(websocketpp::http::status_code::ok);
            return;
        }
    }
}

void LetsPlayServer::OnHTTP(websocketpp::connection_hdl hdl) {
    websocketpp::lib::error_code err;
    auto cptr = server->get_con_from_hdl(hdl, err);
    if (err) return;

    const std::string ip = [&]() {
        boost::system::error_code ec;
        const auto &ep = cptr->get_raw_socket().remote_endpoint(ec);
        if (!ec)
            return ep.address().to_string();
        else
            return std::string("");
    }();

    logger.log('[', ip, "] Requested resource: ", cptr->get_resource());
    if (cptr->get_request_body().length() > 0)
        logger.log('[', ip, "] Requested body: ", cptr->get_request_body());

    std::string path = cptr->get_resource();

    if (path.size() == 0)
        return; // TODO: 404

    // Add / if none exists
    if (path[0] != '/')
        path = '/' + path;

    // Prevent path traversal
    if (path.find("..") != std::string::npos) {
        // TODO: 404
        return;
    }

    cptr->append_header("Access-Control-Allow-Origin", "*");
    const auto request = cptr->get_request();

    const std::regex emu_re{R"(\/emu\/([A-Za-z0-9]+)$)"};
    std::smatch m;

    if(request.get_method() == "GET" && (path == "/" || std::regex_match(path, m, emu_re))) { // GET / OR /emu/[id]
        std::string id;
        if(m.size() > 0) { // If in the form /emu/[id]
            id = m[1].str();

            std::unique_lock<std::mutex> lk(m_EmusMutex);
            // If any not equal to id
            if(std::all_of(m_Emus.begin(), m_Emus.end(), [id](const auto& a) {return a.first != id;})) {
                cptr->append_header("Location", "/");
            }
            lk.unlock();
        }

        // Send client
        LetsPlayServer::sendHTTPFile(cptr, lib::filesystem::path(".") / "client" / "dist" / "index.html");
    } else if(request.get_method() == "GET" && path == "/admin") {
        // TODO: Admin
        cptr->set_body("404");
        cptr->set_status(websocketpp::http::status_code::not_found);
    } else if(lib::filesystem::exists(lib::filesystem::path(".") / "client" / "dist" / path)) {
        // TODO: 404
        auto request = lib::filesystem::path(".") / "client" / "dist" / path;
        if(!lib::filesystem::is_regular_file(request)) {
            cptr->set_body("404");
            cptr->set_status(websocketpp::http::status_code::not_found);
            return;
        }

        LetsPlayServer::sendHTTPFile(cptr, request);
    }
}

void LetsPlayServer::OnMessage(websocketpp::connection_hdl hdl, wcpp_server::message_ptr msg) {
    const std::string& data = msg->get_payload();
    const auto decoded = LetsPlayProtocol::decode(data);

    if (decoded.empty()) return;
    // TODO: that switch case compile-time hash thing; its faster
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
    else if (command == "add")
        t = kCommandType::AddEmu;
    else if (command == "admin")
        t = kCommandType::Admin;
    else if (command == "addemu")
        t = kCommandType::AddEmu;
    else if (command == "shutdown")
        t = kCommandType::Shutdown;
    else if (command == "ff")
        t = kCommandType::FastForward;
    else if (command == "pong")
        t = kCommandType::Pong;
    else
        return;

    EmuID_t emuID;
    LetsPlayUserHdl user_hdl;

    {
        std::unique_lock<std::mutex> lk(m_UsersMutex);
        if (m_Users.find(hdl) != m_Users.end()) {
            user_hdl = m_Users[hdl];
            if (auto user = user_hdl.lock())
                emuID = user->connectedEmu();
        }
    }

    if (auto user = user_hdl.lock())
        logger.log(user->uuid(), " (", user->username(), ") raw: '", data, '\'');

    if (auto user = user_hdl.lock()) {
        if (t == kCommandType::Shutdown) {
            if (!user->hasAdmin)
                return;
            else
                this->Shutdown();
        }
    }

    Command c{t, std::vector<std::string>(), hdl, emuID, user_hdl};
    if (decoded.size() > 1)
        c.params = std::vector<std::string>(decoded.begin() + 1, decoded.end());

    {
        std::unique_lock<std::mutex> lk(m_QueueMutex);
        m_WorkQueue.push(c);
    }

    m_QueueNotifier.notify_one();
}

void LetsPlayServer::Shutdown() {
    // Run this function once
    static bool shuttingdown = false;

    if (!shuttingdown)
        shuttingdown = true;
    else
        return;

    // Stop the work thread loop
    m_QueueThreadRunning = false;
    logger.log("Stopping work thread...");
    {
        logger.log("Emptying the queue...");
        // Empty the queue ...
        std::unique_lock<std::mutex> lk(m_QueueMutex);
        while (!m_WorkQueue.empty()) m_WorkQueue.pop();
        // ... Except for a shutdown command
        m_WorkQueue.push(Command{kCommandType::Shutdown, std::vector<std::string>(),
                                 websocketpp::connection_hdl(), ""});
    }

    logger.log("Stopping listen...");
    // Stop listening so the queue doesn't grow any more
    websocketpp::lib::error_code err;
    server->stop_listening(err);
    if (err)
        logger.err("Error stopping listen ", err.message());
    // Wake up the turn and work threads
    logger.log("Waking up work thread...");
    m_QueueNotifier.notify_one();
    // Wait until they stop looping
    logger.log("Waiting for work thread to stop...");
    m_QueueThread.join();

    // Close every connection
    {
        logger.log("Closing every connection...");
        std::unique_lock<std::mutex> lk(m_UsersMutex);
        for (const auto &pair : m_Users) {
            const auto hdl = pair.first;
            if (!hdl.expired())
                server->close(hdl, websocketpp::close::status::normal, "Closing", err);
        }
    }
}

void LetsPlayServer::QueueThread() {
    while (m_QueueThreadRunning) {
        std::unique_lock<std::mutex> lk(m_QueueMutex);
        // Use std::condition_variable::wait Predicate?
        while (m_WorkQueue.empty()) m_QueueNotifier.wait(lk);

        if (!m_WorkQueue.empty()) {
            auto &command = m_WorkQueue.front();

            switch (command.type) {
                case kCommandType::Chat: {
                    // Chat has only one, the message
                    if (command.params.size() != 1) break;

                    if (auto user = command.user_hdl.lock()) {
                        if (user->username().empty())
                            break;

                        if (user->connectedEmu().empty())
                            break;

                        // Message only has values in the range of typeable
                        // ascii characters excluding \n and \t
                        if (!LetsPlayServer::isAsciiStr(command.params[0])) break;

                        auto maxMessageSize = config.get<std::uint64_t>(nlohmann::json::value_t::number_unsigned,
                                                                        "serverConfig", "maxMessageSize");

                        if (LetsPlayServer::escapedSize(command.params[0]) > maxMessageSize) break;

                        BroadcastToEmu(user->connectedEmu(),
                                       LetsPlayProtocol::encode("chat", user->username(), command.params[0]),
                                       websocketpp::frame::opcode::text
                        );
                        logger.log(user->uuid(), " (", user->username(), "): '", command.params[0], '\'');
                    }
                }
                    break;
                case kCommandType::Username: {
                    // Username has only one param, the username
                    if (command.params.size() != 1) break;

                    if (auto user = command.user_hdl.lock()) {
                        const auto &newUsername = command.params.at(0);
                        const auto oldUsername = user->username();

                        const bool justJoined = oldUsername.empty();

                        // Ignore no change if haven't just joined
                        if (newUsername == oldUsername && !justJoined) {
                            // Treat as invalid if they haven't just joined and they tried to request a new username
                            // that's the same as their current one
                            BroadcastOne(
                                    LetsPlayProtocol::encode("username", oldUsername, oldUsername),
                                    command.hdl);
                            logger.log(user->uuid(),
                                       " (",
                                       user->username(),
                                       ") failed username change to : '",
                                       newUsername,
                                       '\'');
                        }

                        auto maxUsernameLen = config.get<std::uint64_t>(nlohmann::json::value_t::number_unsigned,
                                                                        "serverConfig", "maxUsernameLength"),
                                minUsernameLen = config.get<std::uint64_t>(nlohmann::json::value_t::number_unsigned,
                                                                           "serverConfig", "minUsernameLength");

                        // Size based checks
                        if (newUsername.size() > maxUsernameLen
                            || newUsername.size() < minUsernameLen) {
                            if (justJoined)
                                GiveGuest(command.hdl, command.user_hdl);
                            else {
                                BroadcastOne(
                                        LetsPlayProtocol::encode("username", oldUsername, oldUsername),
                                        command.hdl);
                                logger.log(user->uuid(),
                                           " (",
                                           user->username(),
                                           ") failed username change to '",
                                           newUsername,
                                           "' due to length.");
                            }
                            break;
                        }

                        // Content based checks
                        if (newUsername.front() == ' ' || newUsername.back() == ' ' // Spaces at beginning/end
                            || !LetsPlayServer::isAsciiStr(newUsername)         // Non-ascii printable characters
                            || (newUsername.find("  ") != std::string::npos)) { // Double spaces inside username
                            if (justJoined)
                                GiveGuest(command.hdl, command.user_hdl);
                            else {
                                BroadcastOne(
                                        LetsPlayProtocol::encode("username", oldUsername, oldUsername),
                                        command.hdl);
                                logger.log(user->uuid(),
                                           " (",
                                           user->username(),
                                           ") failed username change to '",
                                           newUsername,
                                           "' due to content.");
                            }
                            break;
                        }

                        // Finally, check if username is already taken
                        if (UsernameTaken(newUsername, user->uuid())) {
                            if (justJoined)
                                GiveGuest(command.hdl, command.user_hdl);
                            else {
                                BroadcastOne(
                                        LetsPlayProtocol::encode("username", oldUsername, oldUsername),
                                        command.hdl);
                                logger.log(user->uuid(),
                                           " (",
                                           user->username(),
                                           ") failed username change to '",
                                           newUsername,
                                           "' because its already taken.");
                            }
                            break;
                        }

                        /*
                         * If all checks were passed, set username and broadcast to the person that they have a new
                         * username, and send a join/rename to everyone if the person just joined/has been around
                         */
                        user->setUsername(newUsername);

                        BroadcastOne(
                                LetsPlayProtocol::encode("username", oldUsername, newUsername),
                                command.hdl
                        );

                        logger.log(user->uuid(), " (", user->username(), ") set username to '", newUsername, '\'');

                        if (justJoined) { // Send a join message
                            BroadcastToEmu(
                                    user->connectedEmu(),
                                    LetsPlayProtocol::encode("join", user->username()),
                                    websocketpp::frame::opcode::text);

                            logger.log(user->uuid(), " (", user->username(), ") joined.");
                        } else { // Tell everyone on the emu someone changed their username
                            BroadcastToEmu(user->connectedEmu(),
                                           LetsPlayProtocol::encode("rename", oldUsername, newUsername),
                                           websocketpp::frame::opcode::text);
                            logger.log(user->uuid(),
                                       " (",
                                       user->username(),
                                       "): ",
                                       oldUsername,
                                       " is now known as ",
                                       newUsername);
                        }
                    }
                }
                    break;
                case kCommandType::List: {
                    if (!command.params.empty()) break;

                    if (auto user = command.user_hdl.lock()) {
                        logger.log(user->uuid(), " (", user->username(), ") requested a user list.");
                    }

                    std::vector<std::string> message;
                    message.emplace_back("list");

                    {
                        std::unique_lock<std::mutex> lkk(m_UsersMutex);
                        for (auto &pair : m_Users) {
                            auto &hdl = pair.first;
                            auto &user = pair.second;

                            auto commandUser = command.user_hdl.lock();
                            if (commandUser) {
                                if ((commandUser->connectedEmu() == user->connectedEmu()) &&
                                    !hdl.expired())
                                    message.push_back(user->username());
                            }
                        }
                    }

                    BroadcastOne(LetsPlayProtocol::encode(message), command.hdl);
                }
                    break;
                case kCommandType::Turn: {
                    if (!command.params.empty()) break;

                    if (auto user = command.user_hdl.lock()) {
                        logger.log(user->uuid(),
                                   " (",
                                   user->username(),
                                   ") requested a turn."
                                   "user->requestedTurn: ",
                                   (user->requestedTurn) == true,
                                   " user->connectedEmu: ",
                                   user->connectedEmu());

                        if (user->connectedEmu().empty() || user->requestedTurn)
                            break;

                        std::unique_lock<std::mutex> lkk(m_EmusMutex);
                        auto emu = m_Emus[command.emuID];
                        if (emu) {
                            user->requestedTurn = true;
                            EmuCommand c{kEmuCommandType::TurnRequest, command.user_hdl};
                            {
                                std::unique_lock<std::mutex> lkkk(*(emu->queueMutex));
                                emu->queue->push(c);
                            }

                            emu->queueNotifier->notify_one();
                        }
                    }
                }
                    break;
                case kCommandType::Shutdown:
                    break;
                case kCommandType::Connect: {
                    auto user = command.user_hdl.lock();
                    if (user) {
                        if (command.params.size() != 1 || user->username().empty()) {
                            LetsPlayServer::BroadcastOne(LetsPlayProtocol::encode("connect", false), command.hdl);
                            logger.log(user->uuid(),
                                       " (",
                                       user->username(),
                                       ") failed to connect to an emulator (1st check).");
                            break;
                        }

                        // Check if the emu that the connect thing that was sent exists
                        {
                            std::unique_lock<std::mutex> lkk(m_EmusMutex);
                            if (m_Emus.find(command.params[0]) == m_Emus.end()) {
                                LetsPlayServer::BroadcastOne(LetsPlayProtocol::encode("connect", false),
                                                             command.hdl);
                                logger.log(user->uuid(),
                                           " (",
                                           user->username(),
                                           ") tried to connect to an emulator '",
                                           command.params[0],
                                           "'that doesn't exist.");
                                break;
                            }
                        }

                        // NOTE: Can remove check and allow on the fly
                        // switching once the transition between being
                        // connected to A and being connected to B is
                        // figured out

                        if (!(user->connectedEmu().empty())) {
                            logger.log("Tried to switch emus");
                            break;
                        }

                        BroadcastToEmu(command.params[0],
                                       LetsPlayProtocol::encode("join", user->username()),
                                       websocketpp::frame::opcode::text);

                        user->setConnectedEmu(command.params[0]);

                        BroadcastOne(LetsPlayProtocol::encode("connect", true), command.hdl);

                        logger.log(user->uuid(), " (", user->username(), ") connected to ", command.params[0]);

                        auto maxUsernameLen = config.get<std::uint64_t>(nlohmann::json::value_t::number_unsigned,
                                                                        "serverConfig", "maxUsernameLength"),
                                minUsernameLen = config.get<std::uint64_t>(nlohmann::json::value_t::number_unsigned,
                                                                           "serverConfig", "minUsernameLength"),
                                maxMessageSize = config.get<std::uint64_t>(nlohmann::json::value_t::number_unsigned,
                                                                           "serverConfig", "maxMessageSize");

                        BroadcastOne(
                                LetsPlayProtocol::encode("emuinfo",
                                                         minUsernameLen,
                                                         maxUsernameLen,
                                                         maxMessageSize,
                                                         user->connectedEmu()),
                                command.hdl
                        );

                        auto &emu = m_Emus[command.params[0]];

                        EmuCommand c{kEmuCommandType::UserConnect};

                        {
                            std::unique_lock<std::mutex> lkk(*(emu->queueMutex));
                            emu->queue->push(c);
                        }

                        emu->queueNotifier->notify_one();
                    }
                }
                    break;
                case kCommandType::Button: {  // button/leftStick/rightStick, button id, value as int16
                    if (command.params.size() != 3) break;

                    {
                        auto user = command.user_hdl.lock();
                        if (user && !user->hasTurn && !user->hasAdmin) break;
                    }


                    const auto &buttonType = command.params[0];
                    std::int16_t id, value;

                    // Spaghet
                    {
                        std::stringstream ss{command.params[1]};
                        ss >> id;
                        if (!ss)
                            break;
                    }
                    {
                        std::stringstream ss{command.params[2]};
                        ss >> value;
                        if (!ss)
                            break;
                    }

                    if (auto user = command.user_hdl.lock())
                        logger.log(user->uuid(),
                                   " (",
                                   user->username(),
                                   ") sent a '",
                                   buttonType,
                                   "' update with id '",
                                   id,
                                   "' and value '",
                                   value,
                                   '\'');

                    if (id < 0)
                        break;

                    if (!command.emuID.empty()) {
                        std::unique_lock<std::mutex> lkk(m_EmusMutex);
                        if (buttonType == "button") {
                            if (id > 15)
                                break;
                            m_Emus[command.emuID]->joypad->updateValue(RETRO_DEVICE_INDEX_ANALOG_BUTTON, id, value);
                        } else if (buttonType == "leftStick") {
                            if (id > 1)
                                break;
                            m_Emus[command.emuID]->joypad->updateValue(RETRO_DEVICE_INDEX_ANALOG_LEFT, id, value);
                        } else if (buttonType == "rightStick") {
                            if (id > 1)
                                break;
                            m_Emus[command.emuID]->joypad->updateValue(RETRO_DEVICE_INDEX_ANALOG_RIGHT, id, value);
                        }
                    }
                }
                    break;
                case kCommandType::AddEmu: {  // emu, dynamic lib for the core, rom path, emu description
                    // TODO:: Add file path checks
                    if (command.params.size() != 4) break;

                    if (auto user = command.user_hdl.lock()) {
                        if (!user->hasAdmin)
                            break;
                    }

                    auto &id = command.params[0];
                    const auto &corePath = command.params[1];
                    const auto &romPath = command.params[2];
                    const auto &description = command.params[3];

                    {
                        std::unique_lock<std::mutex> lkk(m_EmuThreadMutex);
                        m_EmulatorThreads.emplace_back(
                                std::thread(EmulatorController::Run, corePath, romPath, this, id, description));
                    }

                    PreviewTask();
                }
                    break;
                case kCommandType::Admin: {
                    if (command.params.size() != 1) break;

                    if (auto user = command.user_hdl.lock()) {
                        if (user->adminAttempts >= 3)
                            break;
                    }

                    auto salt = config.get<std::string>(nlohmann::json::value_t::string, "serverConfig", "salt"),
                            expectedHash = config.get<std::string>(nlohmann::json::value_t::string, "serverConfig",
                                                                   "adminHash");

                    std::string hashed = md5(command.params[0] + salt);

                    // TODO: Log failed admin attempts

                    if (auto user = command.user_hdl.lock()) {
                        if (hashed == expectedHash) {
                            user->hasAdmin = true;
                        } else {
                            user->adminAttempts++;
                        }

                        BroadcastOne(
                                LetsPlayProtocol::encode("admin", (user->hasAdmin) == true),
                                command.hdl
                        );
                    }
                }
                    break;
                case kCommandType::Pong:
                    if (auto user = command.user_hdl.lock())
                        user->updateLastPong();
                    break;
                case kCommandType::FastForward: {
                    {
                        auto user = command.user_hdl.lock();
                        if (user && !user->hasTurn && !user->hasAdmin) break;
                    }
                    std::unique_lock<std::mutex> lkk(m_EmusMutex);
                    auto emu = m_Emus[command.emuID];
                    if (emu) {
                        EmuCommand c{kEmuCommandType::FastForward};
                        {
                            std::unique_lock<std::mutex> lkkk(*(emu->queueMutex));
                            emu->queue->push(c);
                        }

                        emu->queueNotifier->notify_one();
                    }

                }
                    break;
                case kCommandType::Preview: {
                    std::unique_lock<std::mutex> lkk(m_PreviewsMutex);
                    for (const auto &preview : m_Previews) {
                        websocketpp::lib::error_code ec;
                        server->send(command.hdl, preview.second.data(), preview.second.size(),
                                     websocketpp::frame::opcode::binary, ec);
                    }
                }
                case kCommandType::RemoveEmu:
                case kCommandType::StopEmu:
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

void LetsPlayServer::GeneratePreview(const EmuID_t &id) {
    auto index = std::distance(m_Emus.begin(), m_Emus.find(id));
    auto jpegData = GenerateEmuJPEG(id);

    // Set binary payload info
    jpegData[0] = index | (kBinaryMessageType::Preview << 5);

    m_Previews[id] = jpegData;
}

void LetsPlayServer::PingTask() {
    for (auto &pair : m_Users) {
        auto &hdl = pair.first;
        auto &user = pair.second;
        websocketpp::lib::error_code ec;

        // Check if should d/c
        if (user->shouldDisconnect()) {
            server->close(hdl, websocketpp::close::status::normal, "Timed out.", ec);
            continue;
        }

        // Send a ping if not
        if (!hdl.expired())
            server->send(hdl, LetsPlayProtocol::encode("ping"), websocketpp::frame::opcode::text);
    }
}

void LetsPlayServer::PreviewTask() {
    std::unique_lock<std::mutex> lk(m_EmusMutex);

    // Tell all the emulators to update their own preview thumbnails
    for (auto &p : m_Emus) {
        auto &emu = p.second;

        EmuCommand c{kEmuCommandType::GeneratePreview};

        {
            std::unique_lock<std::mutex> lkk(*(emu->queueMutex));
            emu->queue->push(c);
        }

        emu->queueNotifier->notify_one();
    }
}

void LetsPlayServer::BroadcastAll(const std::string& data, websocketpp::frame::opcode::value op) {
    std::unique_lock<std::mutex> lk(m_UsersMutex, std::try_to_lock);
    for (auto &pair : m_Users) {
        auto &hdl = pair.first;
        auto &user = pair.second;

        websocketpp::lib::error_code ec;
        if (!user->username().empty() && user->connected && !hdl.expired())
            server->send(hdl, data, op, ec);
    }
}

void LetsPlayServer::BroadcastOne(const std::string&& data, websocketpp::connection_hdl hdl) {
    websocketpp::lib::error_code ec;
    server->send(hdl, data, websocketpp::frame::opcode::text, ec);
}

void LetsPlayServer::BroadcastToEmu(const EmuID_t& id, const std::string& message,
                                    websocketpp::frame::opcode::value op) {
    std::unique_lock<std::mutex> lk(m_UsersMutex, std::try_to_lock);
    for (auto &pair : m_Users) {
        auto &hdl = pair.first;
        auto &user = pair.second;

        websocketpp::lib::error_code ec;
        if (user->connectedEmu() == id && !user->username().empty() && user->connected
            && !hdl.expired())
            server->send(hdl, message, op, ec);
    }
}

void LetsPlayServer::GiveGuest(websocketpp::connection_hdl hdl, LetsPlayUserHdl user_hdl) {
    // TODO: Custom guest usernames? (i.e. being able to specify player##### in config)
    if (auto user = user_hdl.lock()) {
        std::string validUsername;
        do {
            validUsername = "guest";
            validUsername += std::to_string(rnd::nextInt() % 100000);
        } while (UsernameTaken(validUsername, user->uuid()));

        const std::string oldUsername = user->username();
        user->setUsername(validUsername);
        // Send valid username
        BroadcastOne(
            LetsPlayProtocol::encode("username", oldUsername, validUsername),
            hdl
        );
        logger.log(user->uuid(), " (", oldUsername, ") given new username '", user->username(), '\'');
    }
}

bool LetsPlayServer::UsernameTaken(const std::string& username, const std::string& uuid) {
    std::unique_lock<std::mutex> lkk(m_UsersMutex);
    for (auto &pair : m_Users) {
        auto &hdl = pair.first;
        auto &user = pair.second;

        if (user->uuid() != uuid &&
            user->username() == username && user->connected && !hdl.expired()) {
            return true;
        }
    }
    return false;
}

void LetsPlayServer::SetupLetsPlayDirectories() {
    /* Example dir setup
     * Looks for config.json in ($XDG_CONFIG_HOME || ~/.config)/letsplay/ (will be %AppData%\Lets Play\config.json on windows)
     * dataDir saved to ($XDG_DATA_HOME || $HOME/.local/share)/letsplay/ (will be %AppData%\Lets Play\Data on windows)
     *
     * dataDir/
     *      system/ (catch-all for throwing in bios and stuff
     *          gba_bios.bin
     *          snes_bios.bin
     *      emulators/ (folder for all emulators)
     *          emu1/ (example)
     *              state01.dat
     *          snes/ (example)
     *              save.frz
     *      cores/ (dir for looking for cores to load, autopopulate in the future???)
     *          snes9x.so
     *      roms/ (dir to search for roms
     *          Earthbound.smc
     *          Super\ Mario\ Advance\ 2.gba
     *
    */
    auto dataDir = config.get<std::string>(nlohmann::json::value_t::string, "serverConfig", "dataDirectory");

    lib::filesystem::path dataPath;
    if (dataDir == "System Default") {
        const char *cXDGDataHome = std::getenv("XDG_DATA_HOME");
        if (cXDGDataHome) { // Using XDG standard
            dataPath = lib::filesystem::path(cXDGDataHome) / "letsplay";
        } else { // If not, fall back to what XDG *would* use
            dataPath = lib::filesystem::path(std::getenv("HOME")) / ".local" / "share" / "letsplay";
        }
    } else {
        dataPath = dataDir;
    }

    lib::filesystem::create_directories(systemDirectory = dataPath / "system");
    lib::filesystem::create_directories(emuDirectory = dataPath / "emulators");
    lib::filesystem::create_directories(romDirectory = dataPath / "roms");
    lib::filesystem::create_directories(coreDirectory = dataPath / "cores");
}

void LetsPlayServer::SaveTask() {
    std::unique_lock<std::mutex> lk(m_EmusMutex);

    for (auto &p : m_Emus) {
        auto &emu = p.second;

        EmuCommand c{kEmuCommandType::Save};

        {
            std::unique_lock<std::mutex> lkk(*(emu->queueMutex));
            emu->queue->push(c);
        }

        emu->queueNotifier->notify_one();
    }
}

void LetsPlayServer::BackupTask() {
    std::unique_lock<std::mutex> lk(m_EmusMutex);

    for (auto &p : m_Emus) {
        auto &emu = p.second;

        EmuCommand c{kEmuCommandType::Backup};

        {
            std::unique_lock<std::mutex> lkk(*(emu->queueMutex));
            emu->queue->push(c);
        }

        emu->queueNotifier->notify_one();
    }
}

void LetsPlayServer::AddEmu(const EmuID_t& id, EmulatorControllerProxy *emu) {
    std::unique_lock<std::mutex> lk(m_EmusMutex);
    m_Emus[id] = emu;
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

std::vector<std::uint8_t> LetsPlayServer::GenerateEmuJPEG(const EmuID_t &id) {
    thread_local static tjhandle _jpegCompressor = tjInitCompress();
    thread_local static long unsigned int _jpegBufferSize = 20000000;
    thread_local static std::vector<std::uint8_t> jpegData(20000000); // 20MB jpeg buffer
    thread_local static unsigned i{0};
    thread_local static auto quality = config.get<std::uint64_t>(nlohmann::json::value_t::number_unsigned,
                                                                 "serverConfig", "jpegQuality");
    Frame frame = [&]() {
        // Possible race condition, unlocked m_EmusMutex
        auto emu = m_Emus[id];
        return emu->getFrame();
    }();

    // currentBuffer was nullptr
    if (frame.width == 0 || frame.height == 0) return std::vector<std::uint8_t>{0, 2};

    // update quality value from config every 120 frames
    if ((++i %= 120) == 0) {
        auto q = config.get<std::uint64_t>(nlohmann::json::value_t::number_unsigned, "serverConfig", "jpegQuality");

        if (q > 100 || q < 1) quality = 95;
        else quality = q;
    }

    long unsigned int jpegSize = _jpegBufferSize;
    std::uint8_t *cjpegData = &jpegData[1];
    tjCompress2(_jpegCompressor, frame.data.data(), frame.width, frame.width * 3, frame.height,
                TJPF_RGB, &cjpegData, &jpegSize, TJSAMP_420, quality, TJFLAG_ACCURATEDCT);

    std::vector<std::uint8_t> slicedData(std::begin(jpegData), std::next(jpegData.begin(), jpegSize + 1));

    return slicedData;
}

void LetsPlayServer::SendFrame(const EmuID_t& id) {
    // Skip if no users
    {
        std::unique_lock<std::mutex> lk(m_UsersMutex);
        bool hasUsers{false};
        for (auto &pair : m_Users) {
            if (pair.second->connectedEmu() == id)
                hasUsers = true;
        }

        if (!hasUsers)
            return;
    }

    auto jpegData = GenerateEmuJPEG(id);

    // Mark as screen message
    jpegData[0] = 0 | (kBinaryMessageType::Screen << 5);

    std::unique_lock<std::mutex> lk(m_UsersMutex);
    for (auto &pair : m_Users) {
        auto &hdl = pair.first;
        auto &user = pair.second;

        if (user->connectedEmu() == id && user->connected && !hdl.expired()) {
            websocketpp::lib::error_code ec;
            server->send(hdl, jpegData.data(), jpegData.size(), websocketpp::frame::opcode::binary, ec);
        }
    }
}

std::string LetsPlayServer::escapeTilde(std::string str) {
    if (str.front() == '~') {
        const char *homePath = std::getenv("HOME");
        if (!homePath) {
            throw std::invalid_argument("Tilde path was specified but couldn't retrieve "
                                        "actual home path. Check if $HOME was declared.\n");

        }

        str.erase(0, 1);
        str.insert(0, homePath);
    }
    return str;
}

