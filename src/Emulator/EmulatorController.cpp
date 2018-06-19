#include "EmulatorController.h"
#include <memory>

EmuID_t EmulatorController::id;
LetsPlayServer* EmulatorController::m_server{nullptr};
EmulatorControllerProxy EmulatorController::proxy;
RetroCore EmulatorController::Core;

std::vector<LetsPlayUser*> EmulatorController::m_TurnQueue;
std::mutex EmulatorController::m_TurnMutex;
std::condition_variable EmulatorController::m_TurnNotifier;
std::atomic<bool> EmulatorController::m_TurnThreadRunning;
std::thread EmulatorController::m_TurnThread;
std::atomic<std::uint64_t> EmulatorController::usersConnected{0};

VideoFormat EmulatorController::m_videoFormat;
Frame EmulatorController::m_keyFrame;
const void* EmulatorController::m_currentBuffer{nullptr};
std::mutex EmulatorController::m_videoMutex;
retro_system_av_info EmulatorController::m_avinfo;

// LetsPlay filesystem layout
/*
 * letsplayfolder/ (~/.letsplay?)
 *     cores/
 *         mgba_libretro.so
 *         vbam_libretro.so
 *         ...
 *     roms/
 *         gba/
 *             Super\ Mario\ Advance\ 1.gba
 *             ...
 *         n64/
 *             Super\ Mario\ 64.n64
 *             ...
 *     system/
 *         some_required_bios.bin
 *         ...
 *     emulators/
 *         gba1/ (automatically created)
 *             state0.sav
 *             emulator_specific_file
 *         snes1/
 *             state0.frz
 *         ...
 *
 */

