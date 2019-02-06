/**
 * @file RetroCore.h
 *
 * @author ctrlaltf2
 *
 * @section LICENSE
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 */
#pragma once
#include <cstdlib>
#include <functional>
#include <iostream>
#include <type_traits>

#ifdef WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "libretro.h"

/**
 * @class RetroCore
 *
 * Class that loads libretro dynamic library functions into memory.
 *
 * @todo Make this cross platform.
 */
class RetroCore {
    template<class T>
        using fn = typename std::add_pointer<T>::type;
    /**
     * Handle to the dynamically loaded core lib
     */
    void *m_hCore = nullptr;

    /**
     * Utility function for loading symbols
     */
    template<typename Symbol>
    static void Load(void *hCore, Symbol& sym, const char *name) {
        std::clog << "Loading '" << name << "'...\n";
#ifdef WIN32
        sym = (Symbol) GetProcAddress(static_cast<HMODULE>(hCore), name);
#else
        sym = (Symbol) dlsym(hCore, name);
#endif

        // TODO: Exception
        if (sym == nullptr) {
            std::cerr << "Failed to load symbol '" << name << "'\n";
            std::exit(-3);
        } else {
            std::clog << "Found symbol, storing into pointer at address " << std::addressof(sym)
                << '\n';
        }
    }

  public:
    // Callback registerers
    fn<void(retro_environment_t)>           fSetEnvironment{nullptr};
    fn<void(retro_video_refresh_t)>         fSetVideoRefresh {nullptr};
    fn<void(retro_input_poll_t)>            fSetInputPoll{nullptr};
    fn<void(retro_input_state_t)>           fSetInputState{nullptr};
    fn<void(retro_audio_sample_t)>          fSetAudioSample{nullptr};
    fn<void(retro_audio_sample_batch_t)>    fSetAudioSampleBatch{nullptr};

    // libretro functions that do things
    fn<void()> fInit{nullptr};
    fn<void()> fDeinit{nullptr};
    fn<void()> fReset{nullptr};
    fn<void()> fRun{nullptr};
    fn<void()> fUnloadGame{nullptr};
    fn<void(retro_system_info *)> fGetSystemInfo{nullptr};
    fn<void(retro_system_av_info *)> fGetAudioVideoInfo{nullptr};
    fn<void(unsigned, unsigned)> fSetControllerPortDevice{nullptr};
    fn<bool(const retro_game_info*)> fLoadGame{nullptr};
    fn<unsigned()> fRetroAPIVersion{nullptr};

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
    void Init(const char *hCore);

    /**
     * Properly shuts down the retro core by calling deinit and similar.
     */
    ~RetroCore();
};
