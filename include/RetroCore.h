#pragma once
#include <cstdlib>
#include <functional>
#include <iostream>

#include <dlfcn.h>

#include "libretro.h"

// Loads a libretro core, acts as a storage/convenience class for the libretro
// api
class RetroCore {
    /**
     * Handle to the dynamically loaded core lib
     */
    void* m_hCore = nullptr;

    /**
     * Utility function for loading symbols
     */
    template <typename Symbol>
    static void Load(void* hCore, Symbol& sym, const char* name) {
        std::clog << "Loading '" << name << "'...\n";
        sym = (Symbol)dlsym(hCore, name);

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
    void (*fSetEnvironment)(retro_environment_t) = nullptr;
    void (*fSetVideoRefresh)(retro_video_refresh_t) = nullptr;
    void (*fSetInputPoll)(retro_input_poll_t) = nullptr;
    void (*fSetInputState)(retro_input_state_t) = nullptr;
    void (*fSetAudioSample)(retro_audio_sample_t) = nullptr;
    void (*fSetAudioSampleBatch)(retro_audio_sample_batch_t) = nullptr;

    // libretro functions that do things

    void (*fInit)() = nullptr;
    void (*fDeinit)() = nullptr;
    void (*fReset)() = nullptr;
    void (*fRun)() = nullptr;
    void (*fUnloadGame)() = nullptr;
    unsigned (*fRetroAPIVersion)() = nullptr;
    void (*fGetSystemInfo)(retro_system_info*) = nullptr;
    void (*fGetAudioVideoInfo)(retro_system_av_info*) = nullptr;
    void (*fSetControllerPortDevice)(unsigned, unsigned) = nullptr;
    bool (*fLoadGame)(const retro_game_info*) = nullptr;

    RetroCore();

    /**
     * Delete the copy constructor because this can cause problems with the
     * destructor being called and closing the dynamic lib, therefore making the
     * core pointer invalid
     */
    RetroCore(const RetroCore&) = delete;
    /**
     * Create a RetroCore object based on a core path
     * @param corePath Path to the core to load
     */
    // RetroCore(const char* corePath);

    /**
     * Create a RetroCore object from an.already existing libretro.so handle
     * @param hCore Handle to the already loaded libretro core
     */
    // RetroCore(void* hCore);

    /**
     * Initialize the RetroCore object (merges the two constructors)
     */
    void Init(const char* hCore);

    /**
     * Properly shuts down the retro core by calling deinit and similar
     */
    ~RetroCore();
};
