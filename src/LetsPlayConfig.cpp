#include "LetsPlayConfig.h"

// clang-format off
// All durations are in ms
const nlohmann::json LetsPlayConfig::defaultConfig = R"json(
{
    "serverConfig": {
        "emulators": {
            "template": {
                "coreLocation": "./core",
                "fps": 60,
                "overrideFramerate": false,
                "romLocation": "./rom",
                "turnLength": 10000
            }
        },
        "heartbeatTimeout": 3000,
        "maxMessageSize": 100,
        "maxUsernameLength": 15,
        "minUsernameLength": 3,
        "saveDirectory": "~/.letsplay/save/",
        "syncInterval": 5000,
        "systemDirectory": "~/.letsplay/system/"
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
    if (std::unique_lock<std::shared_mutex> lk(mutex, std::try_to_lock);
        std::filesystem::exists(m_configPath) &&
        std::filesystem::is_regular_file(m_configPath)) {
        std::ifstream fi(m_configPath);
        fi >> config;
    } else {
        // Warn or execption
    }
}

void LetsPlayConfig::LoadFrom(const std::filesystem::path& path) {
    std::clog << "Loading file" << '\n';
    if (std::unique_lock<std::shared_mutex> lk((mutex));
        std::filesystem::exists(path) &&
        std::filesystem::is_regular_file(path)) {
        std::clog << "Valid path: " << path << '\n';
        m_configPath = path;
        ReloadConfig();
    } else {
        // Warn or exception
    }
}

void LetsPlayConfig::SaveConfig() {
    std::shared_lock<std::shared_mutex> lk((mutex));
    std::ofstream fo(m_configPath);
    fo << std::setw(4) << config;
}

LetsPlayConfig::~LetsPlayConfig() { SaveConfig(); }
