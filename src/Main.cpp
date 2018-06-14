#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "EmulatorController.h"
#include "LetsPlayServer.h"
#include "RetroCore.h"

int main(int argc, char** argv) {
    LetsPlayServer server;
    server.Run(std::stoul(std::string(argv[1])));
    std::cout << "Server >>didn't<< crash while shutting down" << '\n';
}
