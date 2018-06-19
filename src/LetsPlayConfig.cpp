#include "LetsPlayConfig.h"

// clang-format off
const nlohmann::json LetsPlayConfig::defaultConfig = R"json(
{
    "serverConfig": {
        "emulators": {
            "template": {
                "coreLocation": "./core",
                "fps": 60,
                "overrideFramerate": false,
                "romLocation": "./rom",
                "turnLength": "10s",
            }
        },
        "heartbeatTimeout": "3s",
        "maxMessageSize": 100,
        "maxUsernameLength": 15,
        "minUsernameLength": 3,
        "saveDirectory": "~/.letsplay/save/",
        "syncInterval": "5s",
        "systemDirectory": "~/.letsplay/system/",
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

    if (m_config["serverConfig"].count(setting))
        return m_config["serverConfig"][setting];

    // Fallback on default values
    if (defaultConfig["serverConfig"].count(setting))
        return defaultConfig["serverConfig"][setting];

    return nlohmann::json;
}

void LetsPlayConfig::setServerSetting(const std::string& setting,
                                      const nlohmann::json& value) {
    std::unique_lock<std::mutex> lk(m_configMutex);
    m_config["serverConfig"][setting] = value;
}

void LetsPlayConfig::setCoreSetting(const std::string& coreName,
                                    const std::string& setting,
                                    const nlohmann::json& value) {
    std::unique_lock<std::mutex> lk(m_configMutex);
    m_config["coreConfig"][coreName][setting] = value;
}

nlohmann::json LetsPlayConfig::getCoreSetting(const std::string& coreName,
                                              const std::string& setting) {
    std::unique_lock<std::mutex> lk(m_configMutex);

    if (m_config["coreConfig"][coreName].count(setting))
        return m_config["coreConfig"][coreName][setting];

    // Fallback on default
    if (defaultConfig["coreConfig"][coreName].count(setting))
        return defaultConfig["coreConfig"][coreName][setting];

    return nlohmann::json;
}

~LetsPlayConfig::LetsPlayConfig() {
    std::unique_lock<std::mutex> lk(m_configMutex);
    std::ofstream fo(m_configPath);
    fo << std::setw(4) << m_config;
}
