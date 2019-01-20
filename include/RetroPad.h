/**
 * @file RetroPad.h
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
class RetroPad;

#pragma once
#include <atomic>
#include <cstdint>

/**
 * @class RetroPad
 *
 * A class that represents the RetroArch joypad.
 */
class RetroPad {
    std::atomic<std::uint16_t> m_buttonState{0};

  public:
    /**
     * Update that a button is pressed
     * @param id the RETRO_DEVICE_ID_JOYPAD id
     */
    void buttonPress(unsigned id);

    /**
     * Update that a button is released
     * @param id the RETRO_DEVICE_ID_JOYPAD id
     */

    void buttonRelease(unsigned id);

    /**
     * Checks if a button is pressed
     * @param the ID
     */
    bool isPressed(unsigned btn);
};
