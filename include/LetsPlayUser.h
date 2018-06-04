#pragma once
#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "Server.h"

/*
 * Class representing a connected user (across all emulators)
 */
class LetsPlayUser {
    /*
     * Time point of the last message sent (includes the no operation heartbeat
     * message [to be implemented])
     */
    std::chrono::time_point<std::chrono::steady_clock> m_lastHeartbeat;

    /*
     * Username for the user
     */
    std::string m_username;

    /*
     * Lookup map of emulator ID to if the user has a turn on that emu
     */
    std::map<EmuID_t, bool> m_hasTurn;

    /*
     * Lookup map of emulator ID to if the user has requested a turn on that emu
     */
    std::map<EmuID_t, bool> m_requestedTurn;

    /*
     * List of the emulators the user is connected to
     */
    std::vector<EmuID_t> m_connectedEmus;

    /*
     * Mutex for accessing hasTurn, requestedTurn, or connectedEmus
     */
    std::mutex m_access;

   public:
    LetsPlayUser() : m_lastHeartbeat{std::chrono::steady_clock::now()} {}

    /*
     * Returns true if the user's last heartbeat was over the limit for timeout
     * @return True if the user should be disconnected
     */
    bool shouldDisconnect() const {
        return std::chrono::steady_clock::now() >
               (m_lastHeartbeat + std::chrono::seconds(c_heartbeatTimeout));
    }

    /*
     * Returns true if the user currently has a turn on the specified emu
     */
    bool hasTurnOn(const EmuID_t& id);

    /*
     * Returns true if the user has currently requested a turn on the specified
     * emu
     * NOTE: id should be checked to be a valid emu before calling
     */
    bool requestedTurnOn(const EmuID_t& id);

    /*
     * Returns true if the user is connected to the emulator specific by id
     * NOTE: id should be checked to be a valid emu before calling
     */
    bool isConnectedTo(const EmuID_t& id);

    /*
     * Removes user's connected status on the specified emu id
     */
    void disconnect(const EmuID_t& id);

    /*
     * Adds the connected status to the user for the specified emu id
     */
    void connect(const EmuID_t& id);

    /*
     * Removes any occurences of id from the turn, requested turn, and
     * connection vector
     */
    void cleanupID(const EmuID_t& id);
};