void EmulatorController::Run(const std::string& corePath,
                             const std::string& romPath, LetsPlayServer* server,
                             EmuID_t t_id) {
    Core.Init(corePath.c_str());
    m_server = server;
    id = t_id;
    proxy = EmulatorControllerProxy{AddTurnRequest, UserDisconnected,
                                    UserConnected, GetFrame};
    m_server->AddEmu(id, &proxy);

    auto& emuConfigs = server->m_config["serverConfig"]["emulators"];
    if (!emuConfigs.count(id)) emuConfigs[id] = emuConfigs["template"];

    (*(Core.fSetEnvironment))(OnEnvironment);
    (*(Core.fSetVideoRefresh))(OnVideoRefresh);
    (*(Core.fSetInputPoll))(OnPollInput);
    (*(Core.fSetInputState))(OnGetInputState);
    (*(Core.fSetAudioSample))(OnLRAudioSample);
    (*(Core.fSetAudioSampleBatch))(OnBatchAudioSample);
    (*(Core.fInit))();

    std::clog << "Past initialization" << '\n';

    retro_system_info system = {0};
    retro_game_info info = {romPath.c_str(), 0};
    FILE* file = fopen(romPath.c_str(), "rb");

    if (!file) {
        std::cout << "invalid file" << '\n';
        return;
    }

    fseek(file, 0, SEEK_END);
    info.size = ftell(file);
    rewind(file);

    (*(Core.fGetSystemInfo))(&system);

    if (!system.need_fullpath) {
        info.data = malloc(info.size);

        if (!info.data || !fread((void*)info.data, info.size, 1, file)) {
            std::cout << "Some error about sizing" << '\n';
            fclose(file);
            return;
        }
    }

    if (!((*(Core.fLoadGame))(&info))) {
        std::cout << "failed to do a thing" << '\n';
    }

    m_TurnThreadRunning = true;
    m_TurnThread = std::thread(EmulatorController::TurnThread);

    (*(Core.fGetAudioVideoInfo))(&m_avinfo);
    int fps{-1};
    if (emuConfig[id]["overrideFramerate"].is_boolean() &&
        emuConfig[id]["fps"].is_number())
        fps = (1.0 / emuConfig[id]["fps"]) * 1000;

    // TODO: Manage this thread
    if (fps != -1) {
        std::thread t([&]() {
            using namespace std::chrono;
            auto nextKeyFrame = steady_clock::now();

            while (true) {
                server->SendFrame(id);
                std::this_thread::sleep_for(milliseconds(250));
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
        wait_time = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(msWait);
        (*(Core.fRun))();
        if (fps == -1) server->SendFrame(id);
    }
}

bool EmulatorController::OnEnvironment(unsigned cmd, void* data) {
    switch (cmd) {
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
            const retro_pixel_format* fmt =
                static_cast<retro_pixel_format*>(data);

            if (*fmt > RETRO_PIXEL_FORMAT_RGB565) return false;

            return SetPixelFormat(*fmt);
        } break;
        // clang-format off
        // Will be implemented
        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: // I think this is called when the avinfo changes
        case RETRO_ENVIRONMENT_GET_OVERSCAN: // We don't (usually) want overscan
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: // Path to the system directory, isolated to each emu and core
        case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH: // Path to the libretro so core
        case RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK: // Use this instead of sleep_until?
        case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE: // For rumble support for later on
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: // See core logs
        case RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY: // Where assets are stored
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: // Directory to store saves in
        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: // Use to see if the core recognizes the retropad (if it doesn't well....)
        case RETRO_ENVIRONMENT_GET_USERNAME: // If netplay is ever figured out (probably won't be, the docs suck), emulators will be able to announce themselves as emu1, emu2, snes1, snes2, etc
        case RETRO_ENVIRONMENT_GET_LANGUAGE: // Some cores might use this and its simple to add
        case RETRO_ENVIRONMENT_GET_VFS_INTERFACE: // Some cores use this and it wouldn't be hard to implement with fstream and filesystem being a thing
        case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE: // Some cores might not use audio, so don't even bother with sending the audio streams
        case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE: // Might want this for support for more hardware accelerated cores
        // clang-format on
        default:
            return false;
    }
}

void EmulatorController::OnVideoRefresh(const void* data, unsigned width,
                                        unsigned height, size_t pitch) {
    std::unique_lock<std::mutex> lk(m_videoMutex);
    if (width != m_videoFormat.width || height != m_videoFormat.height ||
        pitch != m_videoFormat.pitch) {
        std::clog << "Screen Res changed from " << m_videoFormat.width << 'x'
                  << m_videoFormat.height << " to " << width << 'x' << height
                  << ' ' << pitch << '\n';
        m_videoFormat.width = width;
        m_videoFormat.height = height;
        m_videoFormat.pitch = pitch;
    }
    m_currentBuffer = data;
}

void EmulatorController::OnPollInput() {}

std::int16_t EmulatorController::OnGetInputState(unsigned port, unsigned device,
                                                 unsigned index, unsigned id) {
    return 0;
}

void EmulatorController::OnLRAudioSample(std::int16_t left,
                                         std::int16_t right) {}

size_t EmulatorController::OnBatchAudioSample(const std::int16_t* data,
                                              size_t frames) {
    return frames;
}

void EmulatorController::TurnThread() {
    while (m_TurnThreadRunning) {
        std::unique_lock<std::mutex> lk((m_TurnMutex));

        // Wait for a nonempty queue
        while (m_TurnQueue.empty()) {
            m_TurnNotifier.wait(lk);
        }

        if (m_TurnThreadRunning == false) break;
        // XXX: Unprotected access to top of the queue, only access
        // threadsafe members
        auto& currentUser = m_TurnQueue[0];
        std::string username = currentUser->username();
        currentUser->hasTurn = true;
        m_server->BroadcastAll(id + ": " + username + " now has a turn!",
                               websocketpp::frame::opcode::binary);
        const auto turnEnd = std::chrono::steady_clock::now() +
                             std::chrono::seconds(c_turnLength);

        while (currentUser && currentUser->hasTurn &&
               (std::chrono::steady_clock::now() < turnEnd)) {
            // FIXME?: does turnEnd - std::chrono::steady_clock::now() cause
            // underflow or UB for the case where now() is greater than
            // turnEnd?
            m_TurnNotifier.wait_for(lk,
                                    turnEnd - std::chrono::steady_clock::now());
        }

        if (currentUser) {
            currentUser->hasTurn = false;
            currentUser->requestedTurn = false;
        }

        m_server->BroadcastAll(id + ": " + username + "'s turn has ended!",
                               websocketpp::frame::opcode::text);
        m_TurnQueue.erase(m_TurnQueue.begin());
    }
}

void EmulatorController::AddTurnRequest(LetsPlayUser* user) {
    std::unique_lock<std::mutex> lk((m_TurnMutex));
    m_TurnQueue.emplace_back(user);
    m_TurnNotifier.notify_one();
}

void EmulatorController::UserDisconnected(LetsPlayUser* user) {
    --usersConnected;
    std::unique_lock<std::mutex> lk((m_TurnMutex));
    auto& currentUser = m_TurnQueue[0];

    if (currentUser->hasTurn) {  // Current turn is user
        m_TurnNotifier.notify_one();
    } else if (currentUser->requestedTurn) {  // In the queue
        m_TurnQueue.erase(
            std::remove(m_TurnQueue.begin(), m_TurnQueue.end(), user),
            m_TurnQueue.end());
    };
    // Set the current user to nullptr so turnqueue knows not to try to
    // modify it, as it may be in an invalid state because the memory it
    // points to (managed by a std::map) may be reallocated as part of an
    // erase
    currentUser = nullptr;
}

void EmulatorController::UserConnected(LetsPlayUser* user) {
    ++usersConnected;
    return;
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
            m_videoFormat.aMask =
                0x00000000;  // normally 0xff but who cares about alpha

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
    return false;
}

Frame EmulatorController::GetFrame() {
    static size_t o = 0;
    std::unique_lock<std::mutex> lk(m_videoMutex);
    if (m_currentBuffer == nullptr) return Frame{0, 0, {}};
    std::clog << m_videoFormat.width << ' ' << m_videoFormat.height << ' '
              << m_videoFormat.pitch << '\n';
    // Reserve just enough space
    std::shared_ptr<std::uint8_t[]> outVec(
        new std::uint8_t[m_videoFormat.width * m_videoFormat.height * 3]);
    size_t j{0};

    const std::uint8_t* i = static_cast<const std::uint8_t*>(m_currentBuffer);
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
            const std::uint8_t& rMax =
                1 << (m_videoFormat.aShift - m_videoFormat.rShift);
            const std::uint8_t& gMax =
                1 << (m_videoFormat.rShift - m_videoFormat.gShift);
            const std::uint8_t& bMax =
                1 << (m_videoFormat.gShift - m_videoFormat.bShift);

            const std::uint8_t& rVal =
                (pixel & m_videoFormat.rMask) >> m_videoFormat.rShift;
            const std::uint8_t& gVal =
                (pixel & m_videoFormat.gMask) >> m_videoFormat.gShift;
            const std::uint8_t& bVal =
                (pixel & m_videoFormat.bMask) >> m_videoFormat.bShift;

            std::uint8_t rNormalized = (rVal / (double)rMax) * 255;
            std::uint8_t gNormalized = (gVal / (double)gMax) * 255;
            std::uint8_t bNormalized = (bVal / (double)bMax) * 255;

            outVec[j++] = rNormalized;
            outVec[j++] = gNormalized;
            outVec[j++] = bNormalized;
        }
        // I don't even know what i'm doing anymore
        i += m_videoFormat.pitch - 2 * m_videoFormat.width;
        // i += o;
    }

    // std::clog << o << '\n';
    // o = (o + 1) % 100;
    return Frame{m_videoFormat.width, m_videoFormat.height, outVec};
}
