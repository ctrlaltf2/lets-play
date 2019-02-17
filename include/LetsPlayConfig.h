/**
 * @file LetsPlayConfig.h
 *
 * @author ctrlaltf2
 *
 * @section LICENSE
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  @section DESCRIPTION
 *  Helper class that stores default and loaded-from-a-file configs for Let's Play.
 */

class LetsPlayConfig;

#pragma once

#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <shared_mutex>

#include "nlohmann/json.hpp"

#include "common/filesystem.h"

/**
 * @class LetsPlayConfig
 *
 * Helper class that stores default and loaded-from-a-file configs for Let's Play.
 * @todo Create a variadic templated function that can pull the config from the
 * default or loaded-from-a-file config and return a result.
 */
class LetsPlayConfig {
    /**
     * Path to the config file
     */
    lib::filesystem::path m_configPath;

  public:
    /**
     * Mutex to make the config thread-safe
     *
     * @note This is locked by threads that directly access the config object.
     */
    std::shared_timed_mutex mutex;

    /**
     * JSON object holding the current config info
     */
    nlohmann::json config;

    /**
     * Default config, if a value isn't contained in the (possibly loaded)
     * m_config, the server will fall back on these values
     */
    static const nlohmann::json defaultConfig;

    /**
     * Loads the file from the path m_configPath and reloads the config
     *
     * @todo When exceptions are implemented, throw when an invalid path
     * is given.
     */
    void ReloadConfig();

    /**
     * Load a config from a path
     *
     * @param path The path to load the file from
     */
    void LoadFrom(const lib::filesystem::path& path);

    /**
     * Writes the current config to the disk
     */
    void SaveConfig();

    ~LetsPlayConfig();

    // TODO: Variadic function, string..., nlohmann::json::type. Warn or error onmismatched datatype or nonexistent value. Falls back on default config. A call might look like nlohmann::json val = config.pull("serverConfig", "emulators", "emu1", "turnLength", nlohmann::json::value_t::number_unsigned);
};
