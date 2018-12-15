class LetsPlayConfig;

#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <variant>

#include "nlohmann/json.hpp"

class LetsPlayConfig {
    /*
     * Path to the config file
     */
    std::filesystem::path m_configPath;

  public:
    /*
     * Mutex to make the config threadsafe, locked by threads that directly
     * access the config object
     */
    std::shared_mutex mutex;
    /*

     * Json object holding the current config info
     */
    nlohmann::json config;

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
     * Writes the current config to the disk
     */
    void SaveConfig();

    ~LetsPlayConfig();
};
