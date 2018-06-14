#include "EmulatorController.h"
#include <memory>

RetroCore EmulatorController::Core;
LetsPlayServer* EmulatorController::m_server{nullptr};
std::vector<LetsPlayUser*> EmulatorController::m_TurnQueue;
std::mutex EmulatorController::m_TurnMutex;
std::condition_variable EmulatorController::m_TurnNotifier;
std::atomic<bool> EmulatorController::m_TurnThreadRunning;
std::thread EmulatorController::m_TurnThread;
EmuID_t EmulatorController::id;
VideoFormat EmulatorController::m_videoFormat;
EmulatorControllerProxy EmulatorController::proxy;
ScreenMatrix_t EmulatorController::m_nextFrame;
ScreenMatrix_t EmulatorController::m_screen;
std::mutex EmulatorController::m_screenMutex;

void EmulatorController::Run(const std::string& corePath,
                             const std::string& romPath, LetsPlayServer* server,
                             EmuID_t t_id) {
    Core.Init(corePath.c_str());
    m_server = server;
    id = t_id;
    proxy = EmulatorControllerProxy{addTurnRequest, userDisconnected,
                                    userConnected, getScreen};
    m_server->addEmu(id, &proxy);
    std::thread t(
        [](std::function<void()> function) {
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                function();
            }
        },
        SaveImage);

    t.detach();

    (*(Core.fSetEnvironment))(OnEnvironment);
    (*(Core.fSetVideoRefresh))(OnVideoRefresh);
    (*(Core.fSetInputPoll))(OnPollInput);
    (*(Core.fSetInputState))(OnGetInputState);
    (*(Core.fSetAudioSample))(OnLRAudioSample);
    (*(Core.fSetAudioSampleBatch))(OnBatchAudioSample);
    (*(Core.fInit))();

    // TODO: C++
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

    while (true) (*(Core.fRun))();
}

bool EmulatorController::OnEnvironment(unsigned cmd, void* data) {
    switch (cmd) {
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
            const retro_pixel_format* fmt =
                static_cast<retro_pixel_format*>(data);

            if (*fmt > RETRO_PIXEL_FORMAT_RGB565) return false;

            return setPixelFormat(*fmt);
        } break;
        default:
            return false;
    }
}

void EmulatorController::OnVideoRefresh(const void* data, unsigned width,
                                        unsigned height, size_t stride) {
    /*
     * true if function only copies updates to m_nextFrame after merging
     * m_screen and the previous version of m_nextFrame — only false on video
     * mode resizes
     */
    bool diffMode;
    if (width != m_videoFormat.width || height != m_videoFormat.height) {
        std::clog << "Screen Res changed from " << m_videoFormat.width << 'x'
                  << m_videoFormat.height << " to " << width << 'x' << height
                  << '\n';
        m_videoFormat.width = width;
        m_videoFormat.height = height;
        std::cout << width << ' ' << height << ' ' << stride << '\n';

        // Reset the screens with all transparent vectors (the user's canvas
        // will resize and stretch the existing image until new frames are sent)
        std::unique_lock<std::mutex> lk((m_screenMutex));
        m_screen.assign(height, std::vector<RGBColor>(
                                    width, RGBColor(0, 0, 0, Visible{false})));
        m_nextFrame = m_screen;
        // Set diffMode to false so that all pixels are copied, not just the
        // differences
        diffMode = false;
    } else {
        // Merge m_nextFrame and m_screen
        std::unique_lock<std::mutex> lk((m_screenMutex));
        overlay(m_nextFrame, m_screen);
        diffMode = true;
    }

    const auto bytesPerPel = m_videoFormat.bitsPerPel / 8;
    const auto dataLength = bytesPerPel * stride * height;

    const std::uint16_t* pData = static_cast<const std::uint16_t*>(data);
    auto i = pData;

    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            // Pull a pixel from the data stream
            std::uint32_t pixel{0};
            pixel |= *(i++);

            if (bytesPerPel == 4) {
                pixel <<= 16;
                pixel |= *(i++);
            }

            // Calculate the rgb 0 - 255 values
            const std::uint8_t rMax =
                1 << (m_videoFormat.aShift - m_videoFormat.rShift);
            const std::uint8_t gMax =
                1 << (m_videoFormat.rShift - m_videoFormat.gShift);
            const std::uint8_t bMax =
                1 << (m_videoFormat.gShift - m_videoFormat.bShift);
            // std::cout << (unsigned)rMax << ' ' << (unsigned)gMax << ' '
            //<< (unsigned)bMax << '\n';

            const std::uint8_t rVal =
                (pixel & m_videoFormat.rMask) >> m_videoFormat.rShift;
            const std::uint8_t gVal =
                (pixel & m_videoFormat.gMask) >> m_videoFormat.gShift;
            const std::uint8_t bVal =
                (pixel & m_videoFormat.bMask) >> m_videoFormat.bShift;
            const std::uint8_t aVal =
                (pixel & m_videoFormat.aMask) >> m_videoFormat.aShift;

            // std::cout << (unsigned)rVal << ' ' << (unsigned)gVal << ' '
            //<< (unsigned)bVal << '\n';
            const std::uint8_t rNormalized = (rVal / (double)rMax) * 255;
            const std::uint8_t gNormalized = (gVal / (double)gMax) * 255;
            const std::uint8_t bNormalized = (bVal / (double)bMax) * 255;
            const bool aNormalized = (aVal > 0) ? true : false;

            // Update the next frame accordingly
            // FIXME? Mutex the screen vecs or does it even matter? We want
            // performance at this part and a read while writing happening about
            // once out of 1000 frames with 60 frames a second won't hurt
            // anything
            RGBColor pel{rNormalized, gNormalized, bNormalized, true};
            if (diffMode && m_screen[y][x] != pel)
                m_nextFrame.at(y).at(x) = pel;
            else
                m_nextFrame[y][x] = RGBColor{0, 0, 0, false};

            if (false && pixel != 0) {
                std::cout << " -- Pel -- " << pixel << '\n';
                std::cout << " -- Max Values -- " << (unsigned)rMax << ' '
                          << (unsigned)gMax << ' ' << (unsigned)bMax << '\n';
                std::cout << " -- Masks -- " << (unsigned)m_videoFormat.rMask
                          << ' ' << m_videoFormat.gMask << ' '
                          << m_videoFormat.bMask << '\n';
                std::cout << " -- Shifts -- " << (unsigned)m_videoFormat.aShift
                          << ' ' << (unsigned)m_videoFormat.gShift << ' '
                          << (unsigned)m_videoFormat.bShift << '\n';
                std::cout << " -- Normalized -- " << (unsigned)rNormalized
                          << ' ' << (unsigned)gNormalized << ' '
                          << (unsigned)bNormalized << '\n';
                std::cout << " -- Final values -- "
                          << (unsigned)m_nextFrame[y][x].r << ' '
                          << (unsigned)m_nextFrame[y][x].g << ' '
                          << (unsigned)m_nextFrame[y][x].b << ' '
                          << (unsigned)m_nextFrame[y][x].a << '\n';
                std::cout << " -- Diff Mode -- " << diffMode << '\n';
            }
        }
        // i += stride / 2;
    }
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
        std::cout << "Loop" << '\n';
        std::unique_lock<std::mutex> lk((m_TurnMutex));

        // Wait for a nonempty queue
        while (m_TurnQueue.empty()) {
            m_TurnNotifier.wait(lk);
            std::cout << "notified or spurious awake" << '\n';
        }

        if (m_TurnThreadRunning == false) break;
        // XXX: Unprotected access to top of the queue, only access
        // threadsafe members
        auto& currentUser = m_TurnQueue[0];
        std::string username = currentUser->username();
        currentUser->hasTurn = true;
        m_server->BroadcastAll(id + ": " + username + " now has a turn!");
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
        m_server->BroadcastAll(id + ": " + username + "'s turn has ended!");
        m_TurnQueue.erase(m_TurnQueue.begin());
    }
}

