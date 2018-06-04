#pragma once
#include <cstdint>
#include <iostream>

#include "RetroCore.h"

/*
 * 10 Reasons for creating this pseudo class
 *
 * 1. C sucks
 * 2. C sucks
 * 3. C sucks
 * 4. C sucks
 * 5. C sucks
 * 6. C sucks
 * 7. C sucks
 * 8. C sucks
 * 9. C sucks
 * 10. static class functions couldn't be registered as callbacks
 */
namespace EmulatorController {
void Run(const char* corePath, const char* romPath);
bool OnEnvironment(unsigned cmd, void* data);
void OnVideoRefresh(const void* data, unsigned width, unsigned height,
                    size_t stride);
void OnPollInput();
std::int16_t OnGetInputState(unsigned port, unsigned device, unsigned index,
                             unsigned id);
void OnLRAudioSample(std::int16_t left, std::int16_t right);
size_t OnBatchAudioSample(const std::int16_t* data, size_t frames);
}  // namespace EmulatorController
