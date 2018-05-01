#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "RetroCore.h"

void EmulatorController(const char* corePath, const char* romPath) {
    RetroCore Core(corePath);

    std::cout << "Done Loading" << '\n';
    (*(Core.fSetEnvironment))(
        [](unsigned cmd, void* data) -> bool { return false; });

    (*(Core.fSetVideoRefresh))([](const void* data, unsigned width,
                                  unsigned height, size_t stride) -> void {
        std::cout << width << 'x' << height << '@' << stride << '\n';
    });

    (*(Core.fSetInputPoll))([]() -> void {
        //...
    });

    (*(Core.fSetInputState))([](unsigned port, unsigned device, unsigned index,
                                unsigned id) -> std::int16_t { return 0; });

    (*(Core.fSetAudioSample))(
        [](std::int16_t left, std::int16_t right) -> void {
            // ...
        });

    (*(Core.fSetAudioSampleBatch))(
        [](const std::int16_t* data, size_t frames) -> size_t {
            return frames;
        });

    (*(Core.fInit))();
    std::cout << "Done and ready to load " << '\n';

    retro_system_av_info av = {0};
    retro_system_info system = {0};
    retro_game_info info = {romPath, 0};
    FILE* file = fopen(romPath, "rb");

    if (!file) {
        std::cout << "invalid file" << '\n';
        return;
    }

    fseek(file, 0, SEEK_END);
    info.size = ftell(file);
    rewind(file);

    (*(Core.fGetSystemInfo))(&system);

    if (!system.need_fullpath) {
        info.data = malloc(info.size);

        if (!info.data || !fread((void*)info.data, info.size, 1, file)) {
            std::cout << "Some error about sizing" << '\n';
            return;
        }
    }

    if (!((*(Core.fLoadGame))(&info))) {
        std::cout << "failed to do a thing" << '\n';
    }

    while (true) (*(Core.fRun))();
}
