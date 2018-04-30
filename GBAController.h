#pragma once
#include <iostream>

#include "RetroCore.h"
#include "libretro.h"

class GBAController {
    /**
     * Handle to libretro core library
     */
    void* m_hCore = nullptr;

    RetroCore m_Core;

    enum class Buttons {
        A = 1,
        B = 2,
        L = 4,
        R = 8,
        Up = 16,
        Down = 32,
        Left = 64,
        Right = 128,
        Start = 256,
        Select = 512
    };

    /**
     * Mask for which buttons are pressed based on the above values
     */
    std::uint16_t buttonMask{0};

   public:
    GBAController() = delete;
    GBAController(const char* corePath, const char* romPath);

    /*** Callbacks ***/

    /**
     * Called when the core requests extra information about the environment*/
    bool OnEnvironment(unsigned cmd, char* data);

    /**
     * Called when the video refreshes
     */
    void OnScreenUpdate(const void* data, unsigned width, unsigned height,
                        size_t pitch);

    /**
     * Called when the core requests that the state of the input is polled and
     * updated
     */
    void OnInputPoll();

    /**
     * Called when the core requests the state of the input as it stands,
     * no polling
     */
    std::int16_t OnGetInputState(unsigned port, unsigned device, unsigned index,
                                 unsigned id);

    /**
     * Called when an audio packet is sent in either channel, l or r (not
     * commonly used)
     */
    void OnAudioPacket(std::int16_t left, std::int16_t right);

    /**
     * Called when a set of audio packets are produced
     */
    size_t OnBatchAudioPacket(const std::int16_t* data, size_t frames);
};
