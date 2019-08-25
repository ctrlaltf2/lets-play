#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "boost/program_options.hpp"

#include "common/filesystem.h"

#include "EmulatorController.h"
#include "LetsPlayServer.h"
#include "RetroCore.h"

int main(int argc, char **argv) {
    std::uint16_t port{8080};

    lib::filesystem::path configPath; // default: ($XDG_CONFIG_HOME || $HOME/.config)/letsplay/config.json
    const char *cXDGConfigHome = std::getenv("XDG_CONFIG_HOME");
    if (cXDGConfigHome)
        configPath = lib::filesystem::path(cXDGConfigHome) / "letsplay" / "config.json";
    else
#if defined(__unix__) || defined(__APPLE__)
        configPath = lib::filesystem::path(std::getenv("HOME")) / ".config" / "letsplay" / "config.json";
#else
		// TODO(modeco80): probably finalize and discuss where things should actually go,
		// but for now, this should hopefully cause things to not crash on windows
		configPath = lib::filesystem::path(std::getenv("LOCALAPPDATA")) / "letsplay" / "config.json";
#endif

    try {
        using namespace boost;
        program_options::options_description desc{"Options"};
        // clang-format off
        desc.add_options()("help,h", "Help")
            ("config", program_options::value<std::string>(), "Config file path")
            ("port", program_options::value<std::uint16_t>(), "Port to run the server on");
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
            configPath = LetsPlayServer::escapeTilde(vm["config"].as<std::string>());
        }

        if (lib::filesystem::create_directories(configPath.parent_path()))
            std::cerr << "Warning: Config file didn't initially exist. Creating directories." << '\n';

    } catch (const boost::program_options::error& e) {
        std::cerr << e.what() << '\n';
    }
    LetsPlayServer server(configPath);

    bool retry{true};
    while (retry) {
        try {
            server.Run(port);
            retry = false;
        } catch (const std::runtime_error &e) {
            port++;
        }
    }
    std::cout << "Server >>didn't<< crash while shutting down" << '\n';
}
