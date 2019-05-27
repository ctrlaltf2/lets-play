#include "RetroPad.h"

bool RetroPad::isPressed(unsigned id) {
    // ???: Use more advanced ways to detect analog button presses?
    return std::abs(m_buttonStates.at(id).value) > ((2 << 14) - 1) / 2;
}

std::int16_t RetroPad::analogValue(unsigned index, unsigned id) {
    if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON)
        return m_buttonStates.at(id).value;

    if (id == RETRO_DEVICE_ID_ANALOG_X)
        return m_stickStates.at(index).X.value;
    else if (id == RETRO_DEVICE_ID_ANALOG_Y)
        return m_stickStates.at(index).Y.value;

    // return 0 if the core is misbehaving and requesting invalid values
    return 0;
}

void RetroPad::updateValue(unsigned index, unsigned id, std::int16_t value) {
    if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON) {
        m_buttonStates.at(id).value = value;
        return;
    }

    if (id == RETRO_DEVICE_ID_ANALOG_X)
        m_stickStates.at(index).X.value = value;
    else if (id == RETRO_DEVICE_ID_ANALOG_Y)
        m_stickStates.at(index).Y.value = value;
}

void RetroPad::resetValues() {
    for (auto &state : m_buttonStates) {
        state.value = 0;
    }

    for (auto &stick : m_stickStates) {
        stick.X.value = 0;
        stick.Y.value = 0;
    }
}

std::bitset<16> RetroPad::getPressedState() {
    std::bitset<16> state;
    for(int i = 0; i < state.size();++i) {
        state[i] = this->isPressed(i);
    }

    return state;
}