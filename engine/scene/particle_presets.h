// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file particle_presets.h
/// @brief Factory methods for common particle effect configurations.
#pragma once

#include "scene/particle_emitter.h"

namespace Vestige
{

/// @brief Provides pre-configured ParticleEmitterConfig for common effects.
struct ParticlePresets
{
    /// @brief Upward cone fire with orange→red gradient, fast lifetime.
    static ParticleEmitterConfig torchFire();

    /// @brief Small gentle yellow flame.
    static ParticleEmitterConfig candleFlame();

    /// @brief Large fire with wide spread and embers.
    static ParticleEmitterConfig campfire();

    /// @brief Grey expanding smoke puffs with alpha blend.
    static ParticleEmitterConfig smoke();

    /// @brief Tiny slow-drifting ambient particles for indoor spaces.
    static ParticleEmitterConfig dustMotes();

    /// @brief Thin rising column that disperses with age.
    static ParticleEmitterConfig incense();

    /// @brief Bright sparks / embers that arc and fade.
    static ParticleEmitterConfig sparks();
};

} // namespace Vestige
