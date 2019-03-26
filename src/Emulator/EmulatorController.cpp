#include "EmulatorController.h"

EmuID_t EmulatorController::id;
std::string EmulatorController::coreName;
LetsPlayServer *EmulatorController::m_server{nullptr};
EmulatorControllerProxy EmulatorController::proxy;
RetroCore EmulatorController::Core;
char *EmulatorController::romData{nullptr};

std::vector<LetsPlayUserHdl> EmulatorController::m_TurnQueue;
std::mutex EmulatorController::m_TurnMutex;
std::condition_variable EmulatorController::m_TurnNotifier;
std::atomic<bool> EmulatorController::m_TurnThreadRunning;
std::thread EmulatorController::m_TurnThread;
RetroPad EmulatorController::joypad;

VideoFormat EmulatorController::m_videoFormat;
const void *EmulatorController::m_currentBuffer{nullptr};
std::mutex EmulatorController::m_videoMutex;
retro_system_av_info EmulatorController::m_avinfo;

lib::filesystem::path EmulatorController::dataDirectory;
lib::filesystem::path EmulatorController::saveDirectory;
std::string EmulatorController::saveDirString;

std::shared_timed_mutex EmulatorController::m_generalMutex;

void EmulatorController::Run(const std::string& corePath, const std::string& romPath,
                             LetsPlayServer *server, EmuID_t t_id) {
    lib::filesystem::path coreFile = corePath, romFile = romPath;
    if (!lib::filesystem::is_regular_file(coreFile)) {
        server->logger.err("Provided core path '", corePath, "' was invalid.");
        return;
    }
    if (!lib::filesystem::is_regular_file(romFile)) {
        server->logger.err("Provided rom path '", romPath, "' was not valid.");
        return;
    }
    server->logger.log("Starting up ", t_id, "...");

    Core.Load(coreFile.string().c_str());
    m_server = server;
    id = t_id;
    proxy = EmulatorControllerProxy{AddTurnRequest, UserDisconnected, UserConnected, GetFrame,
                                    false, &joypad, Save, Backup};
    m_server->AddEmu(id, &proxy);

    // Add emu specific config if it doesn't already exist
    {
        std::unique_lock<std::shared_timed_mutex> lk(server->config.mutex);
        if (!server->config.config["serverConfig"]["emulators"].count(t_id)) {
            server->config.config["serverConfig"]["emulators"][t_id] =
                LetsPlayConfig::defaultConfig["serverConfig"]["emulators"]["template"];
        }
    }

    // Create emu folder if it doesn't already exist
    lib::filesystem::create_directories(dataDirectory = m_server->emuDirectory / id);
    lib::filesystem::create_directories(dataDirectory / "history");
    lib::filesystem::create_directories(dataDirectory / "backups");
    lib::filesystem::create_directories(saveDirectory = dataDirectory / "saves");

    server->config.SaveConfig();

    Core.SetEnvironment(OnEnvironment);
    Core.SetVideoRefresh(OnVideoRefresh);
    Core.SetInputPoll(OnPollInput);
    Core.SetInputState(OnGetInputState);
    Core.SetAudioSample(OnLRAudioSample);
    Core.SetAudioSampleBatch(OnBatchAudioSample);
    Core.Init();

    m_server->logger.log(id, ": Finished initialization.");

    retro_game_info info = {romPath.c_str(), nullptr, static_cast<size_t>(lib::filesystem::file_size(romFile)), nullptr};
    std::ifstream fo(romFile.string(), std::ios::binary);

    retro_system_info system{};
    Core.GetSystemInfo(&system);

    if (!system.need_fullpath) {
        romData = new char[lib::filesystem::file_size(romFile)];
        info.data = static_cast<void *>(romData);

        if (!info.data) {
            m_server->logger.err(id, ": Failed to allocate memory for the ROM");
            return;
        }

        if (!fo.read(romData, lib::filesystem::file_size(romFile))) {
            m_server->logger.err(id, ": Failed to load data from the file. Do you have the correct access rights?");
            return;
        }
    }

    // TODO: compressed roms and stuff

    if (!Core.LoadGame(&info)) {
        m_server->logger.err(id, ": Failed to load game. Was the rom the correct file type?");
        return;
    }

    // Load state if applicable
    Load();

    m_TurnThreadRunning = true;
    m_TurnThread = std::thread(EmulatorController::TurnThread);

    Core.GetAudioVideoInfo(&m_avinfo);
    std::uint64_t fps = -1ull;

    auto &config = m_server->config;
    if (config.get<bool>(nlohmann::json::value_t::boolean, "serverConfig", "emulators", id, "overrideFramerate")) {
        fps = config.get<std::uint64_t>(nlohmann::json::value_t::number_unsigned, "serverConfig", "emulators", id,
                                        "fps");
    }


    // TODO: Manage this thread
    if (fps != -1ull) {
        std::thread t([&]() {
            using namespace std::chrono;

            while (true) {
                server->SendFrame(id);
                std::this_thread::sleep_for(
                    milliseconds(static_cast<long int>((1.0 / fps) * 1000)));
            }
        });
        t.detach();
    }

    unsigned msWait = (1.0 / m_avinfo.timing.fps) * 1000;
    proxy.isReady = true;
    std::chrono::time_point<std::chrono::steady_clock> wait_time =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(msWait);
    while (true) {
        std::this_thread::sleep_until(wait_time);
        wait_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(msWait);
        Core.Run();
        if (fps == -1ull) m_server->SendFrame(id);
    }
}

