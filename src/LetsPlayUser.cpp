
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
    this->updateLastPong();
}

bool LetsPlayUser::shouldDisconnect() {
    return std::chrono::steady_clock::now() >
        (m_lastPong + std::chrono::seconds(10));
}

void LetsPlayUser::updateLastPong() {
    std::unique_lock<std::mutex> lk(m_access);
    m_lastPong = std::chrono::steady_clock::now();
}

EmuID_t LetsPlayUser::connectedEmu() {
    std::unique_lock<std::mutex> lk(m_access);
    return m_connectedEmu;
}

void LetsPlayUser::setConnectedEmu(const EmuID_t& id) {
    std::unique_lock<std::mutex> lk(m_access);
    m_connectedEmu = id;
}

std::string LetsPlayUser::username() {
    std::unique_lock<std::mutex> lk(m_access);
    return m_username;
}

void LetsPlayUser::setUsername(const std::string& name) {
    std::unique_lock<std::mutex> lk(m_access);
    m_username = name;
}

std::string LetsPlayUser::IP() const {
    return m_ip;
}

void LetsPlayUser::setIP(const std::string& ip) {
    std::unique_lock<std::mutex> lk(m_access);
    m_ip = ip;
}

std::string LetsPlayUser::uuid() const {
    std::string s = uuid::to_string(m_uuid);
    s.insert(s.begin(), '{');
    s += '}';
    return s;
}

std::chrono::time_point<std::chrono::steady_clock> LetsPlayUser::lastUsernameChange() {
    std::unique_lock<std::mutex> lk(m_muting);
    return m_lastUsernameChange;
}

void LetsPlayUser::updateLastUsernameChange() {
    std::unique_lock<std::mutex> lk(m_muting);
    m_lastUsernameChange = std::chrono::steady_clock::now();
}

std::vector<std::chrono::time_point<std::chrono::steady_clock>> LetsPlayUser::messageTimestamps() {
    std::unique_lock<std::mutex> lk(m_muting);
    return m_messageTimestamps;
}

void LetsPlayUser::updateMessageTimestamps(const std::uint32_t historySize) {
    std::unique_lock<std::mutex> lk(m_muting);

    if(m_messageTimestamps.size() >= historySize)
        m_messageTimestamps.erase(m_messageTimestamps.begin());

    m_messageTimestamps.push_back(std::chrono::steady_clock::now());
}

std::chrono::time_point<std::chrono::steady_clock> LetsPlayUser::muteTime() {
    std::unique_lock<std::mutex> lk(m_muting);
    return m_muteTime;
}

void LetsPlayUser::mute(const std::uint32_t seconds) {
    std::unique_lock<std::mutex> lk(m_muting);
    m_muteTime = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
}