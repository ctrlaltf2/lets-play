/**
 * @file AnalogStick.h
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
struct AnalogStick;

#pragma once

#include "Button.h"

struct AnalogStick {
    Button X, Y;
};