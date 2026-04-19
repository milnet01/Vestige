// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file deterministic_lcg_rng.h
/// @brief Tiny deterministic LCG shared across subsystems that need
/// reproducible randomness without dragging in <random>.
#pragma once

#include <cstdint>

namespace Vestige
{

/// @brief Numerical Recipes linear-congruential generator.
///
/// Used by `EnvironmentForces` and `ClothSimulator` for deterministic
/// gust/ripple/noise selection. Matches the coefficients originally
/// duplicated in both subsystems (`state * 1664525 + 1013904223`) so the
/// visual and physical behaviour remains bit-identical to the pre-extraction
/// code, while new callers get a single well-tested implementation.
class DeterministicLcgRng
{
public:
    /// @brief Construct with an explicit seed. Default 12345u matches the
    /// ClothSimulator default; EnvironmentForces seeds with 54321u via
    /// the two-argument reset().
    explicit DeterministicLcgRng(uint32_t seed = 12345u) : m_state(seed) {}

    void seed(uint32_t s) { m_state = s; }

    /// @brief Advances the state and returns a float in [0, 1).
    float nextFloat()
    {
        m_state = m_state * 1664525u + 1013904223u;
        return static_cast<float>(m_state & 0x00FFFFFFu) / 16777216.0f;
    }

    /// @brief Returns a float in [lo, hi).
    float nextRange(float lo, float hi) { return lo + nextFloat() * (hi - lo); }

    uint32_t state() const { return m_state; }

private:
    uint32_t m_state;
};

} // namespace Vestige
