#include "LetsPlayUser.h"

bool LetsPlayUser::shouldDisconnect() const {
    return std::chrono::steady_clock::now() >
           (m_lastHeartbeat + std::chrono::seconds(c_heartbeatTimeout));
}

EmuID_t LetsPlayUser::connectedEmu() {
    std::unique_lock lk((m_access));
    return m_connectedEmu;
}

void LetsPlayUser::setConnectedEmu(const EmuID_t& id) {
    std::unique_lock lk((m_access));
    m_connectedEmu = id;
}

std::string LetsPlayUser::username() {
    std::unique_lock lk((m_access));
    return m_username;
}

void LetsPlayUser::setUsername(const std::string& name) {
    std::unique_lock lk((m_access));
    m_username = name;
}
