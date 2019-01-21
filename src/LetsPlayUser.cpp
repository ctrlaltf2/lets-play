
#include <LetsPlayUser.h>

#include "LetsPlayUser.h"

std::mutex g_uuidMutex;
uuid::random_generator g_UUIDGen;

LetsPlayUser::LetsPlayUser()
    : m_lastPong{std::chrono::steady_clock::now()},
      hasTurn{false},
      requestedTurn{false},
      connected{true},
      hasAdmin{false},
      adminAttempts{0} {
    g_uuidMutex.lock();
    m_uuid = g_UUIDGen();
    g_uuidMutex.unlock();
}

bool LetsPlayUser::shouldDisconnect() {
    return std::chrono::steady_clock::now() >
        (m_lastPong + std::chrono::seconds(10));
}

void LetsPlayUser::updateLastPong() {
    std::unique_lock lk(m_access);
    m_lastPong = std::chrono::steady_clock::now();
}

EmuID_t LetsPlayUser::connectedEmu() {
    std::unique_lock lk(m_access);
    return m_connectedEmu;
}

void LetsPlayUser::setConnectedEmu(const EmuID_t& id) {
    std::unique_lock lk(m_access);
    m_connectedEmu = id;
}

std::string LetsPlayUser::username() {
    std::unique_lock lk(m_access);
    return m_username;
}

void LetsPlayUser::setUsername(const std::string& name) {
    std::unique_lock lk(m_access);
    m_username = name;
}

std::string LetsPlayUser::IP() const {
    return m_ip;
}

void LetsPlayUser::setIP(const std::string& ip) {
    std::unique_lock lk(m_access);
    m_ip = ip;
}

std::string LetsPlayUser::uuid() const {
    std::string s = uuid::to_string(m_uuid);
    s.insert(s.begin(), '{');
    s += '}';
    return s;
}
