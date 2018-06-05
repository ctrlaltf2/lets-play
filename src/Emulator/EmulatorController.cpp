#include "EmulatorController.h"
#include <memory>

RetroCore EmulatorController::Core;
LetsPlayServer* m_server;
std::queue<LetsPlayUser*> m_TurnQueue;
std::mutex m_TurnMutex;
std::condition_variable m_TurnNotifier;
std::atomic<bool> m_TurnThreadRunning;
std::thread m_TurnThread;
EmuID_t id;

LetsPlayServer* EmulatorController::m_server{nullptr};
void EmulatorController::Run(const char* corePath, const char* romPath) {
    Core.Init(corePath);

    (*(Core.fSetEnvironment))(OnEnvironment);
    (*(Core.fSetVideoRefresh))(OnVideoRefresh);
    (*(Core.fSetInputPoll))(OnPollInput);
    (*(Core.fSetInputState))(OnGetInputState);
    (*(Core.fSetAudioSample))(OnLRAudioSample);
    (*(Core.fSetAudioSampleBatch))(OnBatchAudioSample);
    (*(Core.fInit))();

    // TODO: C++
    retro_system_info system = {0};
    retro_game_info info = {romPath, 0};
    FILE* file = fopen(romPath, "rb");

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

    m_TurnThread = std::thread(EmulatorController::TurnThread);

    while (true) (*(Core.fRun))();
}

bool EmulatorController::OnEnvironment(unsigned cmd, void* data) {
    switch (cmd) {
        default:
            return false;
    }
}

void EmulatorController::OnVideoRefresh(const void* data, unsigned width,
                                        unsigned height, size_t stride) {
    std::cout << width << ' ' << height << ' ' << stride << '\n';
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
        while (m_TurnQueue.empty()) m_TurnNotifier.wait(lk);

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
            std::remove_if(m_TurnQueue.begin(), m_TurnQueue.end(), user),
            m_TurnQueue.end());
    };
    // Set the current user to nullptr so turnqueue knows not to try to modify
    // it, as it may be in an invalid state because the memory it points to
    // (managed by a std::map) may be reallocated as part of an erase
    currentUser = nullptr;
}

void EmulatorController::userConnected(LetsPlayUser* user) { return; }
