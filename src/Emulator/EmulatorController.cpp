#include "EmulatorController.h"

namespace EmulatorController {
    /**
     * ID of the emulator controller / emulator.
     */
    static thread_local EmuID_t id;

    /**
     * Name of the library that is loaded (mGBA, Snes9x, bsnes, etc).
     *
     * @todo Grab the info from the loaded core and populate this field.
     */
    static thread_local std::string coreName;

    /**
     * Pointer to the server managing the emulator controller
     */
    static thread_local LetsPlayServer *server{nullptr};

    /**
     * Pointer to some functions that the managing server needs to call.
     */
    static thread_local EmulatorControllerProxy proxy;

    /**
     * The object that manages the libretro lower level functions. Used mostly
     * for loading symbols and storing function pointers.
     */
    static thread_local RetroCore Core;

    /**
     * Rom data if loaded from file.
     */
    static thread_local char *romData{nullptr};

    /**
     * Turn queue for this emulator
     */
    static thread_local std::vector <LetsPlayUserHdl> turnQueue;

    /**
     * Turn queue mutex
     */
    static thread_local std::mutex turnMutex;

    /**
     * The joypad object storing the button state.
     */
    static thread_local RetroPad joypad;

    /**
     * Stores the masks and shifts required to generate a rgb 0xRRGGBB
     * vector from the video_refresh callback data.
     */
    static thread_local VideoFormat videoFormat;

    /**
     * Pointer to the current video buffer.
     */
    static thread_local const void *currentBuffer{nullptr};

    /**
     * Mutex for accessing m_screen or m_nextFrame or updating the buffer.
     */
    static thread_local std::mutex videoMutex;

    /**
     * libretro API struct that stores audio-video information.
     */
    static thread_local retro_system_av_info avinfo;

    /**
     * Whether or not this emulator is fast forwarded
     */
    static thread_local std::atomic<bool> fastForward{false};

    /**
     * Timepoint of the last fastForward toggle. Used to prevent (over|ab)use.
     */
    static thread_local std::chrono::time_point <std::chrono::steady_clock> lastFastForward;

    /**
     * Location of the emulator directory, loaded from config.
     */
    static thread_local lib::filesystem::path dataDirectory;

    /**
     * Given to the core as the save directory
     */
    static thread_local lib::filesystem::path saveDirectory;

    /**
     * String representation of saveDirectory. Storing as string to prevent dangling pointer in OnEnvironment.
     */
    static thread_local std::string saveDirString;

    /**
     * General mutex for things that won't really go off at once and get blocked.
     */
    static thread_local std::shared_timed_mutex generalMutex;


    /*
     * --- Work Queue Stuff ---
     */

    /**
     * Mutex for the condition variable
     */
    static thread_local std::mutex queueMutex;

    /**
     * Condition variable used to wait for new messages
     */
    static thread_local std::condition_variable queueNotifier;

    /**
     * Queue thread running variable. Set to false and wakeup queueNotifier to stop the queue thread
     */
    static thread_local std::atomic<bool> queueRunning{false};

    /**
     * Work queue
     */
    static thread_local std::queue <EmuCommand> workQueue;

    /**
     * Queue thread
     */
    static thread_local std::thread queueThread;
}


