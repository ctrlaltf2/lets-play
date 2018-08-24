class RetroPad;

#pragma once
#include <atomic>
#include <cstdint>

class RetroPad {
    std::atomic<std::uint16_t> m_buttonState{0};

  public:
    /*
     * Update so that a button is pressed
     * @param id the RETRO_DEVICE_ID_JOYPAD id
     */
    void buttonPress(unsigned id);

    /*
     * Update so that a button is released
     * @param id the RETRO_DEVICE_ID_JOYPAD id
     */

    void buttonRelease(unsigned id);

    /*
     * Checks if a button is pressed
     * @param the ID
     */
    bool isPressed(unsigned btn);
};
