// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file water_presets.h
/// @brief The Inspector's water-surface presets, and which one a surface
///        currently matches.
#pragma once

#include "scene/water_surface.h"

#include <array>

namespace Vestige
{

/// @brief Preset names in Combo order; index 0, "Custom", sets nothing.
inline constexpr std::array<const char*, 5> kWaterPresetNames = {
    "Custom", "Still Bath", "Gentle Pool", "Flowing Stream", "Ocean Swell"};

/// @brief Writes preset @a preset's waves, flow, surface detail and colours
///        into @a config. 0 ("Custom") or an unknown index changes nothing.
void applyWaterPreset(int preset, WaterSurfaceConfig& config);

/// @brief The preset whose fields @a config holds exactly, or 0 ("Custom")
///        when none does. The Inspector shows this, so each surface shows
///        its own preset and any manual edit shows "Custom".
int matchingWaterPreset(const WaterSurfaceConfig& config);

}  // namespace Vestige