bool EmulatorController::OnEnvironment(unsigned cmd, void *data) {
    auto &config = m_server->config;
    switch (cmd) {
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
            const retro_pixel_format *fmt = static_cast<retro_pixel_format *>(data);

            if (*fmt > RETRO_PIXEL_FORMAT_RGB565) return false;

            return SetPixelFormat(*fmt);
        }
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: // system dir, dataDir / system
            *static_cast<const char **>(data) = m_server->systemDirectory.string().c_str();
            break;
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: // save dir, dataDir / emulators / emu_id / saves
            saveDirString = saveDirectory.string();
            *static_cast<const char **>(data) = saveDirString.c_str();
            break;
        case RETRO_ENVIRONMENT_GET_USERNAME:
            *static_cast<const char **>(data) = id.c_str();
            break;
        case RETRO_ENVIRONMENT_GET_OVERSCAN: // We don't (usually) want overscan
            return false;
            // Will be implemented
        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: // I think this is called when the avinfo changes
        case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH: // Path to the libretro so core
        case RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK: // Use this instead of sleep_until?
        case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE: // For rumble support for later on
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: // See core logs
        case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY: // Where assets are stored
        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: // Use to see if the core recognizes the retropad (if it doesn't well....)
        case RETRO_ENVIRONMENT_GET_LANGUAGE: // Some cores might use this and its simple to add
        case RETRO_ENVIRONMENT_GET_VFS_INTERFACE: // Some cores use this and it wouldn't be hard to implement with fstream and filesystem being a thing
        case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE: // Some cores might not use audio, so don't even bother with sending the audio streams
        case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE: // Might want this for support for more hardware accelerated cores
        default:return false;
            // clang-format on
    }
    return true;
}

void EmulatorController::OnVideoRefresh(const void *data, unsigned width, unsigned height,
                                        size_t pitch) {
    std::unique_lock<std::mutex> lk(m_videoMutex);
    if (width != m_videoFormat.width || height != m_videoFormat.height ||
        pitch != m_videoFormat.pitch) {
        std::clog << "Screen Res changed from " << m_videoFormat.width << 'x'
                  << m_videoFormat.height << " to " << width << 'x' << height << ' ' << pitch
                  << '\n';
        m_videoFormat.width = width;
        m_videoFormat.height = height;
        m_videoFormat.pitch = pitch;
    }
    m_currentBuffer = data;
}

void EmulatorController::OnPollInput() {}

std::int16_t EmulatorController::OnGetInputState(unsigned port, unsigned device, unsigned index,
                                                 unsigned id) {
    if (port != 0)
        return 0;

    switch (device) {
        case RETRO_DEVICE_JOYPAD:
            return joypad.isPressed(id);
        case RETRO_DEVICE_ANALOG:
            return joypad.analogValue(index, id);
        default:
            return 0;
    }
}

void EmulatorController::OnLRAudioSample(std::int16_t /*left*/, std::int16_t /*right*/) {}

