class LetsPlayConfig;
#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <variant>

#include "nlohmann/json.hpp"

using json_value_type = std::variant<
    std::nullptr_t, nlohhmann::basic_json::boolean_t,
    nlohmann::basic_json::string_t, nlohmann::basic_json::number_integer_t,
    nlohmann::basic_json::number_unsigned_t, nlohmann::basic_json::float_t,
    nlohmann::basic_json::object_t, nlohmann::basic_json::array_t>;

class LetsPlayConfig {
    /*
     * Json object holding the current config info
     */
    nlohmann::json m_config;

    /*
     * Path to the config file
     */
    std::filesystem::path m_configPath;

    /*
     * Mutex to make the config threadsafe
     */
    std::mutex m_configMutex;

   public:
    /*
     * Default config, if a value isn't contained in the (possibly loaded)
     * m_config, the server will fall back on these values
     */
    static const nlohmann::json defaultConfig;

    // TODO: Exception
    /*
     * Loads the file from the path m_configPath and reloads the config
     */
    void ReloadConfig();

    /*
     * Load a config from a path
     */
    void LoadFrom(const std::filesystem::path& path);

    /*
     * Get a specific config from m_config, either the server settings or the
     * core specific settings
     * @param setting The server setting to retrieve
     * @return The setting, if it exists
     */
    json_value_type getServerSetting(const std::string& setting);

    /*
     * Get a core setting, based on the core's name. See libretro wiki page on
     * each core to see what every core reports itself as, as well as the
     * settings the core recognizes. Some of these settings that you'd expect to
     * see include sensor levels, if a core should look for a bios file, screen
     * rotation modes, and more.
     * @param coreName The name of the core (see the libretro core's wiki page,
     * under the Directories header it will say what the core reports itself as)
     * @param setting Which setting to get
     * @return The setting, if it exists
     */
    json_value_type getCoreSetting(const std::string& coreName,
                                   const std::string& setting);

    /*
     * Similar semantics to getServerSetting except it safely sets a server
     * setting
     * @param setting The name of the server setting to lookup and modify
     * @param value The value to set the setting to
     */
    void setServerSetting(const std::string& setting,
                          const nlohmann::json& value);

    /*
     * Similar semantics to getCoreSetting except it safely sets a setting
     * @param coreName The name of the core
     * @param setting Which setting to set
     * @param value The value to set it to
     */

    void setCoreSetting(const std::string& coreName, const std::string& setting,
                        const nlohmann::json& value);

    /*
     * Retrieve an emu-specific setting such as turn length or framerate
     * overrides
     * @param id the emu id
     * @param The setting to return, if it exists
     * @return The value of the setting, if it exists (nlohmann::json()
     * otherwise)
     */
    json_value_type getEmuSetting(const std::string& id,
                                  const std::string& setting);

    /*
     * Set an emu-specific setting
     * @param id The id of the emu
     * @param setting The setting to set
     * @param value the value to set the setting to
     */
    void setEmuSetting(const std::string& id, const std::string& setting,
                       const std::string& value);

    /*
     * Creates an emulator under serverConfig->emulators equal to the id if it
     * does not already exist
     * @param id the id to check for and create if nonexistent
     */
    void createEmuIfNotExist(const std::string& id);

    /*
     * Writes the current config to the disk
     */
    void SaveConfig();

    ~LetsPlayConfig();

    static json_value_type jsonToValueType(const nlohmann::json& j);
};
