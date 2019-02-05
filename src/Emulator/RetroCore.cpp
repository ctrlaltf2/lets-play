#include "RetroCore.h"

RetroCore::RetroCore() = default;

void RetroCore::Init(const char *corePath) {
    std::clog << "Loading file from '" << corePath << "'\n";

    std::string errInfo;
#ifdef WIN32
    void *hCore = LoadLibrary(corePath);
    
    if(!hCore)
        errInfo = "Windows error code " + std::to_string(GetLastError());
#else
    void *hCore = dlopen(corePath, RTLD_NOW);

    if(!hCore)
        errInfo = dlerror();
#endif

    // TODO: Exception
    if (!hCore) {
        std::cerr << "Failed to load libretro core -- " << errInfo << '\n';
        std::exit(-1);
    }

    this->m_hCore = hCore;

    // Load functions from hCore

    RetroCore::Load(this->m_hCore, fSetEnvironment, "retro_set_environment");
    RetroCore::Load(this->m_hCore, fSetVideoRefresh, "retro_set_video_refresh");
    RetroCore::Load(this->m_hCore, fSetInputPoll, "retro_set_input_poll");
    RetroCore::Load(this->m_hCore, fSetInputState, "retro_set_input_state");
    RetroCore::Load(this->m_hCore, fSetAudioSample, "retro_set_audio_sample");
    RetroCore::Load(this->m_hCore, fSetAudioSampleBatch, "retro_set_audio_sample_batch");

    RetroCore::Load(this->m_hCore, fInit, "retro_init");
    RetroCore::Load(this->m_hCore, fDeinit, "retro_deinit");
    RetroCore::Load(this->m_hCore, fRetroAPIVersion, "retro_api_version");
    RetroCore::Load(this->m_hCore, fGetSystemInfo, "retro_get_system_info");
    RetroCore::Load(this->m_hCore, fGetAudioVideoInfo, "retro_get_system_av_info");
    RetroCore::Load(this->m_hCore, fSetControllerPortDevice, "retro_set_controller_port_device");
    RetroCore::Load(this->m_hCore, fReset, "retro_reset");
    RetroCore::Load(this->m_hCore, fRun, "retro_run");
    RetroCore::Load(this->m_hCore, fLoadGame, "retro_load_game");
    RetroCore::Load(this->m_hCore, fUnloadGame, "retro_unload_game");
}

RetroCore::~RetroCore() {
    if (m_hCore) {
        (*(this->fUnloadGame))();
        (*(this->fDeinit))();
#ifdef WIN32
        FreeLibrary(static_cast<HMODULE>(hCore));
#else
        dlclose(m_hCore);
#endif
    }
}