size_t EmulatorController::OnBatchAudioSample(const std::int16_t */*data*/, size_t frames) {
    return frames;
}

void EmulatorController::TurnThread() {
    while (m_TurnThreadRunning) {
        std::unique_lock<std::mutex> lk(m_TurnMutex);

        // Wait for a nonempty queue
        while (m_TurnQueue.empty()) {
            m_TurnNotifier.wait(lk);
        }

        if (m_TurnThreadRunning == false) break;

        // While someone in the turn queue has disconnected
        while (m_TurnQueue[0].expired() || !m_TurnQueue[0].lock()->connected) {
            if (m_TurnQueue.empty())
                break;
            m_TurnQueue.erase(m_TurnQueue.begin());
        }

        EmulatorController::SendTurnList();

        // If everyone in the turn queue left and was removed by the above section's code, restart the turn loop at the top
        if (m_TurnQueue.empty())
            continue;

        if (auto currentUser = m_TurnQueue[0].lock()) {
            currentUser->hasTurn = true;

            // TOOD: Fallback on template instead of id if invalid value? What happens to the value if its not in the emu's config?
            const auto turnLength = m_server->config.get<std::uint64_t>(nlohmann::json::value_t::number_unsigned,
                                                                        "serverConfig", "emulators", id, "turnLength");
            const auto turnEnd = std::chrono::steady_clock::now() + std::chrono::milliseconds(turnLength);

            while ((currentUser.use_count() > 1) && currentUser->connected && currentUser->hasTurn
                   && (std::chrono::steady_clock::now() < turnEnd)) {
                /* FIXME?: does turnEnd - std::chrono::steady_clock::now() cause
                 * underflow or UB for the case where now() is greater than
                 * turnEnd? */
                m_TurnNotifier.wait_for(lk, turnEnd - std::chrono::steady_clock::now());
            }

            if (m_TurnQueue.size() > 1) {
                currentUser->hasTurn = false;
                currentUser->requestedTurn = false;
            }

        }

        if (m_TurnQueue.size() > 1)
            m_TurnQueue.erase(m_TurnQueue.begin());

        EmulatorController::SendTurnList();
    }
}

void EmulatorController::AddTurnRequest(LetsPlayUserHdl user_hdl) {
    // Add user to the list
    std::unique_lock<std::mutex> lk(m_TurnMutex);
    m_TurnQueue.emplace_back(user_hdl);

    // Send off updated turn list
    EmulatorController::SendTurnList();

    // Wake up the turn thread to manage the changes
    m_TurnNotifier.notify_one();
}

void EmulatorController::SendTurnList() {
    const std::string turnList = [&] {
        // Majority of the time this won't lock because m_TurnMutex will have already been locked by the caller
        std::unique_lock<std::mutex> lk(m_TurnMutex, std::try_to_lock);

        std::vector<std::string> names{"turns"};
        for (auto user_hdl : m_TurnQueue) {
            // If pointer hasn't been deleted and user is still connected
            auto user = user_hdl.lock();
            if (user && user->connected)
                names.push_back(user->username());
        }
        return LetsPlayProtocol::encode(names);
    }();

    m_server->BroadcastToEmu(id, turnList, websocketpp::frame::opcode::text);
}

void EmulatorController::UserDisconnected(LetsPlayUserHdl user_hdl) {
    // Update flag in case the turn queue gets to the user before its removed from memory in m_server
    if (auto user = user_hdl.lock())
        user->connected = false;

    // Wake up the turn thread in case that person had a turn when they disconnected
    m_TurnNotifier.notify_one();
}

void EmulatorController::UserConnected(LetsPlayUserHdl) {
}

