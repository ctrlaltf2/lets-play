#include "RetroCore.h"

RetroCore::RetroCore() {}

// RetroCore::RetroCore(const char* corePath) {
//    void* hCore = dlopen(corePath, RTLD_NOW);
//
//    // TODO: Exception
//    if (hCore == nullptr) {
//        std::cerr << "Failed to load libretro core -- " << dlerror() << '\n';
//        std::exit(-1);
//    }
//
//    Init(hCore);
//}

// RetroCore::RetroCore(void* hCore) { Init(hCore); }

void RetroCore::Init(const char* corePath) {
    std::clog << "Loading file from '" << corePath << "'\n";
    void* hCore = dlopen(corePath, RTLD_NOW);

    // TODO: Exception
    if (hCore == nullptr) {
        std::cerr << "Failed to load libretro core -- " << dlerror() << '\n';
        std::exit(-1);
    }

    // TODO: Exception
    if (hCore == nullptr) {
        std::cerr << "RetroCore handle points to nothing\n";
        std::exit(-2);
    }
    this->m_hCore = hCore;

    // Load functions from hCore

    RetroCore::Load(this->m_hCore, fSetEnvironment, "retro_set_environment");
    RetroCore::Load(this->m_hCore, fSetVideoRefresh, "retro_set_video_refresh");
    RetroCore::Load(this->m_hCore, fSetInputPoll, "retro_set_input_poll");
    RetroCore::Load(this->m_hCore, fSetInputState, "retro_set_input_state");
    RetroCore::Load(this->m_hCore, fSetAudioSample, "retro_set_audio_sample");
    RetroCore::Load(this->m_hCore, fSetAudioSampleBatch,
                    "retro_set_audio_sample_batch");

    RetroCore::Load(this->m_hCore, fInit, "retro_init");
    RetroCore::Load(this->m_hCore, fDeinit, "retro_deinit");
    RetroCore::Load(this->m_hCore, fRetroAPIVersion, "retro_api_version");
    RetroCore::Load(this->m_hCore, fGetSystemInfo, "retro_get_system_info");
    RetroCore::Load(this->m_hCore, fGetAudioVideoInfo,
                    "retro_get_system_av_info");
    RetroCore::Load(this->m_hCore, fSetControllerPortDevice,
                    "retro_set_controller_port_device");
    RetroCore::Load(this->m_hCore, fReset, "retro_reset");
    RetroCore::Load(this->m_hCore, fRun, "retro_run");
    RetroCore::Load(this->m_hCore, fLoadGame, "retro_load_game");
    RetroCore::Load(this->m_hCore, fUnloadGame, "retro_unload_game");
}

RetroCore::~RetroCore() {
    (*(this->fUnloadGame))();
    (*(this->fDeinit))();
    dlclose(m_hCore);
}