void EmulatorController::addTurnRequest(LetsPlayUser* user) {
    std::unique_lock<std::mutex> lk((m_TurnMutex));
    m_TurnQueue.emplace_back(user);
    m_TurnNotifier.notify_one();
}

void EmulatorController::userDisconnected(LetsPlayUser* user) {
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

void EmulatorController::userConnected(LetsPlayUser* user) { return; }

bool EmulatorController::setPixelFormat(const retro_pixel_format fmt) {
    switch (fmt) {
        case RETRO_PIXEL_FORMAT_0RGB1555:  // 16 bit
            // rrrrrgggggbbbbba
            std::clog << "Setting format to 0RGB15555" << '\n';
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
        case RETRO_PIXEL_FORMAT_XRGB8888:  // 32 bit
            std::clog << "Setting format to XRGB8888" << '\n';
            m_videoFormat.rMask = 0xff000000;
            m_videoFormat.gMask = 0x00ff0000;
            m_videoFormat.bMask = 0x0000ff00;
            m_videoFormat.aMask = 0x000000ff;

            m_videoFormat.rShift = 16;
            m_videoFormat.gShift = 8;
            m_videoFormat.bShift = 0;
            m_videoFormat.aShift = 24;

            m_videoFormat.bitsPerPel = 32;
            return true;
        case RETRO_PIXEL_FORMAT_RGB565:  // 16 bit
            // rrrrrggggggbbbbb
            std::clog << "Setting format to RGB565" << '\n';
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

void EmulatorController::overlay(ScreenMatrix_t& fg, ScreenMatrix_t& bg) {
    if (fg.size() == 0 || bg.size() == 0 || fg.size() != bg.size() ||
        fg[0].size() != bg[0].size())
        return;

    for (std::size_t y = 0; y < fg.size(); ++y) {
        for (std::size_t x = 0; x < fg[0].size(); ++x) {
            if (fg[y][x].a) bg[y][x] = fg[y][x];
        }
    }
}

ScreenMatrix_t EmulatorController::getScreen() {
    std::unique_lock<std::mutex> lk((m_screenMutex));
    return m_screen;
}

void EmulatorController::SaveImage() {
    static unsigned imageNum = 0;
    // Split to own function later
    auto screen = getScreen();

    const size_t height = screen.size();
    const size_t width = [&]() -> size_t {
        if (screen.size() < 1)
            return 0;
        else
            return screen[0].size();
    }();

    if (std::max(height, width) <= 0) return;

    std::vector<std::uint8_t> outVec;

    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            auto pel = screen.at(y).at(x);
            std::cout << (unsigned)pel.r << ' ' << (unsigned)pel.g << ' '
                      << (unsigned)pel.b << '\n';

            // RGBA
            outVec.push_back(pel.r.load());
            outVec.push_back(pel.g.load());
            outVec.push_back(pel.b.load());

            if (pel.a)
                outVec.push_back(255);
            else
                outVec.push_back(0);
        }
    }
    // ------
    // size_t WebPEncodeLosslessRGBA(const uint8_t* rgba, int width, int height,
    // int stride, uint8_t** output);

    std::uint8_t* output;
    size_t written = WebPEncodeLosslessRGBA(outVec.data(), width, height,
                                            width * 3, &output);

    std::ofstream of(
        std::string("screenshot") + std::to_string(imageNum++) + ".webp",
        std::ios::binary);
    of.write(reinterpret_cast<char*>(output), written);
    WebPFree(output);
}
