#include "LetsPlayUser.h"

bool LetsPlayUser::hasTurnOn(const EmuID_t& id) {
    std::unique_lock<std::mutex> lk((m_access));
    return m_hasTurn[id];
}

bool LetsPlayUser::requestedTurnOn(const EmuID_t& id) {
    std::unique_lock<std::mutex> lk((m_access));
    return m_requestedTurn[id];
}

bool LetsPlayUser::isConnectedTo(const EmuID_t& id) {
    std::unique_lock<std::mutex> lk((m_access));
    return std::find(m_connectedEmus.cbegin(), m_connectedEmus.cend(), id) !=
           m_connectedEmus.cend();
}

void LetsPlayUser::connect(const EmuID_t& id) {
    if (!this->isConnectedTo(id)) {
        std::unique_lock<std::mutex> lk((m_access));
        m_connectedEmus.emplace_back(id);
    }
}

void LetsPlayUser::cleanupID(const EmuID_t& id) {
    std::unique_lock<std::mutex> lk((m_access));
    m_hasTurn.erase(id);
    m_requestedTurn.erase(id);
    auto connectedEmusSearch =
        std::find(m_connectedEmus.begin(), m_connectedEmus.end(), id);
    if (connectedEmusSearch != m_connectedEmus.end())
        m_connectedEmus.erase(connectedEmusSearch);
}

void LetsPlayUser::disconnect(const EmuID_t& id) {
    if (this->isConnectedTo(id)) {
        std::unique_lock<std::mutex> lk((m_access));
        m_connectedEmus.emplace_back(id);
    }
}
