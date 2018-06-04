#include "EmulatorController.h"
#include <memory>

namespace EmulatorController {

thread_local RetroCore Core;
thread_local bool initialized = false;
void Run(const char* corePath, const char* romPath) {
    Core = RetroCore(corePath);

    std::cout << "setEnvironment: " << &(Core.fSetEnvironment) << '\n';
    std::cout << "OnEnvironment: " << std::addressof(OnEnvironment) << '\n';
    std::cout << __LINE__ << '\n';
    (*(Core.fSetEnvironment))(OnEnvironment);
    std::cout << __LINE__ << '\n';
    (*(Core.fSetVideoRefresh))(&OnVideoRefresh);
    std::cout << __LINE__ << '\n';
    (*(Core.fSetInputPoll))(OnPollInput);
    std::cout << __LINE__ << '\n';
    (*(Core.fSetInputState))(OnGetInputState);
    std::cout << __LINE__ << '\n';
    (*(Core.fSetAudioSample))(OnLRAudioSample);
    std::cout << __LINE__ << '\n';
    (*(Core.fSetAudioSampleBatch))(OnBatchAudioSample);
    std::cout << __LINE__ << '\n';

    (*(Core.fInit))();
    std::cout << __LINE__ << '\n';

    // TODO: C++
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
            fclose(file);
            return;
        }
    }

    if (!((*(Core.fLoadGame))(&info))) {
        std::cout << "failed to do a thing" << '\n';
    }

    while (true) (*(Core.fRun))();
}

bool OnEnvironment(unsigned cmd, void* data) {
    switch (cmd) {
        default:
            return false;
    }
}

void OnVideoRefresh(const void* data, unsigned width, unsigned height,
                    size_t stride) {
    std::cout << width << ' ' << height << ' ' << stride << '\n';
}

void OnPollInput() {}

std::int16_t OnGetInputState(unsigned port, unsigned device, unsigned index,
                             unsigned id) {
    return 0;
}

void OnLRAudioSample(std::int16_t left, std::int16_t right) {}

size_t OnBatchAudioSample(const std::int16_t* data, size_t frames) {
    return frames;
}
}  // namespace EmulatorController
