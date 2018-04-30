#include "GBAController.h"

GBAController::GBAController(const char* corePath, const char* romPath) {
    this->m_Core = RetroCore(corePath);

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

    m_Core.fGetSystemInfo(&system);

    if (!system.need_fullpath) {
        info.data = malloc(info.size);

        if (!info.data || !fread((void*)info.data, info.size, 1, file)) {
            std::cerr << "Some error about sizing" << '\n';
            std::exit(-5);
        }
    }

    if (!m_Core.fLoadGame(&info)) {
        std::cerr << "failed to do a thing" << '\n';
        std::exit(-6);
    }
}