bool EmulatorController::SetPixelFormat(const retro_pixel_format fmt) {
    switch (fmt) {
        // TODO: Find a core that uses this and test it
        case RETRO_PIXEL_FORMAT_0RGB1555:  // 16 bit
            // rrrrrgggggbbbbba
            std::clog << "0RGB1555" << '\n';
            m_videoFormat.rMask = 0b1111100000000000;
            m_videoFormat.gMask = 0b0000011111000000;
            m_videoFormat.bMask = 0b0000000000111110;
            m_videoFormat.aMask = 0b0000000000000000;

            m_videoFormat.rShift = 10;
            m_videoFormat.gShift = 5;
            m_videoFormat.bShift = 0;
            m_videoFormat.aShift = 15;

            m_videoFormat.bitsPerPel = 16;
            return true;
            // TODO: Fix (find a core that uses this, bsnes accuracy gives a zeroed
            // out video buffer so thats a no go)
        case RETRO_PIXEL_FORMAT_XRGB8888:  // 32 bit
            std::clog << "XRGB8888\n";
            m_videoFormat.rMask = 0xff000000;
            m_videoFormat.gMask = 0x00ff0000;
            m_videoFormat.bMask = 0x0000ff00;
            m_videoFormat.aMask = 0x00000000;  // normally 0xff but who cares about alpha

            m_videoFormat.rShift = 16;
            m_videoFormat.gShift = 8;
            m_videoFormat.bShift = 0;
            m_videoFormat.aShift = 24;

            m_videoFormat.bitsPerPel = 32;
            return true;
        case RETRO_PIXEL_FORMAT_RGB565:  // 16 bit
            // rrrrrggggggbbbbb
            std::clog << "RGB656\n";
            m_videoFormat.rMask = 0b1111100000000000;
            m_videoFormat.gMask = 0b0000011111100000;
            m_videoFormat.bMask = 0b0000000000011111;
            m_videoFormat.aMask = 0b0000000000000000;

            m_videoFormat.rShift = 11;
            m_videoFormat.gShift = 5;
            m_videoFormat.bShift = 0;
            m_videoFormat.aShift = 16;

            m_videoFormat.bitsPerPel = 16;
            return true;
        default:
            return false;
    }
}

Frame EmulatorController::GetFrame() {
    std::unique_lock<std::mutex> lk(m_videoMutex);
    if (m_currentBuffer == nullptr) return Frame{0, 0, {}};

    std::vector<std::uint8_t> outVec(m_videoFormat.width * m_videoFormat.height * 3);

    size_t j{0};

    const auto *i = static_cast<const std::uint8_t *>(m_currentBuffer);
    for (size_t h = 0; h < m_videoFormat.height; ++h) {
        for (size_t w = 0; w < m_videoFormat.width; ++w) {
            std::uint32_t pixel{0};
            // Assuming little endian
            pixel |= *(i++);
            pixel |= *(i++) << 8;

            if (m_videoFormat.bitsPerPel == 32) {
                pixel |= *(i++) << 16;
                pixel |= *(i++) << 24;
            }

            // Calculate the rgb 0 - 255 values
            const std::uint8_t& rMax = 1 << (m_videoFormat.aShift - m_videoFormat.rShift);
            const std::uint8_t& gMax = 1 << (m_videoFormat.rShift - m_videoFormat.gShift);
            const std::uint8_t& bMax = 1 << (m_videoFormat.gShift - m_videoFormat.bShift);

            const std::uint8_t& rVal = (pixel & m_videoFormat.rMask) >> m_videoFormat.rShift;
            const std::uint8_t& gVal = (pixel & m_videoFormat.gMask) >> m_videoFormat.gShift;
            const std::uint8_t& bVal = (pixel & m_videoFormat.bMask) >> m_videoFormat.bShift;

            std::uint8_t rNormalized = (rVal / (double) rMax) * 255;
            std::uint8_t gNormalized = (gVal / (double) gMax) * 255;
            std::uint8_t bNormalized = (bVal / (double) bMax) * 255;

            outVec[j++] = rNormalized;
            outVec[j++] = gNormalized;
            outVec[j++] = bNormalized;
        }
        i += m_videoFormat.pitch - 2 * m_videoFormat.width;
    }

    return Frame{m_videoFormat.width, m_videoFormat.height, outVec};
}