void EmulatorController::Run(const std::string& corePath, const std::string& romPath,
                             LetsPlayServer *t_server, EmuID_t t_id, const std::string &description) {
    lib::filesystem::path coreFile = corePath, romFile = romPath;
    if (!lib::filesystem::is_regular_file(coreFile)) {
        server->logger.err("Provided core path '", corePath, "' was invalid.");
        return;
    }
    if (!lib::filesystem::is_regular_file(romFile)) {
        server->logger.err("Provided rom path '", romPath, "' was not valid.");
        return;
    }
    t_server->logger.log("Starting up ", t_id, "...");

    Core.Load(coreFile.string().c_str());
    server = t_server;
    id = t_id;
    proxy = EmulatorControllerProxy{&workQueue, &queueMutex, &queueNotifier, GetFrame, &joypad, description};
    server->AddEmu(id, &proxy);

    // Add emu specific config if it doesn't already exist
    std::cout << "Getting" << '\n';
    auto emuConfigs = server->config.get<nlohmann::json>(nlohmann::json::value_t::array, "serverConfig", "emulators");
    std::cout << emuConfigs << '\n';
    if(!emuConfigs.count(id)) {
        std::cout << "No count, setting" << '\n';
        auto emuTemplate = server->config.get<nlohmann::json>(nlohmann::json::value_t::object, "serverConfig", "emulators", "template");
        server->config.set("serverConfig", "emulators", id, emuTemplate);
    }

    // Create emu folder if it doesn't already exist
    lib::filesystem::create_directories(dataDirectory = server->emuDirectory / id);
    lib::filesystem::create_directories(dataDirectory / "history");
    lib::filesystem::create_directories(dataDirectory / "backups" / "states");
    lib::filesystem::create_directories(saveDirectory = dataDirectory / "saves");

    server->config.SaveConfig();

    Core.SetEnvironment(OnEnvironment);
    Core.SetVideoRefresh(OnVideoRefresh);
    Core.SetInputPoll(OnPollInput);
    Core.SetInputState(OnGetInputState);
    Core.SetAudioSample(OnLRAudioSample);
    Core.SetAudioSampleBatch(OnBatchAudioSample);
    Core.Init();

    server->logger.log(id, ": Finished initialization.");

    retro_game_info info = {romPath.c_str(), nullptr, static_cast<size_t>(lib::filesystem::file_size(romFile)), nullptr};
    std::ifstream fo(romFile.string(), std::ios::binary);

    retro_system_info system{};
    Core.GetSystemInfo(&system);

    if (!system.need_fullpath) {
        romData = new char[lib::filesystem::file_size(romFile)];
        info.data = static_cast<void *>(romData);

        if (!info.data) {
            server->logger.err(id, ": Failed to allocate memory for the ROM");
            return;
        }

        if (!fo.read(romData, lib::filesystem::file_size(romFile))) {
            server->logger.err(id, ": Failed to load data from the file. Do you have the correct access rights?");
            return;
        }
    }

    // TODO: compressed roms and stuff

    if (!Core.LoadGame(&info)) {
        server->logger.err(id, ": Failed to load game. Was the rom the correct file type?");
        return;
    }

    // Load state if applicable
    Load();

    auto &config = server->config;

    Core.GetAudioVideoInfo(&avinfo);

    unsigned msWait = (1.0 / avinfo.timing.fps) * 1000;
    std::chrono::time_point<std::chrono::steady_clock> nextRun =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(msWait / (fastForward ? 2 : 1));

    // Set FPS if applicable
    auto overrideFPS = server->config.get<bool>(nlohmann::json::value_t::boolean, "serverConfig", "emulators", id,
                                                "overrideFramerate");
    std::chrono::microseconds frameDeltaTime;


    if (overrideFPS) {
        auto newFPS = server->config.get<std::uint64_t>(nlohmann::json::value_t::number_unsigned, "serverConfig",
                                                        "emulators", id, "fps");
        frameDeltaTime = std::chrono::microseconds(1'000'000) / newFPS;
    }

    // Terrible main emulator loop that manages all the things
    std::chrono::time_point<std::chrono::steady_clock> turnEnd, nextFrame;
    bool frameSkip = false;
    while (true) {
        // Check turn state
        // Possible race condition but wouldn't really matter because it'd be a read during a write onto a boolean value
        if (!turnQueue.empty()) {
            if (auto currentUser = turnQueue[0].lock()) {
                // Things that could happen:
                // Newly added, no turn grant
                // Current, has turn grant
                // Manage leaves
                if (!currentUser->hasTurn && currentUser->connected) { // newly added
                    // grant turn
                    currentUser->hasTurn = true;

                    // update turn end
                    const auto turnLength = server->config.get<std::uint64_t>(nlohmann::json::value_t::number_unsigned,
                                                                              "serverConfig", "emulators", id,
                                                                              "turnLength");
                    turnEnd = std::chrono::steady_clock::now() + std::chrono::milliseconds(turnLength);
                } else { // has turn, check grant validity
                    if (turnEnd < std::chrono::steady_clock::now()) { // no turn: end it
                        std::unique_lock <std::mutex> lk(turnMutex);
                        if (turnQueue.size() > 1) {
                            currentUser->hasTurn = false;
                            currentUser->requestedTurn = false;
                            turnQueue.erase(turnQueue.begin());
                            joypad.resetValues();
                            EmulatorController::SendTurnList();
                        }
                    }
                }
            } else { // Something happened to the user, so skip them
                std::unique_lock <std::mutex> lk(turnMutex);
                if (!turnQueue.empty()) {
                    turnQueue.erase(turnQueue.begin());
                    joypad.resetValues();
                    EmulatorController::SendTurnList();
                }
            }
        }

        // While there's work and we have time before the next retro_run call
        while (!workQueue.empty() && (std::chrono::steady_clock::now() < nextRun) &&
               (!overrideFPS || (std::chrono::steady_clock::now() < nextFrame))) {
            auto &command = workQueue.front();

            switch (command.command) {
                case kEmuCommandType::Save:
                    Save();
                    break;
                case kEmuCommandType::Backup:
                    Backup();
                    break;
                case kEmuCommandType::GeneratePreview:
                    server->GeneratePreview(id);
                    break;
                case kEmuCommandType::TurnRequest:
                    if (command.user_hdl)
                        AddTurnRequest(*command.user_hdl);
                    break;
                case kEmuCommandType::UserDisconnect:
                    if (command.user_hdl)
                        UserDisconnected(*command.user_hdl);
                    break;
                case kEmuCommandType::FastForward:
                    FastForward();
                    break;
                case kEmuCommandType::UserConnect:
                    EmulatorController::SendTurnList();
                    break;
            }
            std::unique_lock <std::mutex> lk(queueMutex);
            workQueue.pop();
        }

        // Wait until the next frame because at this point we've either passed the wait time (so 0 wait) or have no more work
        // NOTE: If on a slow fps rate, there will be a lot of wasted time and the turns updating and work queue will be slow
        // This relies on the fact that emulators usually want to be run 30 to 60 times a second
        std::this_thread::sleep_until(nextRun);
        nextRun = std::chrono::steady_clock::now() + std::chrono::milliseconds(msWait / (fastForward ? 2 : 1));
        Core.Run();

        if (overrideFPS && (nextFrame < std::chrono::steady_clock::now())) {
            server->SendFrame(id);
            nextFrame = std::chrono::steady_clock::now() + frameDeltaTime;
        } else if (!overrideFPS) {
            if (fastForward && (frameSkip ^= true)) server->SendFrame(id);
            else server->SendFrame(id);
        }
    }
}

