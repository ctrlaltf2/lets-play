/**
 * @file RetroCore.h
 *
 * @author ctrlaltf2
 *
 */
#pragma once
#include <cstdlib>
#include <functional>
#include <iostream>
#include <type_traits>

#include "boost/function.hpp"
#include "boost/dll/import.hpp"

#include "libretro.h"

/**
 * @class RetroCore
 *
 * Class that loads libretro dynamic library functions into memory.
 *
 * @todo Make this cross platform.
 */
class RetroCore {
  public:
    // Callback registrars
    boost::function<void(retro_environment_t)> SetEnvironment;
    boost::function<void(retro_video_refresh_t)> SetVideoRefresh;
    boost::function<void(retro_input_poll_t)> SetInputPoll;
    boost::function<void(retro_input_state_t)> SetInputState;
    boost::function<void(retro_audio_sample_t)> SetAudioSample;
    boost::function<void(retro_audio_sample_batch_t)> SetAudioSampleBatch;

    // libretro functions that do things
    boost::function<void()> Init;
    boost::function<void()> Deinit;
    boost::function<void()> Reset;
    boost::function<void()> Run;
    boost::function<void()> UnloadGame;
    boost::function<void(retro_system_info *)> GetSystemInfo;
    boost::function<void(retro_system_av_info *)> GetAudioVideoInfo;
    boost::function<void(unsigned, unsigned)> SetControllerPortDevice;
    boost::function<bool(const retro_game_info *)> LoadGame;
    boost::function<size_t()> SaveStateSize;
    boost::function<bool(void *, size_t)> SaveState;
    boost::function<bool(const void *, size_t)> LoadState;
    boost::function<unsigned()> RetroAPIVersion;

    RetroCore();

    /**
     * Delete the copy constructor because this can cause problems with the
     * destructor being called and closing the dynamic lib, therefore making the
     * core pointer invalid.
     */
    RetroCore(const RetroCore&) = delete;

    // TODO: On release, use constructor as intended with RAII
    /**
     * Initialize the RetroCore object.
     */
    void Load(const char *corePath);

    /**
     * Properly shuts down the retro core by calling deinit and similar.
     */
    ~RetroCore();
};
