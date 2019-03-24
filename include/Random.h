/**
 * @file Random.h
 *
 * @author ctrlaltf2
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
