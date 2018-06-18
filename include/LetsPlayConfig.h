class LetsPlayConfig;
#pragma once

#include <filesystem>

#include "nlohmann/json.hpp"

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
     */
    nlohmann::json LetsPlayConfig::getServerSetting(const std::string& setting);

    /*
     * Get a core setting, based on the core's name. See libretro wiki page on
     * each core to see what every core reports itself as, as well as the
     * settings the core recognizes. Some of these settings that you'd expect to
     * see include sensor levels, if a core should look for a bios file, screen
     * rotation modes, and more.
     * @param coreName The name of the core (see the libretro core's wiki page,
     * under the Directories header it will say what the core reports itself as)
     * @param setting Which setting to get
     */
    nlohmann::json LetsPlayConfig::getCoreSetting(const std::string& coreName,
                                                  const std::string& setting) {}
    /*
     * Writes the current config to the disk
     */
    ~LetsPlayConfig();
};
