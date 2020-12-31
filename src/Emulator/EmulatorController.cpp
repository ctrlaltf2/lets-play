#include "EmulatorController.h"

/**
 * Now, you're probably wondering: static thread_local? namespaced pseudo-classes? Surely this guy is crazy!
 * Well, you're in for a story. Basically, the libretro API, the thing that this 'class' interacts with
 * and allows emulators to be run, provides no way for functions that you register to have a void*
 * params I can throw around, so frontends (i.e. this program) are stuck storing state in global variables.
 * Not *that* terrible (other than, you know, globals), until you realise you want to load in multiple
 * emulators and... well... can't do that if they share the same global state! So, the current solution
 * to that is to run every emulator in its own thread (yikes!), so that they each can get their own state.
 *
 * Something I'm looking into for solving this in a more elegant way is to:
 *      - fork() off and spawn a new process when creating a EmulatorController
 *      - Use TIPC as the IPC protocol for EmulatorControler <-> LetsPlayServer communication
 *          - Why TIPC? Well, just in case I wanted to cluster this and allow my RPI to run a SNES emulator,
 *            while my laptop runs the actual server, because, why not? Also TIPC sounds pretty cool
 */
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
     * libretro logging interface struct (points to server->logger.logFormatted)
     */
    static thread_local retro_log_callback log_cb;

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
    static thread_local boost::filesystem::path dataDirectory;

    /**
     * Given to the core as the save directory
     */
    static thread_local boost::filesystem::path saveDirectory;

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

    /**
     * List of the forbidden button combos
     * @note Works by using 16 length bitsets that act as the pressed state for the input. The nth bit in the bitsets
     * represent the RETRO_DEVICE_ID_JOYPAD ID and whether or not it is pressed. On every button press by a user,
     * the button state for this emulator is retrieved as a bitset. Then, every bitset in this is list is AND'd against
     * that state. If the resultant is the same as the forbidden state, then the button combo should be blocked.
     */
     static thread_local std::vector<std::bitset<16>> forbiddenCombos;

     /**
      * Relation for button name -> Retro button ID
      */
     static const std::map<std::string, unsigned> buttonAsRetroID = {
             {"a", RETRO_DEVICE_ID_JOYPAD_A},
             {"b", RETRO_DEVICE_ID_JOYPAD_B},
             {"x", RETRO_DEVICE_ID_JOYPAD_X},
             {"y", RETRO_DEVICE_ID_JOYPAD_Y},
             {"start", RETRO_DEVICE_ID_JOYPAD_START},
             {"select", RETRO_DEVICE_ID_JOYPAD_SELECT},
             {"up", RETRO_DEVICE_ID_JOYPAD_UP},
             {"down", RETRO_DEVICE_ID_JOYPAD_DOWN},
             {"left", RETRO_DEVICE_ID_JOYPAD_LEFT},
             {"right", RETRO_DEVICE_ID_JOYPAD_RIGHT},
             {"r", RETRO_DEVICE_ID_JOYPAD_R},
             {"l", RETRO_DEVICE_ID_JOYPAD_L},
             {"r2", RETRO_DEVICE_ID_JOYPAD_R2},
             {"l2", RETRO_DEVICE_ID_JOYPAD_L2},
             {"r3", RETRO_DEVICE_ID_JOYPAD_R3},
             {"l3", RETRO_DEVICE_ID_JOYPAD_L3}
     };

     /**
      * Value to keep track of the user count
      *
      * NOTE: This is accessed only in ::Run inside the main loop. All accesses are sequential and not subject to data races.
      * If, for some reason, this value is exported via the EmulatorProxy, then it should be changed to an atomic.
      */
      static thread_local std::uint64_t users{0};
}


