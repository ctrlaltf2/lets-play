/**
 * @file LetsPlayConfig.h
 *
 * @author ctrlaltf2
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

#include <nlohmann/json.hpp>

#include <boost/filesystem.hpp>

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
    boost::filesystem::path m_configPath;

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
    static nlohmann::json defaultConfig;

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
    void LoadFrom(const boost::filesystem::path& path);

    /**
     * Writes the current config to the disk
     */
    void SaveConfig();

    ~LetsPlayConfig();

    // 1
    template<typename ReturnType, typename... Keys>
    ReturnType get(nlohmann::json::value_t expectedType, std::string key, Keys... k) {
        std::lock_guard<std::shared_timed_mutex> lk(mutex);
        try {
            nlohmann::json j = get(config[key], k...);
            ReturnType rt = j.get<ReturnType>();
            if (j.type() != expectedType) { // not in config or is wrong data type
                j = get(LetsPlayConfig::defaultConfig[key], k...);
                rt = j.get<ReturnType>();
            }
            return rt;

        } catch (const nlohmann::json::type_error &e) {
            nlohmann::json j = get(LetsPlayConfig::defaultConfig[key], k...);
            return j.get<ReturnType>();
        }
    }

    // 2, n-1
    template<typename... Keys>
    nlohmann::json &get(nlohmann::json &j, std::string key, Keys... k) {
        return get(j[key], k...);
    }

    // n;
    nlohmann::json &get(nlohmann::json &j, std::string key) {
        return j[key];
    }

    template<typename ...Keys>
    void set(std::string key, Keys... k) {
        {
            std::unique_lock<std::shared_timed_mutex> lk(mutex);
            setImpl(config, key, k...);
        }
        SaveConfig();
    }

    template<typename ...Keys>
    void setImpl(nlohmann::json& t, std::string key, Keys... k) {
        setImpl(t[key], k...);
    }

    template<typename Value>
    void setImpl(nlohmann::json& t, std::string key, Value v) {
        t[key] = v;
    }
};