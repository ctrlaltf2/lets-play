#include "RetroPad.h"

// Assumes id is > 15
void RetroPad::buttonPress(unsigned id) { m_buttonState |= (1u << id); }

// Assumes id is > 15
void RetroPad::buttonRelease(unsigned id) { m_buttonState &= ~(1u << id); }

bool RetroPad::isPressed(unsigned btn) { return (m_buttonState >> btn) & 1u; }
