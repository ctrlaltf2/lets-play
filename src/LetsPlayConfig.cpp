#include "LetsPlayConfig.h"

// clang-format off
const nlohmann::json LetsPlayConfig::defaultConfig = R"json(
{
    "serverConfig": {
        "syncInterval": "5s",
        "maxMessageSize": 100,
        "maxUsernameLength": 15,
        "minUsernameLength": 3,
        "turnLength": "10s",
        "heartbeatTimeout": "3s",
        "overrideFramerate": false,
        "framerate": 60,
        "systemDirectory": "~/.letsplay/system/",
        "saveDirectory": "~/.letsplay/save/"
    },
    "coreConfig": {
        "Snes9x": {
            "snes9x_up_down_allowed": "enabled"
        },
        "mGBA": {
            "mgba_solar_sensor_level": 5
        }
    }
}
)json"_json;
// clang-format on

void LetsPlayConfig::ReloadConfig() {
    std::unique_lock<std::mutex> lk(m_configMutex, std::try_to_lock);
    if (std::filesystem::exists(m_configPath)) {
        std::ifstream fi(m_configPath);
        fi >> m_config;
    }
}

void LetsPlayConfig::LoadFrom(const std::filesystem::path& path) {
    std::unique_lock<std::mutex> lk(m_configMutex);
    if (std::filesystem::exists(path)) {
        m_configPath = path;
        ReloadConfig();
    }
}

nlohmann::json LetsPlayConfig::getServerSetting(const std::string& setting) {
    std::unique_lock<std::mutex> lk(m_configMutex);

    if (!m_config.count("serverConfig") ||
        !m_config["serverConfig"].count(setting)) {
        // Fallback on default values
        if (!defaultConfig["serverConfig"].count(setting))
            return nlohmann : json;
        else
            return defaultConfig["serverConfig"][setting];
    }

    return m_config["serverConfig"][setting];
}

nlohmann::json LetsPlayConfig::getCoreSetting(const std::string& coreName,
                                              const std::string& setting) {
    std::unique_lock<std::mutex> lk(m_configMutex);

    if (!m_config.count("coreConfig") ||
        !m_config["coreConfig"].count(coreName) ||
        !m_config["coreConfig"][coreName].count(setting)) {
        // Not inside the config that was loaded from disk, so fallback on
        // checking the default config
        if (!m_config["coreConfig"].count(coreName) ||
            !m_config["coreConfig"][coreName].count(setting)) {
            return nlohmann::json;
        } else {  // Value exists in default and not disk
            return defaultConfig["coreConfig"][coreName][setting];
        }
    }

    return m_config["coreConfig"][coreName][setting];
}

~LetsPlayConfig::LetsPlayConfig() {
    std::unique_lock<std::mutex> lk(m_configMutex);
    std::ofstream fo(m_configPath);
    fo << std::setw(4) << m_config;
}
