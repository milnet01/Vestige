// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_presets.h
/// @brief Factory methods for common cloth simulation configurations.
#pragma once

#include "physics/cloth_simulator.h"

#include <cstdint>

namespace Vestige
{

/// @brief Extended config that includes wind and drag alongside solver parameters.
struct ClothPresetConfig
{
    ClothConfig solver;            ///< Grid, mass, compliance, substeps, damping
    float windStrength = 0.0f;     ///< Default wind strength
    float dragCoefficient = 1.0f;  ///< Aerodynamic drag coefficient
};

/// @brief Cloth preset type for UI tracking.
enum class ClothPresetType : uint8_t
{
    CUSTOM = 0,
    LINEN_CURTAIN,
    TENT_FABRIC,
    BANNER,
    HEAVY_DRAPE,
    STIFF_FENCE,
    COUNT
};

/// @brief Provides pre-configured ClothPresetConfig for common fabric types.
struct ClothPresets
{
    /// @brief Light flowing fabric — entrance curtains, lightweight drapes.
    static ClothPresetConfig linenCurtain();

    /// @brief Heavy dense fabric — goat hair tent covers, stiff awnings.
    static ClothPresetConfig tentFabric();

    /// @brief Lightweight responsive fabric — flags, banners, pennants.
    static ClothPresetConfig banner();

    /// @brief Dense luxurious fabric — partition veils, thick curtains.
    static ClothPresetConfig heavyDrape();

    /// @brief Taut fabric panels — courtyard fence linen, stretched between posts.
    static ClothPresetConfig stiffFence();

    /// @brief Returns the display name for a preset type.
    static const char* getPresetName(ClothPresetType type);

    /// @brief Returns the config for a preset type (CUSTOM returns linenCurtain defaults).
    static ClothPresetConfig getPresetConfig(ClothPresetType type);
};

} // namespace Vestige
