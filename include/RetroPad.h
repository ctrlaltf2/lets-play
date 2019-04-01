/**
 * @file RetroPad.h
 *
 * @author ctrlaltf2
 *
 */
class RetroPad;

#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "AnalogStick.h"
#include "Button.h"

#include "libretro.h"
/**
 * @class RetroPad
 *
 * A class that represents the RetroArch joypad.
 */
class RetroPad {
    /**
     * The buttons on the RetroPad controller. Ordered by their RETRO_DEVICE_JOYPAD_IDs.
     */
    std::array<Button, 16> m_buttonStates;

    /**
     * Two analog sticks. 0th element represents the left stick, the 1st the right.
     */
    std::array<AnalogStick, 2> m_stickStates;

  public:
    /**
     * Checks if a button is pressed
     * @param the RETRO_DEVICE_ID_JOYPAD id
     */
    bool isPressed(unsigned id);

    /**
     * Returns the stored analog value for a specific button
     * @param index the RETRO_DEVICE_INDEX value
     * @param id the RETRO_DEVICE_ID_ value
     */
    std::int16_t analogValue(unsigned index, unsigned id);

    /**
     * Sets the stored analog value and a specific button
     * @param index the RETRO_DEVICE_INDEX value
     * @param id the RETRO_DEVICE_ID value
     * @param value the new value to set
     */
    void updateValue(unsigned index, unsigned id, std::int16_t value);

    /**
     * Called between turns, resets all buttons to unpressed so that there's no stuck down buttons
     */
    void resetValues();
};
