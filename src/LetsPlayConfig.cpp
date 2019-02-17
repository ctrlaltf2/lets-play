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
        "salt": "ncft9PlmVA",
        "adminHash": "be23396d825c5a17c57c7738ac4b98a5",
        "jpegQuality": 80,
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
    std::unique_lock<std::shared_timed_mutex> lk(mutex, std::try_to_lock);
    if (lib::filesystem::exists(m_configPath) && lib::filesystem::is_regular_file(m_configPath)) {
        std::ifstream fi(m_configPath.string());
        fi >> config;
    } else {
        // Warn or execption
    }
}

void LetsPlayConfig::LoadFrom(const lib::filesystem::path& path) {
    std::clog << "Loading file" << '\n';
    std::unique_lock<std::shared_timed_mutex> lk(mutex);
    if (lib::filesystem::exists(path) && lib::filesystem::is_regular_file(path)) {
        std::clog << "Valid path: " << path << '\n';
        m_configPath = path;
        ReloadConfig();
    } else {
        // Warn or exception
    }
}

void LetsPlayConfig::SaveConfig() {
    std::shared_lock<std::shared_timed_mutex> lk(mutex);
    std::ofstream fo(m_configPath.string());
    fo << std::setw(4) << config;
}

LetsPlayConfig::~LetsPlayConfig() {
    SaveConfig();
}
