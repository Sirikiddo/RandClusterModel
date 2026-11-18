#pragma once

#include <cstdint>

namespace RockAppearance {
inline std::uint32_t hash(std::uint32_t value) {
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}

inline float random(std::uint32_t cell, std::uint32_t seed, std::uint32_t channel = 0u) {
    return static_cast<float>(hash(cell ^ seed ^ (channel * 0x9e3779b9u)) & 0x00ffffffu)
        / 16777216.0f;
}
} // namespace RockAppearance