void EmulatorController::Save() {
    std::unique_lock <std::shared_timed_mutex> lk(m_generalMutex);
    auto size = Core.SaveStateSize();

    if (size == 0) { // Not supported by the loaded core
        m_server->logger.log(id, ": Warning; Saving for this core unsupported. Skipping save procedure.");
        return;
    }

    std::vector<unsigned char> saveData(size);

    if (!Core.SaveState(saveData.data(), size)) {
        m_server->logger.log(id, ": Warning; Failed to serialize data with size ", size, ".");
        return;
    }

    auto newSaveFile = dataDirectory / "history" / "current.state";

    if (lib::filesystem::exists(newSaveFile)) { // Move current file to a backup if if exists
        m_server->logger.log(id, ": Existing state detected; Moving to new state.");
        namespace chrono = std::chrono;

        auto tp = chrono::system_clock::now().time_since_epoch();
        auto timestamp = std::to_string(chrono::duration_cast<chrono::seconds>(tp).count());

        auto backupName = dataDirectory / "history" / (timestamp + ".state");

        m_server->logger.log(id, ": Moved current state to ", backupName.string());

        lib::filesystem::rename(newSaveFile, backupName);
    }

    // Remove old temporaries
    {
        std::vector<lib::filesystem::path> temporaryStates;
        for (auto &p : lib::filesystem::directory_iterator(dataDirectory / "history")) {
            auto &path = p.path();

            if (lib::filesystem::is_regular_file(path) && path.extension() == ".state" && path.filename() != "current")
                temporaryStates.push_back(path);
        }

        auto maxHistorySize = m_server->config.get<std::uint64_t>(nlohmann::json::value_t::number_unsigned,
                                                                  "serverConfig", "backups", "maxHistorySize");
        if (temporaryStates.size() > maxHistorySize) {
            // Sort by filename
            std::sort(temporaryStates.begin(), temporaryStates.end(), [](const auto &a, const auto &b) {
                return a.string() < b.string();
            });

            // Delete the oldest file
            m_server->logger.log(id, ": Over threshold; Removing ", temporaryStates.front().string());
            lib::filesystem::remove(temporaryStates.front());
        }

    }

    std::ofstream fo(newSaveFile.string(), std::ios::binary);
    fo.write(reinterpret_cast<char *>(saveData.data()), size);
}

void EmulatorController::Backup() {
    if (!lib::filesystem::exists(
            dataDirectory / "history" / "current.state")) // Create a current.state save if none exists
        Save();

    std::unique_lock<std::shared_timed_mutex> lk(m_generalMutex);

    namespace chrono = std::chrono;
    auto tp = chrono::system_clock::now().time_since_epoch();
    auto timestamp = std::to_string(chrono::duration_cast<chrono::seconds>(tp).count());

    // Copy any emulator generated files over
    auto currentBackup = dataDirectory / "backups" / timestamp;

    // *Almost* perfect compatibility between boost::filesystem and std::filesystem!
#ifdef USE_BOOST_FILESYSTEM
    std::function<void(const lib::filesystem::path &, const lib::filesystem::path &)> recursive_copy;
    recursive_copy = [&recursive_copy](const lib::filesystem::path &src, const lib::filesystem::path &dst) {
        if (lib::filesystem::exists(dst)) {
            return;
        }

        if (lib::filesystem::is_directory(src)) {
            lib::filesystem::create_directories(dst);
            for (auto &item : lib::filesystem::directory_iterator(src)) {
                recursive_copy(item.path(), dst / item.path().filename());
            }
        } else if (lib::filesystem::is_regular_file(src)) {
            lib::filesystem::copy(src, dst);
        }
    };

    if (!lib::filesystem::is_empty(saveDirectory))
        recursive_copy(saveDirectory, currentBackup);
#else
    lib::filesystem::copy(saveDirectory, currentBackup, lib::filesystem::copy_options::recursive);
#endif

    // Copy current history state over
    lib::filesystem::copy(dataDirectory / "history" / "current.state", currentBackup);
}

void EmulatorController::Load() {
    std::unique_lock <std::shared_timed_mutex> lk(m_generalMutex);
    auto saveFile = dataDirectory / "history" / "current.state";

    if (!lib::filesystem::exists(saveFile)) return; // Hasn't saved yet, so don't try to load it

    auto saveFileSize = lib::filesystem::file_size(saveFile);
    std::vector<unsigned char> saveData(saveFileSize);

    std::ifstream fi(saveFile.string(), std::ios::binary);
    fi.read(reinterpret_cast<char *>(saveData.data()), saveFileSize);

    Core.LoadState(saveData.data(), saveFileSize);
}