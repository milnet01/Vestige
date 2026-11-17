// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file water_presets.cpp
/// @brief The Inspector's water-surface presets.
#include "editor/panels/water_presets.h"

namespace Vestige
{

void applyWaterPreset(int preset, WaterSurfaceConfig& config)
{
    if (preset == 1) // Still Bath
    {
        config.numWaves = 1;
        config.waves[0] = {0.001f, 4.0f, 0.1f, 0.0f};
        config.flowSpeed = 0.05f;
        config.normalStrength = 0.3f;
        config.dudvStrength = 0.005f;
        config.specularPower = 256.0f;
        config.shallowColor = {0.15f, 0.35f, 0.45f, 0.7f};
        config.deepColor = {0.02f, 0.08f, 0.15f, 1.0f};
    }
    else if (preset == 2) // Gentle Pool
    {
        config.numWaves = 2;
        config.waves[0] = {0.005f, 3.0f, 0.2f, 10.0f};
        config.waves[1] = {0.003f, 2.0f, 0.15f, 75.0f};
        config.flowSpeed = 0.15f;
        config.normalStrength = 0.8f;
        config.dudvStrength = 0.015f;
        config.specularPower = 256.0f;
        config.shallowColor = {0.1f, 0.4f, 0.5f, 0.8f};
        config.deepColor = {0.02f, 0.1f, 0.25f, 1.0f};
    }
    else if (preset == 3) // Flowing Stream
    {
        config.numWaves = 3;
        config.waves[0] = {0.015f, 1.5f, 0.8f, 0.0f};
        config.waves[1] = {0.008f, 1.0f, 0.6f, 15.0f};
        config.waves[2] = {0.004f, 0.8f, 1.0f, -10.0f};
        config.flowSpeed = 0.6f;
        config.normalStrength = 1.5f;
        config.dudvStrength = 0.03f;
        config.specularPower = 128.0f;
        config.shallowColor = {0.12f, 0.38f, 0.42f, 0.75f};
        config.deepColor = {0.03f, 0.12f, 0.2f, 1.0f};
    }
    else if (preset == 4) // Ocean Swell
    {
        config.numWaves = 4;
        config.waves[0] = {0.04f, 8.0f, 0.3f, 0.0f};
        config.waves[1] = {0.025f, 5.0f, 0.25f, 30.0f};
        config.waves[2] = {0.015f, 3.0f, 0.4f, -20.0f};
        config.waves[3] = {0.008f, 1.5f, 0.6f, 60.0f};
        config.flowSpeed = 0.3f;
        config.normalStrength = 2.0f;
        config.dudvStrength = 0.04f;
        config.specularPower = 64.0f;
        config.shallowColor = {0.05f, 0.3f, 0.4f, 0.85f};
        config.deepColor = {0.01f, 0.05f, 0.15f, 1.0f};
    }
}

namespace
{

bool sameWave(const WaterSurfaceConfig::Wave& a, const WaterSurfaceConfig::Wave& b)
{
    return a.amplitude == b.amplitude && a.wavelength == b.wavelength
        && a.speed == b.speed && a.direction == b.direction;
}

/// True when @a a and @a b agree on every field applyWaterPreset writes.
/// Exact float equality is right: a preset copies its literals unchanged.
bool samePresetFields(const WaterSurfaceConfig& a, const WaterSurfaceConfig& b)
{
    if (a.numWaves != b.numWaves || a.flowSpeed != b.flowSpeed
        || a.normalStrength != b.normalStrength || a.dudvStrength != b.dudvStrength
        || a.specularPower != b.specularPower || a.shallowColor != b.shallowColor
        || a.deepColor != b.deepColor)
    {
        return false;
    }
    for (int i = 0; i < a.numWaves && i < WaterSurfaceConfig::MAX_WAVES; ++i)
    {
        if (!sameWave(a.waves[i], b.waves[i]))
        {
            return false;
        }
    }
    return true;
}

}  // namespace

int matchingWaterPreset(const WaterSurfaceConfig& config)
{
    for (int preset = 1; preset < static_cast<int>(kWaterPresetNames.size()); ++preset)
    {
        WaterSurfaceConfig probe = config;
        applyWaterPreset(preset, probe);
        if (samePresetFields(probe, config))
        {
            return preset;
        }
    }
    return 0;
}

}  // namespace Vestige
