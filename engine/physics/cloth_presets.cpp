// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_presets.cpp
/// @brief Cloth simulation preset parameter values.
///
/// Damping values based on research (Pikuma, CS184, NvCloth):
///   0.01 = lightweight fabric, slightly underdamped (swings back, oscillates a bit)
///   0.02 = standard cotton/linen, settles in 2-3 seconds
///   0.03 = heavy fabric, near-critically damped (minimal overshoot)
///   0.05+ = overdamped (sluggish, unrealistic for hanging fabric)
#include "physics/cloth_presets.h"

namespace Vestige
{

ClothPresetConfig ClothPresets::linenCurtain()
{
    ClothPresetConfig preset;
    preset.solver.particleMass = 0.02f;
    preset.solver.substeps = 6;                // Moderate substeps — good visual quality
    preset.solver.stretchCompliance = 0.00005f; // Very stiff stretch — holds shape
    preset.solver.shearCompliance = 0.0003f;
    preset.solver.bendCompliance = 0.005f;     // Stiff bend — actively unfolds to flat
    preset.solver.damping = 0.02f;             // Slightly underdamped — natural pendulum settling
    preset.windStrength = 4.0f;                // Moderate wind
    preset.dragCoefficient = 1.5f;
    return preset;
}

ClothPresetConfig ClothPresets::tentFabric()
{
    ClothPresetConfig preset;
    preset.solver.particleMass = 0.08f;
    preset.solver.substeps = 6;
    preset.solver.stretchCompliance = 0.00005f;
    preset.solver.shearCompliance = 0.0005f;
    preset.solver.bendCompliance = 0.05f;
    preset.solver.damping = 0.03f;             // Heavier fabric, settles faster
    preset.windStrength = 3.0f;
    preset.dragCoefficient = 1.0f;
    return preset;
}

ClothPresetConfig ClothPresets::banner()
{
    ClothPresetConfig preset;
    preset.solver.particleMass = 0.03f;
    preset.solver.substeps = 8;
    preset.solver.stretchCompliance = 0.0002f;
    preset.solver.shearCompliance = 0.0005f;
    preset.solver.bendCompliance = 0.02f;      // Moderate bend — flutters but unfolds to flat
    preset.solver.damping = 0.015f;            // Light damping — banners flutter freely
    preset.windStrength = 8.0f;
    preset.dragCoefficient = 2.0f;
    return preset;
}

ClothPresetConfig ClothPresets::heavyDrape()
{
    ClothPresetConfig preset;
    preset.solver.particleMass = 0.10f;
    preset.solver.substeps = 4;
    preset.solver.stretchCompliance = 0.00001f;
    preset.solver.shearCompliance = 0.0002f;
    preset.solver.bendCompliance = 0.02f;
    preset.solver.damping = 0.04f;             // Heavy, near-critically damped
    preset.windStrength = 2.0f;
    preset.dragCoefficient = 0.8f;
    return preset;
}

ClothPresetConfig ClothPresets::stiffFence()
{
    ClothPresetConfig preset;
    preset.solver.particleMass = 0.04f;
    preset.solver.substeps = 4;
    preset.solver.stretchCompliance = 0.00002f;
    preset.solver.shearCompliance = 0.0003f;
    preset.solver.bendCompliance = 0.03f;       // Taut — resists folding, stays flat
    preset.solver.damping = 0.025f;            // Moderate — taut fabric with slight ripple
    preset.windStrength = 5.0f;
    preset.dragCoefficient = 1.5f;
    return preset;
}

const char* ClothPresets::getPresetName(ClothPresetType type)
{
    switch (type)
    {
    case ClothPresetType::CUSTOM:         return "Custom";
    case ClothPresetType::LINEN_CURTAIN:  return "Linen Curtain";
    case ClothPresetType::TENT_FABRIC:    return "Tent Fabric";
    case ClothPresetType::BANNER:         return "Banner";
    case ClothPresetType::HEAVY_DRAPE:    return "Heavy Drape";
    case ClothPresetType::STIFF_FENCE:    return "Stiff Fence";
    default:                              return "Unknown";
    }
}

ClothPresetConfig ClothPresets::getPresetConfig(ClothPresetType type)
{
    switch (type)
    {
    case ClothPresetType::LINEN_CURTAIN:  return linenCurtain();
    case ClothPresetType::TENT_FABRIC:    return tentFabric();
    case ClothPresetType::BANNER:         return banner();
    case ClothPresetType::HEAVY_DRAPE:    return heavyDrape();
    case ClothPresetType::STIFF_FENCE:    return stiffFence();
    default:                              return linenCurtain();
    }
}

} // namespace Vestige
