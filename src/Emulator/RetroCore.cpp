#include "RetroCore.h"

RetroCore::RetroCore() = default;

void RetroCore::Load(const char *corePath) {
    namespace dll = boost::dll;

    std::clog << "Loading file from '" << corePath << "'\n";

    try {
        SetEnvironment = dll::import<void(retro_environment_t)>(corePath, "retro_set_environment",
                                                                dll::load_mode::rtld_now);
        SetVideoRefresh = dll::import<void(retro_video_refresh_t)>(corePath, "retro_set_video_refresh",
                                                                   dll::load_mode::rtld_now);
        SetInputPoll = dll::import<void(retro_input_poll_t)>(corePath, "retro_set_input_poll",
                                                             dll::load_mode::rtld_now);
        SetInputState = dll::import<void(retro_input_state_t)>(corePath, "retro_set_input_state",
                                                               dll::load_mode::rtld_now);
        SetAudioSample = dll::import<void(retro_audio_sample_t)>(corePath, "retro_set_audio_sample",
                                                                 dll::load_mode::rtld_now);
        SetAudioSampleBatch = dll::import<void(retro_audio_sample_batch_t)>(corePath, "retro_set_audio_sample_batch",
                                                                            dll::load_mode::rtld_now);

        Init = dll::import<void()>(corePath, "retro_init", dll::load_mode::rtld_now);
        Deinit = dll::import<void()>(corePath, "retro_deinit", dll::load_mode::rtld_now);
        Reset = dll::import<void()>(corePath, "retro_reset", dll::load_mode::rtld_now);
        Run = dll::import<void()>(corePath, "retro_run", dll::load_mode::rtld_now);
        RetroAPIVersion = dll::import<unsigned()>(corePath, "retro_api_version", dll::load_mode::rtld_now);
        GetSystemInfo = dll::import<void(retro_system_info *)>(corePath, "retro_get_system_info",
                                                               dll::load_mode::rtld_now);
        GetAudioVideoInfo = dll::import<void(retro_system_av_info *)>(corePath, "retro_get_system_av_info",
                                                                      dll::load_mode::rtld_now);
        SetControllerPortDevice = dll::import<void(unsigned, unsigned)>(corePath,
                                                                        "retro_set_controller_port_device",
                                                                        dll::load_mode::rtld_now);
        LoadGame = dll::import<bool(const retro_game_info *)>(corePath, "retro_load_game", dll::load_mode::rtld_now);
        UnloadGame = dll::import<void()>(corePath, "retro_unload_game", dll::load_mode::rtld_now);

        SaveStateSize = dll::import<size_t()>(corePath, "retro_serialize_size", dll::load_mode::rtld_now);
        SaveState = dll::import<bool(void *, size_t)>(corePath, "retro_serialize", dll::load_mode::rtld_now);
        LoadState = dll::import<bool(const void *, size_t)>(corePath, "retro_unserialize", dll::load_mode::rtld_now);
    } catch (const boost::system::system_error &e) {
        std::cerr << "failed to load a libretro function: " << e.what() << '\n';
        std::exit(-3);
    }
}

RetroCore::~RetroCore() {
    UnloadGame();
    Deinit();
}
