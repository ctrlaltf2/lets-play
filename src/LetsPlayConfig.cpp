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

json_value_type LetsPlayConfig::getServerSetting(const std::string& setting) {
    std::unique_lock<std::mutex> lk(m_configMutex);

    if (m_config["serverConfig"].count(setting))
        return LetsPlayConfig::jsonToValueType(
            m_config["serverConfig"][setting]);

    // Fallback on default values
    if (defaultConfig["serverConfig"].count(setting))
        return LetsPlayConfig::jsonToValueType(
            defaultConfig["serverConfig"][setting]);

    return nlohmann::json();
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

json_value_type LetsPlayConfig::getCoreSetting(const std::string& coreName,
                                               const std::string& setting) {
    std::unique_lock<std::mutex> lk(m_configMutex);

    if (m_config["coreConfig"][coreName].count(setting))
        return LetsPlayConfig::jsonToValueType(
            m_config["coreConfig"][coreName][setting]);

    // Fallback on default
    if (defaultConfig["coreConfig"][coreName].count(setting))
        return LetsPlayConfig::jsonToValueType(
            defaultConfig["coreConfig"][coreName][setting]);

    return nlohmann::json();
}

json_value_type LetsPlayConfig::getEmuSetting(const std::string& id,
                                              const std::string& setting) {
    std::unique_lock<std::mutex> lk(m_configMutex);
    if (m_config["serverConfig"]["emulators"][id].count(setting))
        return LetsPlayConfig::jsonToValueType(
            m_config["serverConfig"]["emulators"][id][setting]);

    if (defaultConfig["serverConfig"]["emulators"]["template"].count(setting))
        return LetsPlayConfig::jsonToValueType(
            defaultConfig["serverConfig"]["emulators"]["emulators"][setting]);

    json_value_type t = nullptr;
    return t;
}

void LetsPlayConfig::setEmuSetting(const std::string& id,
                                   const std::string& setting,
                                   const std::string& value) {
    std::unique_lock<std::mutex> lk(m_configMutex);
    if (m_config["serverConfig"]["emulators"][id].count(setting))
        m_config["serverConfig"]["emulators"][id][setting] = value;
}

void LetsPlayConfig::createEmuIfNotExist(const std::string& id) {
    std::unique_lock<std::mutex> lk(m_configMutex);
    if (!m_config["serverConfig"]["emulators"].count(id))
        m_config["serverConfig"]["emulators"][id] =
            LetsPlayConfig::defaultConfig["serverConfig"]["emulators"]
                                         ["template"];
}

void LetsPlayConfig::SaveConfig() {
    std::unique_lock<std::mutex> lk(m_configMutex);
    std::ofstream fo(m_configPath);
    fo << std::setw(4) << m_config;
}

LetsPlayConfig::~LetsPlayConfig() { SaveConfig(); }

static json_value_type jsonToValueType(const nlohmann::json& j) {
    json_value_type t;
    switch (j.type()) {
        case value_t::null:
            t = nullptr;
            break;
        case value_t::boolean:
            t = j.get<nlohmann::basic_json::boolean_t>();
            break;
        case value_t::string:
            t = j.get<nlohmann::basic_json::string_t>();
            break;
        case value_t::number_integer:
            t = j.get<nlohmann::basic_json::number_integer_t>();
            break;
        case value_t::number_unsigned:
            t = j.get<nlohmann::basic_json::number_unsigned_t>();
            break;
        case value_t::number_float:
            t = j.get<nlohmann::basic_json::number_float_t>();
            break;
        case value_t::object:
            t = j.get<nlohmann::basic_json::object_t>();
            break;
        case value_t::array:
            t = j.get<nlohmann::basic_json::array_t>();
            break;
        case value_t::discarded:
            break;
    }
    return t;
}
