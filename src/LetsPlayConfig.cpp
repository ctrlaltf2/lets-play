#include "LetsPlayConfig.h"

// clang-format off
// All durations are in ms
nlohmann::json LetsPlayConfig::defaultConfig = R"json(
{
    "serverConfig": {
        "emulators": {
            "template": {
                "coreLocation": "./core",
                "romLocation": "./rom",
                "turnLength": 10000,
                "overrideFramerate": false,
                "forbiddenCombos": [],
                "fps": 60,
                "muting": {
                    "messagesPerInterval": 3,
                    "intervalTime": 4,
                    "muteTime": 5,
                    "renameCooldown": 1000
                }
            }
        },
        "backups": {
            "backupInterval": 1440,
            "historyInterval": 5,
            "maxHistorySize": 288
        },
        "salt": "ncft9PlmVA",
        "adminHash": "be23396d825c5a17c57c7738ac4b98a5",
        "dataDirectory": "System Default",
        "jpegQuality": 80,
        "heartbeatTimeout": 3000,
        "maxMessageSize": 100,
        "maxUsernameLength": 15,
        "minUsernameLength": 3,
        "usernameChangeCooldown": 5000,
        "syncInterval": 5000
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
    std::unique_lock<std::shared_timed_mutex> lk(mutex);
    m_configPath = path;
    if (lib::filesystem::exists(path) && lib::filesystem::is_regular_file(path)) {
        ReloadConfig();
    } else {
        config = LetsPlayConfig::defaultConfig;
        SaveConfig();
    }
}

void LetsPlayConfig::SaveConfig() {
    std::shared_lock<std::shared_timed_mutex> lk(mutex, std::try_to_lock);
    std::ofstream fo(m_configPath.string());
    fo << std::setw(4) << config;
}

LetsPlayConfig::~LetsPlayConfig() {
    SaveConfig();
}
