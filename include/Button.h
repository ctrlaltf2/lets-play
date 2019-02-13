/**
 * @file Button.h
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
struct Button;

#pragma once

#include <atomic>
#include <cstdint>

struct Button {
    /**
     * The analog value for this button
     */
    std::atomic<std::int16_t> value{0};
};