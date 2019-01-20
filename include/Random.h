/**
 * @file Random.h
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
#pragma once
#include <chrono>
#include <cstdint>
#include <random>

namespace rnd {
/**
 * Return a random 32bit integer. Uses mersenne twister.
 * Thread safe.
 */
std::uint_fast32_t nextInt();
}
