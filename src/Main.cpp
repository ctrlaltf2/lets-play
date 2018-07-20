#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#include "boost/program_options.hpp"

#include "EmulatorController.h"
#include "LetsPlayServer.h"
#include "RetroCore.h"

int main(int argc, char** argv) {
    std::uint16_t port{3074};

    std::string configPath{"~/.letsplay/config.json"};
    std::filesystem::path configFilePath;

    try {
        using namespace boost;
        program_options::options_description desc{"Options"};
        // clang-format off
        desc.add_options()("help,h", "Help")
            ("config", program_options::value<std::string>(),   "Config file path")
            ("port",   program_options::value<std::uint16_t>(), "Port to run the server on");
        // clang-format on

        program_options::variables_map vm;
        program_options::store(program_options::parse_command_line(argc, argv, desc), vm);

        if (vm.count("help")) {
            std::cout << desc << '\n';
            return 0;
        }

        if (vm.count("port")) {
            port = vm["port"].as<std::uint16_t>();
        }

        if (vm.count("config")) {
            configPath = vm["config"].as<std::string>();
        }

        if (configPath.front() == '~') {
            const char* homePath = std::getenv("HOME");
            if (!homePath) {
                std::cerr << "Tilde path was specified but couldn't retrieve "
                             "actual home path. Check if $HOME was declared.\n";
                return -1;
            }

            std::clog << configPath << __LINE__ << '\n';
            configPath.erase(0, 1);
            std::clog << configPath << __LINE__ << '\n';
            configPath.insert(0, homePath);
            std::clog << configPath << __LINE__ << '\n';
        }
        configFilePath = configPath;

        if (!std::filesystem::exists(configFilePath)) {
            std::cerr << "Warning: config file doesn't exist" << '\n';
        }
    } catch (const boost::program_options::error& e) {
        std::cerr << e.what() << '\n';
    }
    LetsPlayServer server(configFilePath);
    server.Run(port);
    std::cout << "Server >>didn't<< crash while shutting down" << '\n';
}
