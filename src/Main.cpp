#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "EmulatorController.h"
#include "RetroCore.h"
#include "Scheduler.h"
#include "Server.h"

int main(int argc, char** argv) {
    // std::thread runner(EmulatorController, argv[1], argv[2]);
    // runner.join();
    // std::thread(SNESController, argv[1], argv[2]);
    // LetsPlayServer server;
    // server.Run(std::stoul(std::string(argv[1])));
    // std::cout << "Server >>didn't<< crash while shutting down" << '\n';
    Scheduler s;
    std::function<void()> one = []() { std::cout << "one" << '\n'; };
    std::function<void()> two = []() { std::cout << "two" << '\n'; };
    s.Schedule(one, std::chrono::seconds(1));
    s.Schedule(two, std::chrono::seconds(2));

    while (true)
        std::this_thread::sleep_until(std::chrono::steady_clock::now() +
                                      std::chrono::hours(24));
}
