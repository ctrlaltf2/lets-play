/**
 * @file Button.h
 *
 * @author ctrlaltf2
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