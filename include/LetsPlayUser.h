/**
 * @file LetsPlayUser.h
 *
 * @author ctrlaltf2
 *
 */
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

#include "common/typedefs.h"
#include "LetsPlayServer.h"

namespace uuid = boost::uuids;

extern uuid::random_generator g_UUIDGen;
extern std::mutex g_uuidMutex;

/**
 * @class LetsPlayUser
 *
 * Class representing a connected user (across all emulators)
 */
class LetsPlayUser {
    /**
     * Time point of the pong message sent
     */
    std::chrono::time_point<std::chrono::steady_clock> m_lastPong;

    /**
     * Username for the user
     */
    std::string m_username;

    /**
     * The emulator the user is connected to
     */
    EmuID_t m_connectedEmu;

    /**
     * Mutex for accessing username or connectedEmu
     */
    std::mutex m_access;

    /**
     * UUID to identify the user
     */
    uuid::uuid m_uuid;

    /**
     * The IP string for the user
     */
    std::string m_ip;

    /**
     * List that stores the timestamps for the last n messages the user sent where n is the value specified in
     * config["serverConfig"]["emulators"][emuID]["muting"]["messagesPerInterval"]
     */
    std::vector<std::chrono::time_point<std::chrono::steady_clock>> m_messageTimestamps;

    /**
     * Time point for the last username change
     */
    std::chrono::time_point<std::chrono::steady_clock> m_lastUsernameChange;

    /**
     * Time point for when the user was muted
     */
    std::chrono::time_point<std::chrono::steady_clock> m_muteTime;

    /**
     * Mutex for muting related objects
     */
    std::mutex m_muting;


public:
    /**
     * if the user has a turn on their emu
     */
    std::atomic<bool> hasTurn;

    /**
     * Lookup map of emulator ID to if the user has requested a turn on that emu
     */
    std::atomic<bool> requestedTurn;

    /**
     * Whether or not the user is connected to the socket anymore
     */
    std::atomic<bool> connected;

    /**
     * Whether or not the user is an authenticated admin
     */
    std::atomic<bool> hasAdmin;

    /**
     * How many admin attempts the user has made
     */
    std::atomic<std::uint32_t> adminAttempts;

    LetsPlayUser();

    /*
     * The reasoning behind the following redundant and
     * cs-major-just-introduced-to-java-classes-esque getters and setters is for
     * thread safe access / modification
     */

    // TODO: Check for where double-check locking is required

    /**
     * Returns what emu (if any) the user if connected to
     */
    EmuID_t connectedEmu();

    /**
     * Set m_connectedEmu
     */
    void setConnectedEmu(const EmuID_t& id);

    /**
     * Return the username
     */
    std::string username();

    /**
     * Set the username
     */
    void setUsername(const std::string& name);

    /**
     * Get the uuid as a string
     */
    std::string uuid() const;

    /**
     * Set the IP string for the user
     */
    void setIP(const std::string& ip);

    /**
     * Get the IP string for the user
     */
    std::string IP() const;

    /**
     * Update pong time to now
     */
    void updateLastPong();

    /**
     * Whether or not the user should disconnect (missed two pongs)
     */
    bool shouldDisconnect();
};