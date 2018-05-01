#include <iostream>
#include <thread>

#include "RetroCore.h"

#include "Controllers.h"

int main(int argc, char** argv) {
    std::thread runner(GBAController, argv[1], argv[2]);
    runner.join();
    // std::thread(SNESController, argv[1], argv[2]);
}
