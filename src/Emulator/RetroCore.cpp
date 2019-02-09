#include "RetroCore.h"

RetroCore::RetroCore() = default;

void RetroCore::Load(const char *corePath) {
    namespace dll = boost::dll;

    std::clog << "Loading file from '" << corePath << "'\n";

    try {
        SetEnvironment = dll::import<void(retro_environment_t)>(corePath, "retro_set_environment");
        SetVideoRefresh = dll::import<void(retro_video_refresh_t)>(corePath, "retro_set_video_refresh");
        SetInputPoll = dll::import<void(retro_input_poll_t)>(corePath, "retro_set_input_poll");
        SetInputState = dll::import<void(retro_input_state_t)>(corePath, "retro_set_input_state");
        SetAudioSample = dll::import<void(retro_audio_sample_t)>(corePath, "retro_set_audio_sample");
        SetAudioSampleBatch = dll::import<void(retro_audio_sample_batch_t)>(corePath, "retro_set_audio_sample_batch");

        Init = dll::import<void()>(corePath, "retro_init");
        Deinit = dll::import<void()>(corePath, "retro_deinit");
        Reset = dll::import<void()>(corePath, "retro_reset");
        Run = dll::import<void()>(corePath, "retro_run");
        RetroAPIVersion = dll::import<unsigned()>(corePath, "retro_api_version");
        GetSystemInfo = dll::import<void(retro_system_info *)>(corePath, "retro_get_system_info");
        GetAudioVideoInfo = dll::import<void(retro_system_av_info *)>(corePath, "retro_get_system_av_info");
        SetControllerPortDevice = dll::import<void(unsigned, unsigned)>(corePath,
                                                                        "retro_set_controller_port_device");
        LoadGame = dll::import<bool(const retro_game_info *)>(corePath, "retro_load_game");
        UnloadGame = dll::import<void()>(corePath, "retro_unload_game");
    } catch (const boost::system::system_error &e) {
        std::cerr << "failed to load a libretro function: " << e.what() << '\n';
        std::exit(-3);
    }
}

RetroCore::~RetroCore() {
    UnloadGame();
    Deinit();
}
