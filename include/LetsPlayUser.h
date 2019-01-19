class LetsPlayUser;

#pragma once
#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "boost/uuid/uuid.hpp"
#include "boost/uuid/uuid_generators.hpp"
#include "boost/uuid/uuid_io.hpp"

#include "Config.h"
#include "LetsPlayServer.h"

namespace uuid = boost::uuids;

extern uuid::random_generator g_UUIDGen;
extern std::mutex g_uuidMutex;

/*
 * Class representing a connected user (across all emulators)
 */
class LetsPlayUser {
    /*
     * Time point of the pong message sent
     */
    std::chrono::time_point<std::chrono::steady_clock> m_lastPong;

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

    /**
     * UUID to identify the user
     */
    uuid::uuid m_uuid;

  public:
    /*
     * if the user has a turn on their emu
     */
    std::atomic<bool> hasTurn;

    /*
     * Lookup map of emulator ID to if the user has requested a turn on that emu
     */
    std::atomic<bool> requestedTurn;

    /*
     * Whether or not the user is connected to the socket anymore
     */
    std::atomic<bool> connected;

    LetsPlayUser();

    // The reasoning behind the following redundant and
    // cs-major-just-introduced-to-java-classes-esque getters and setters is for
    // thread safe access / modification

    // TODO: Check for where double-check locking is required

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

    /*
     * Get the uuid as a string
     */
    std::string uuid() const;

    /*
     * Updates pong time
     */
    void updateLastPong();

    /*
     * Whether or not the user should disconnect (missed two pongs)
     */
    bool shouldDisconnect();
};
