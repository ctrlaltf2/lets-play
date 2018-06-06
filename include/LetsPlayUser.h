class LetsPlayUser;
#pragma once
#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "Config.h"
#include "LetsPlayServer.h"

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
     * The emulator the user is connected to
     */
    EmuID_t m_connectedEmu;

    /*
     * Mutex for accessing username or connectedEmu
     */
    std::mutex m_access;

   public:
    /*
     * if the user has a turn on the
     */
    std::atomic<bool> hasTurn;

    /*
     * Lookup map of emulator ID to if the user has requested a turn on that emu
     */
    std::atomic<bool> requestedTurn;

    LetsPlayUser() : m_lastHeartbeat{std::chrono::steady_clock::now()} {}

    /*
     * Returns true if the user's last heartbeat was over the limit for timeout
     * @return True if the user should be disconnected
     */
    bool shouldDisconnect() const;

    // The reasoning behind the following redundant and
    // cs-major-just-introduced-to-java-classes-esque getters and setters is for
    // thread safe access / modification

    /*
     * Returns what emu (if any) the user if connected to
     */
    EmuID_t connectedEmu();

    /*
     * Set m_connectedEmu
     */
    void setConnectedEmu(const EmuID_t& id);

    /*
     * Return the username
     */
    std::string username();

    /*
     * Set the username
     */
    void setUsername(const std::string& name);
};