void EmulatorController::Run(const std::string& corePath, const std::string& romPath,
                             LetsPlayServer *t_server, EmuID_t t_id, const std::string &description) {
    boost::filesystem::path coreFile = corePath, romFile = romPath;
    if (!boost::filesystem::is_regular_file(coreFile)) {
        t_server->logger.err("Provided core path '", corePath, "' was invalid.");
        return;
    }

    if (!romPath.empty() && !boost::filesystem::is_regular_file(romFile)) {
        server->logger.err("Provided rom path '", romPath, "' was not valid.");
        return;
    }


    // Create emu folder if it doesn't already exist
    t_server->logger.log("Creating emulator directories...");
    boost::filesystem::create_directories(dataDirectory = t_server->emuDirectory / t_id);
    boost::filesystem::create_directories(dataDirectory / "history");
    boost::filesystem::create_directories(dataDirectory / "backups" / "states");
    boost::filesystem::create_directories(saveDirectory = dataDirectory / "saves");

    t_server->logger.log("Copying core file to own path... (", (dataDirectory / "emulator.so").string(), ')');
    boost::filesystem::remove((dataDirectory / "emulator.so").string());
    boost::filesystem::copy_file(coreFile.string(), (dataDirectory / "emulator.so").string());

    t_server->logger.log("Starting up ", t_id, "...");

    Core.Load((dataDirectory / "emulator.so").string().c_str());

    server = t_server;
    id = t_id;
    proxy = EmulatorControllerProxy{&workQueue, &queueMutex, &queueNotifier, GetFrame, &joypad, description, &forbiddenCombos};

    server->AddEmu(id, &proxy);

    // Add emu specific config if it doesn't already exist
    auto emuConfigs = server->config.get<nlohmann::json>(nlohmann::json::value_t::object, "serverConfig", "emulators");
    if(!emuConfigs.count(id)) {
        auto emuTemplate = server->config.get<nlohmann::json>(nlohmann::json::value_t::object, "serverConfig", "emulators", "template");
        server->config.set("serverConfig", "emulators", id, emuTemplate);
    }

    server->config.SaveConfig();

    Core.SetEnvironment(OnEnvironment);
    Core.SetVideoRefresh(OnVideoRefresh);
    Core.SetInputPoll(OnPollInput);
    Core.SetInputState(OnGetInputState);
    Core.SetAudioSample(OnLRAudioSample);
    Core.SetAudioSampleBatch(OnBatchAudioSample);
    Core.Init();

    // Load forbidden button combos into memory
    auto jForbiddenCombos = server->config.get<nlohmann::json>(nlohmann::json::value_t::array, "serverConfig", "emulators", id, "forbiddenCombos");

    for(std::string buttons : jForbiddenCombos) {
        bool goodCombo{true};
        std::stringstream ss{buttons};
        std::bitset<16> combo;
        for(std::string button; ss >> button;) {
            std::transform(button.begin(), button.end(), button.begin(), ::tolower);
            try {
                const auto retroID = buttonAsRetroID.at(button);
                combo[retroID] = true;
            } catch(const std::out_of_range& e) {
                server->logger.log(id, ": Invalid button name found in forbiddenCombos list called '", button, "'.");
                goodCombo = false;
                break;
            }
        }
        if(combo.any() && goodCombo)
            forbiddenCombos.push_back(combo);
    }

    server->logger.log(id, ": Finished initialization.");

    // If provided an empty path, just skip this part. Leaving a blank path allows for cores that don't need roms to be loaded
    if(!romPath.empty()) {
        retro_game_info info = {romPath.c_str(), nullptr, static_cast<size_t>(boost::filesystem::file_size(romFile)),
                                nullptr};
        std::ifstream fo(romFile.string(), std::ios::binary);

        retro_system_info system{};
        Core.GetSystemInfo(&system);

        if (!system.need_fullpath) {
            romData = new char[boost::filesystem::file_size(romFile)];
            info.data = static_cast<void *>(romData);

            if (!info.data) {
                server->logger.err(id, ": Failed to allocate memory for the ROM");
                return;
            }

            if (!fo.read(romData, boost::filesystem::file_size(romFile))) {
                server->logger.err(id, ": Failed to load data from the file. Do you have the correct access rights?");
                return;
            }
        }

        // TODO: compressed roms and stuff

        if (!Core.LoadGame(&info)) {
            server->logger.err(id, ": Failed to load game. Was the rom the correct file type?");
            return;
        }
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
            } else { // Something happened :( to the user, so skip them
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
                    if(users)
                        --users;
                    if (command.user_hdl)
                        UserDisconnected(*command.user_hdl);
                    break;
                case kEmuCommandType::FastForward:
                    FastForward();
                    break;
                case kEmuCommandType::UserConnect:
                    ++users;
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

        if(users) {
            if (overrideFPS && (nextFrame < std::chrono::steady_clock::now())) {
                server->SendFrame(id);
                nextFrame = std::chrono::steady_clock::now() + frameDeltaTime;
            } else if (!overrideFPS) {
                if (fastForward && (frameSkip ^= true)) server->SendFrame(id);
                else server->SendFrame(id);
            }
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
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: // system dir = dataDir / system
            *static_cast<const char **>(data) = server->systemDirectory.string().c_str();
            break;
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: // save dir = dataDir / emulators / emu_id / saves
            saveDirString = saveDirectory.string();
            *static_cast<const char **>(data) = saveDirString.c_str();
            break;
        case RETRO_ENVIRONMENT_GET_USERNAME:
            *static_cast<const char **>(data) = id.c_str();
            break;
        case RETRO_ENVIRONMENT_GET_OVERSCAN: // We don't (usually) want overscan
            return false;
            // Will be implemented
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: // See core logs
        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: // I think this is called when the avinfo changes
        case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH: // Path to the libretro so core
        case RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK: // Use this instead of sleep_until?
        case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE: // For rumble support for later on
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
        videoFormat.stride = videoFormat.width - 16 * std::ceil(videoFormat.width / 16.0);
        videoFormat.buffer = std::vector <std::uint8_t>((videoFormat.width + videoFormat.stride) * videoFormat.height * 4);
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
    if(fmt == videoFormat.fmt)
        return true;

    switch (fmt) {
        // TODO: Find a core that uses this and test it
        case RETRO_PIXEL_FORMAT_0RGB1555: {  // 16 bit
            // rrrrrgggggbbbbba
            server->logger.log(" Format set: 0RGB1555");
            videoFormat.rMask = 0b1111100000000000;
            videoFormat.gMask = 0b0000011111000000;
            videoFormat.bMask = 0b0000000000111110;
            videoFormat.aMask = 0b0000000000000000;

            videoFormat.rShift = 10;
            videoFormat.gShift = 5;
            videoFormat.bShift = 0;
            videoFormat.aShift = 15;

            videoFormat.bitsPerPel = 16;

            videoFormat.fmt = fmt;
        }
            return true;
            // TODO: Fix (find a core that uses this, bsnes accuracy gives a zeroed
            // out video buffer so thats a no go)
        case RETRO_PIXEL_FORMAT_XRGB8888:  // 32 bit
            server->logger.log(" Format set: XRGB8888");
            videoFormat.fmt = fmt;
            return true;
        case RETRO_PIXEL_FORMAT_RGB565:  // 16 bit
            // rrrrrggggggbbbbb
            server->logger.log(" Format set: RGB565");
            videoFormat.rMask = 0b1111100000000000;
            videoFormat.gMask = 0b0000011111100000;
            videoFormat.bMask = 0b0000000000011111;
            videoFormat.aMask = 0b0000000000000000;

            videoFormat.rShift = 11;
            videoFormat.gShift = 5;
            videoFormat.bShift = 0;
            videoFormat.aShift = 16;

            videoFormat.bitsPerPel = 16;

            videoFormat.fmt = fmt;
            return true;
        default:
            return false;
    }
}

Frame EmulatorController::GetFrame() {
    std::unique_lock <std::mutex> lk(videoMutex);
    if (currentBuffer == nullptr) return Frame{0, 0, {}};

    if(videoFormat.fmt == RETRO_PIXEL_FORMAT_XRGB8888)
        return Frame{videoFormat.width, videoFormat.height, videoFormat.stride, static_cast<const std::uint8_t *>(currentBuffer)};

    size_t j{0};

    const auto *i = static_cast<const std::uint8_t *>(currentBuffer);
    // TODO: The possible address boundary error on the last row. Probably have a check on this loop then manually do the last row
    /*
     * NOTE: This will assume a 16-bit format. This is due to the fact that the only 32-bit format supported by RetroArch
     * is XRGB8888, which is supported by turbojpeg2 out of the box. The rest of the possible formats are 16-bit.
     */

    // Get a scalar to multiply our three R, G, and B vecs by to move it from nbit to 8bit
    const std::uint8_t &rMax = 1 << (videoFormat.aShift - videoFormat.rShift);
    const std::uint8_t &gMax = 1 << (videoFormat.rShift - videoFormat.gShift);
    const std::uint8_t &bMax = 1 << (videoFormat.gShift - videoFormat.bShift);

    const std::uint16_t rScalar = 255.0 / rMax;
    const std::uint16_t gScalar = 255.0 / gMax;
    const std::uint16_t bScalar = 255.0 / bMax;

    for (size_t h = 0; h < videoFormat.height; ++h) {
        for (size_t w = 0; w < (videoFormat.width + videoFormat.stride) / 16; w++) {
            // Translation step: format -> generic pixel vectors
            // 2x 8 bytes (pixels) packed -> 3 vecs, R, G, B, __m128i (16px)
            __m128i rVec, gVec, bVec;
            for(int q = 0; q < 2; ++q) {
                __m128i px8 = _mm_loadu_si128((__m128i *)i);
                i += 16;

                // Pull out the r, g, b values from the packed pixels
                __m128i mask = _mm_set1_epi16(videoFormat.rMask);
                __m128i rVals = _mm_and_si128(px8, mask);
                rVals = _mm_srli_epi16(rVals, videoFormat.rShift);
                __m128i rMult = _mm_set1_epi16(rScalar);
                rVals = _mm_mullo_epi16(rVals, rMult);

                mask = _mm_set1_epi16(videoFormat.gMask);
                __m128i gVals = _mm_and_si128(px8, mask);
                gVals = _mm_srli_epi16(gVals, videoFormat.gShift);
                __m128i gMult = _mm_set1_epi16(gScalar);
                gVals = _mm_mullo_epi16(gVals, gMult);

                mask = _mm_set1_epi16(videoFormat.bMask);
                __m128i bVals = _mm_and_si128(px8, mask);
                bVals = _mm_srli_epi16(bVals, videoFormat.bShift);
                __m128i bMult = _mm_set1_epi16(bScalar);
                bVals = _mm_mullo_epi16(bVals, bMult);

                // At this point, each of the individual vecs has values like so (1 block = 8 bits):
                // | 0 | X | 0 | X | 0 | X | 0 | X | 0 | X | 0 | X | 0 | X | 0 | X |
                // So, we need to pack the numbers together (remove the 0) and make a 64bit vec, which in another loop of this is combined into a 128i vec
                if (q == 0) {
                    const __m128i hiMask = _mm_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, 128, 128, 128, 128, 128, 128, 128,
                                                         128);

                    rVec = _mm_shuffle_epi8(rVals, hiMask);
                    gVec = _mm_shuffle_epi8(gVals, hiMask);
                    bVec = _mm_shuffle_epi8(bVals, hiMask);
                } else {
                    const __m128i loMask = _mm_setr_epi8(128, 128, 128, 128, 128, 128, 128, 128, 0, 2, 4, 6, 8, 10, 12, 14);

                    // Have a temporary to store the result of the shuffle
                    __m128i temp = _mm_shuffle_epi8(rVals, loMask);
                    // OR the hi and lo parts of the
                    rVec = _mm_or_si128(temp, rVec);

                    temp = _mm_shuffle_epi8(gVals, loMask);
                    gVec = _mm_or_si128(temp, gVec);

                    temp = _mm_shuffle_epi8(bVals, loMask);
                    bVec = _mm_or_si128(temp, bVec);
                }
            }

            // With translation done, we now have a 16px R, G, B vecs that need interleaved into XRGB8888
            // This interleaving will yield 16px * 4 channels per px * 8 bits per channel / 128 bits per vec = 4 XRGB vecs

            // Interleaving masks
            const __m128i rInterleaveMask = _mm_setr_epi8(128, 0, 128, 128, 128, 1, 128, 128, 128, 2, 128, 128, 128, 3, 128, 128);
            const __m128i gInterleaveMask = _mm_setr_epi8(128, 128, 0, 128, 128, 128, 1, 128, 128, 128, 2, 128, 128, 128, 3, 128);
            const __m128i bInterleaveMask = _mm_setr_epi8(128, 128, 128, 0, 128, 128, 128, 1, 128, 128, 128, 2, 128, 128, 128, 3);

            /*
             * Now, usually you could just use a loop for this kind of thing, but, the way that SSE is
             * implemented or the way its spec is, I couldn't use a loop for this kinda thing, you need a
             * compile-time constant
             *
             * So, without further ado, copy-pasta-orama!
             */

            // Extract first value
            __m128i ri = _mm_shuffle_epi32(rVec, 0);
            __m128i gi = _mm_shuffle_epi32(gVec, 0);
            __m128i bi = _mm_shuffle_epi32(bVec, 0);

            // Space out values so they can be OR'd together (interleaved)
            __m128i r0 = _mm_shuffle_epi8(ri, rInterleaveMask);
            __m128i g0 = _mm_shuffle_epi8(gi, gInterleaveMask);
            __m128i b0 = _mm_shuffle_epi8(bi, bInterleaveMask);

            {
                SSE128i xrgb = {_mm_or_si128( _mm_or_si128(r0, g0), b0)};
                for(const auto& u8 : xrgb.data8)
                    videoFormat.buffer[j++] = u8;
            }

            // 2nd value...
            ri = _mm_shuffle_epi32(rVec, 1);
            gi = _mm_shuffle_epi32(gVec, 1);
            bi = _mm_shuffle_epi32(bVec, 1);

            r0 = _mm_shuffle_epi8(ri, rInterleaveMask);
            g0 = _mm_shuffle_epi8(gi, gInterleaveMask);
            b0 = _mm_shuffle_epi8(bi, bInterleaveMask);

            {
                SSE128i xrgb = {_mm_or_si128( _mm_or_si128(r0, g0), b0)};
                for(const auto& u8 : xrgb.data8)
                    videoFormat.buffer[j++] = u8;
            }

            // 3rd value...
            ri = _mm_shuffle_epi32(rVec, 2);
            gi = _mm_shuffle_epi32(gVec, 2);
            bi = _mm_shuffle_epi32(bVec, 2);

            r0 = _mm_shuffle_epi8(ri, rInterleaveMask);
            g0 = _mm_shuffle_epi8(gi, gInterleaveMask);
            b0 = _mm_shuffle_epi8(bi, bInterleaveMask);

            {
                SSE128i xrgb = {_mm_or_si128( _mm_or_si128(r0, g0), b0)};
                for(const auto& u8 : xrgb.data8)
                    videoFormat.buffer[j++] = u8;
            }

            // aaaand the 4th value
            ri = _mm_shuffle_epi32(rVec, 3);
            gi = _mm_shuffle_epi32(gVec, 3);
            bi = _mm_shuffle_epi32(bVec, 3);

            r0 = _mm_shuffle_epi8(ri, rInterleaveMask);
            g0 = _mm_shuffle_epi8(gi, gInterleaveMask);
            b0 = _mm_shuffle_epi8(bi, bInterleaveMask);

            {
                SSE128i xrgb = {_mm_or_si128( _mm_or_si128(r0, g0), b0)};
                for(const auto& u8 : xrgb.data8)
                    videoFormat.buffer[j++] = u8;
            }

        }

        i -= 2*videoFormat.stride; // We will have overrun the row by *stride* number of pixels, so correct that before the next line which assumes we're at the end
        i += videoFormat.pitch - 2*videoFormat.width;
    }

    return Frame{videoFormat.width, videoFormat.height, videoFormat.stride, videoFormat.buffer.data()};
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

    if (boost::filesystem::exists(newSaveFile)) { // Move current file to a backup if if exists
        server->logger.log(id, ": Existing state detected; Moving to new state.");
        namespace chrono = std::chrono;

        auto tp = chrono::system_clock::now().time_since_epoch();
        auto timestamp = std::to_string(chrono::duration_cast<chrono::seconds>(tp).count());

        auto backupName = dataDirectory / "history" / (timestamp + ".state");

        server->logger.log(id, ": Moved current state to ", backupName.string());

        boost::filesystem::rename(newSaveFile, backupName);
    }

    // Remove old temporaries
    {
        std::vector<boost::filesystem::path> temporaryStates;
        for (auto &p : boost::filesystem::directory_iterator(dataDirectory / "history")) {
            auto &path = p.path();

            if (boost::filesystem::is_regular_file(path) && path.extension() == ".state" && path.filename() != "current")
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
            boost::filesystem::remove(temporaryStates.front());
        }

    }

    std::ofstream fo(newSaveFile.string(), std::ios::binary);
    fo.write(reinterpret_cast<char *>(saveData.data()), size);
}

void EmulatorController::Backup() {
    if (!boost::filesystem::exists(
            dataDirectory / "history" / "current.state")) // Create a current.state save if none exists
        Save();

    std::unique_lock <std::shared_timed_mutex> lk(generalMutex);

    namespace chrono = std::chrono;
    auto tp = chrono::system_clock::now().time_since_epoch();
    auto timestamp = std::to_string(chrono::duration_cast<chrono::seconds>(tp).count());

    // Copy any emulator generated files over
    auto currentBackup = dataDirectory / "backups" / timestamp;

    std::function<void(const boost::filesystem::path &, const boost::filesystem::path &)> recursive_copy;
    recursive_copy = [&recursive_copy](const boost::filesystem::path &src, const boost::filesystem::path &dst) {
        if (boost::filesystem::exists(dst)) {
            return;
        }

        if (boost::filesystem::is_directory(src)) {
            boost::filesystem::create_directories(dst);
            for (auto &item : boost::filesystem::directory_iterator(src)) {
                recursive_copy(item.path(), dst / item.path().filename());
            }
        } else if (boost::filesystem::is_regular_file(src)) {
            boost::filesystem::copy(src, dst);
        }
    };

    if (!boost::filesystem::is_empty(saveDirectory))
        recursive_copy(saveDirectory, currentBackup);

    // Copy current history state over
    boost::filesystem::copy(dataDirectory / "history" / "current.state",
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

    if (!boost::filesystem::exists(saveFile)) return; // Hasn't saved yet, so don't try to load it

    auto saveFileSize = boost::filesystem::file_size(saveFile);
    std::vector<unsigned char> saveData(saveFileSize);

    std::ifstream fi(saveFile.string(), std::ios::binary);
    fi.read(reinterpret_cast<char *>(saveData.data()), saveFileSize);

    Core.LoadState(saveData.data(), saveFileSize);
}
