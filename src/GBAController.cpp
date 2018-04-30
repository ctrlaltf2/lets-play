#include "GBAController.h"

GBAController::GBAController(const char* corePath, const char* romPath)
    : m_Core(corePath) {
    // TODO: Modernize
    retro_system_info system = {0};
    retro_game_info info = {romPath, 0};
    FILE* file = fopen(romPath, "rb");

    if (!file) {
        std::cerr << "Invalid file '" << romPath << '\'' << '\n';
        std::exit(-4);
    }

    fseek(file, 0, SEEK_END);
    info.size = ftell(file);
    rewind(file);

    (*(m_Core.fGetSystemInfo))(&system);

    if (!system.need_fullpath) {
        info.data = malloc(info.size);

        if (!info.data || !fread((void*)info.data, info.size, 1, file)) {
            std::cerr << "Some error about sizing" << '\n';
            std::exit(-5);
        }
    }

    if (!(*(m_Core.fLoadGame))(&info)) {
        std::cerr << "failed to do a thing" << '\n';
        std::exit(-6);
    }
}

bool GBAController::OnEnvironment(unsigned cmd, char* data) {
    // As of now, ignore all environment requests (mark as "unknown")
    return false;
}

void GBAController::OnScreenUpdate(const void* data, unsigned width,
                                   unsigned height, size_t stride) {
    if (this->width != width || this->height != height) {
        // TODO: Repaint full screen
        this->width = width;
        this->height = height;
    } else {
        // Update screen vector, find diff, send it off
    }
}

void GBAController::OnInputPoll() {
    // Accept input from current user as input to the emulator
    // Update buttonMask
}

std::int16_t GBAController::OnGetInputState(unsigned port, unsigned device,
                                            unsigned index, unsigned id) {
    //...?
}

void GBAController::OnAudioPacket(std::int16_t left, std::int16_t right) {
    // Send audio packet
}
size_t GBAController::OnBatchAudioPacket(const std::int16_t* data,
                                         size_t frames) {
    // Send audio packets (implement first)
}

void GBAController::Run() {
    while (true) (*(m_Core.fRun))();
}