bool EmulatorController::OnEnvironment(unsigned cmd, void *data) {
    auto &config = server->config;
    switch (cmd) {
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
            const retro_pixel_format *fmt = static_cast<retro_pixel_format *>(data);

            if (*fmt > RETRO_PIXEL_FORMAT_RGB565) return false;

            return SetPixelFormat(*fmt);
        }
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: // system dir, dataDir / system
            *static_cast<const char **>(data) = server->systemDirectory.string().c_str();
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
    std::unique_lock <std::mutex> lk(videoMutex);
    if (width != videoFormat.width || height != videoFormat.height ||
        pitch != videoFormat.pitch) {
        std::clog << "Screen Res changed from " << videoFormat.width << 'x'
                  << videoFormat.height << " to " << width << 'x' << height << ' ' << pitch
                  << '\n';
        videoFormat.width = width;
        videoFormat.height = height;
        videoFormat.pitch = pitch;
    }
    currentBuffer = data;
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

void EmulatorController::AddTurnRequest(LetsPlayUserHdl user_hdl) {
    // Add user to the list
    std::unique_lock <std::mutex> lk(turnMutex);
    turnQueue.emplace_back(user_hdl);

    // Send off updated turn list
    EmulatorController::SendTurnList();
}

void EmulatorController::SendTurnList() {
    const std::string turnList = [&] {
        // Majority of the time this won't lock because turnMutex will have already been locked by the caller
        std::unique_lock <std::mutex> lk(turnMutex, std::try_to_lock);

        std::vector<std::string> names{"turns"};
        for (auto user_hdl : turnQueue) {
            // If pointer hasn't been deleted and user is still connected
            auto user = user_hdl.lock();
            if (user && user->connected)
                names.push_back(user->username());
        }
        return LetsPlayProtocol::encode(names);
    }();

    server->BroadcastToEmu(id, turnList, websocketpp::frame::opcode::text);
}

void EmulatorController::UserDisconnected(LetsPlayUserHdl user_hdl) {
    // Update flag in case the turn queue gets to the user before its removed from memory in server
    if (auto user = user_hdl.lock())
        user->connected = false;
}

void EmulatorController::UserConnected(LetsPlayUserHdl) {
    EmulatorController::SendTurnList();
}

bool EmulatorController::SetPixelFormat(const retro_pixel_format fmt) {
    switch (fmt) {
        // TODO: Find a core that uses this and test it
        case RETRO_PIXEL_FORMAT_0RGB1555:  // 16 bit
            // rrrrrgggggbbbbba
            std::clog << "0RGB1555" << '\n';
            videoFormat.rMask = 0b1111100000000000;
            videoFormat.gMask = 0b0000011111000000;
            videoFormat.bMask = 0b0000000000111110;
            videoFormat.aMask = 0b0000000000000000;

            videoFormat.rShift = 10;
            videoFormat.gShift = 5;
            videoFormat.bShift = 0;
            videoFormat.aShift = 15;

            videoFormat.bitsPerPel = 16;
            return true;
            // TODO: Fix (find a core that uses this, bsnes accuracy gives a zeroed
            // out video buffer so thats a no go)
        case RETRO_PIXEL_FORMAT_XRGB8888:  // 32 bit
            std::clog << "XRGB8888\n";
            videoFormat.rMask = 0xff000000;
            videoFormat.gMask = 0x00ff0000;
            videoFormat.bMask = 0x0000ff00;
            videoFormat.aMask = 0x00000000;  // normally 0xff but who cares about alpha

            videoFormat.rShift = 16;
            videoFormat.gShift = 8;
            videoFormat.bShift = 0;
            videoFormat.aShift = 24;

            videoFormat.bitsPerPel = 32;
            return true;
        case RETRO_PIXEL_FORMAT_RGB565:  // 16 bit
            // rrrrrggggggbbbbb
            std::clog << "RGB656\n";
            videoFormat.rMask = 0b1111100000000000;
            videoFormat.gMask = 0b0000011111100000;
            videoFormat.bMask = 0b0000000000011111;
            videoFormat.aMask = 0b0000000000000000;

            videoFormat.rShift = 11;
            videoFormat.gShift = 5;
            videoFormat.bShift = 0;
            videoFormat.aShift = 16;

            videoFormat.bitsPerPel = 16;
            return true;
        default:
            return false;
    }
}

Frame EmulatorController::GetFrame() {
    std::unique_lock <std::mutex> lk(videoMutex);
    if (currentBuffer == nullptr) return Frame{0, 0, {}};

    std::vector <std::uint8_t> outVec(videoFormat.width * videoFormat.height * 3);

    size_t j{0};

    const auto *i = static_cast<const std::uint8_t *>(currentBuffer);
    for (size_t h = 0; h < videoFormat.height; ++h) {
        for (size_t w = 0; w < videoFormat.width; ++w) {
            std::uint32_t pixel{0};
            // Assuming little endian
            pixel |= *(i++);
            pixel |= *(i++) << 8;

            if (videoFormat.bitsPerPel == 32) {
                pixel |= *(i++) << 16;
                pixel |= *(i++) << 24;
            }

            // Calculate the rgb 0 - 255 values
            const std::uint8_t &rMax = 1 << (videoFormat.aShift - videoFormat.rShift);
            const std::uint8_t &gMax = 1 << (videoFormat.rShift - videoFormat.gShift);
            const std::uint8_t &bMax = 1 << (videoFormat.gShift - videoFormat.bShift);

            const std::uint8_t &rVal = (pixel & videoFormat.rMask) >> videoFormat.rShift;
            const std::uint8_t &gVal = (pixel & videoFormat.gMask) >> videoFormat.gShift;
            const std::uint8_t &bVal = (pixel & videoFormat.bMask) >> videoFormat.bShift;

            std::uint8_t rNormalized = (rVal / (double) rMax) * 255;
            std::uint8_t gNormalized = (gVal / (double) gMax) * 255;
            std::uint8_t bNormalized = (bVal / (double) bMax) * 255;

            outVec[j++] = rNormalized;
            outVec[j++] = gNormalized;
            outVec[j++] = bNormalized;
        }
        i += videoFormat.pitch - 2 * videoFormat.width;
    }

    return Frame{videoFormat.width, videoFormat.height, outVec};
}

void EmulatorController::Save() {
    std::unique_lock <std::shared_timed_mutex> lk(generalMutex);
    auto size = Core.SaveStateSize();

    if (size == 0) { // Not supported by the loaded core
        server->logger.log(id, ": Warning; Saving for this core unsupported. Skipping save procedure.");
        return;
    }

    std::vector<unsigned char> saveData(size);

    if (!Core.SaveState(saveData.data(), size)) {
        server->logger.log(id, ": Warning; Failed to serialize data with size ", size, ".");
        return;
    }

    auto newSaveFile = dataDirectory / "history" / "current.state";

    if (lib::filesystem::exists(newSaveFile)) { // Move current file to a backup if if exists
        server->logger.log(id, ": Existing state detected; Moving to new state.");
        namespace chrono = std::chrono;

        auto tp = chrono::system_clock::now().time_since_epoch();
        auto timestamp = std::to_string(chrono::duration_cast<chrono::seconds>(tp).count());

        auto backupName = dataDirectory / "history" / (timestamp + ".state");

        server->logger.log(id, ": Moved current state to ", backupName.string());

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

        auto maxHistorySize = server->config.get<std::uint64_t>(nlohmann::json::value_t::number_unsigned,
                                                                "serverConfig", "backups", "maxHistorySize");
        if (temporaryStates.size() > maxHistorySize) {
            // Sort by filename
            std::sort(temporaryStates.begin(), temporaryStates.end(), [](const auto &a, const auto &b) {
                return a.string() < b.string();
            });

            // Delete the oldest file
            server->logger.log(id, ": Over threshold; Removing ", temporaryStates.front().string());
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

    std::unique_lock <std::shared_timed_mutex> lk(generalMutex);

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
    lib::filesystem::copy(dataDirectory / "history" / "current.state",
                          dataDirectory / "backups" / "states" / (timestamp + ".state"));
}

void EmulatorController::FastForward() {
    const auto &now = std::chrono::steady_clock::now();

    // limit rate that the fast forward state can be toggled
    if (now > (lastFastForward + std::chrono::milliseconds(
            150))) { // 150 ms ~= 7 clicks per second ~= how fast the average person can click
        // yay types
        bool b = fastForward;
        b ^= true;
        fastForward = b;
    }
}

void EmulatorController::Load() {
    std::unique_lock <std::shared_timed_mutex> lk(generalMutex);
    auto saveFile = dataDirectory / "history" / "current.state";

    if (!lib::filesystem::exists(saveFile)) return; // Hasn't saved yet, so don't try to load it

    auto saveFileSize = lib::filesystem::file_size(saveFile);
    std::vector<unsigned char> saveData(saveFileSize);

    std::ifstream fi(saveFile.string(), std::ios::binary);
    fi.read(reinterpret_cast<char *>(saveData.data()), saveFileSize);

    Core.LoadState(saveData.data(), saveFileSize);
}