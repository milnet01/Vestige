// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_occlusion.cpp
/// @brief Material-based occlusion / obstruction gain model.
#include "audio/audio_occlusion.h"

#include <algorithm>

namespace Vestige
{

AudioOcclusionMaterial occlusionMaterialFor(AudioOcclusionMaterialPreset preset)
{
    // Relative ordering rationale (see header comment): Concrete
    // muffles the most, Cloth the least, with Wood / Glass / Stone /
    // Metal / Water interpolating. Values intentionally exaggerated
    // over laboratory measurements so differences stay audible
    // without pushing source gains into the headroom.
    switch (preset)
    {
        case AudioOcclusionMaterialPreset::Air:      return {1.00f, 0.00f};
        case AudioOcclusionMaterialPreset::Cloth:    return {0.70f, 0.30f};
        case AudioOcclusionMaterialPreset::Wood:     return {0.50f, 0.50f};
        case AudioOcclusionMaterialPreset::Glass:    return {0.40f, 0.20f};
        case AudioOcclusionMaterialPreset::Stone:    return {0.15f, 0.80f};
        case AudioOcclusionMaterialPreset::Concrete: return {0.05f, 0.90f};
        case AudioOcclusionMaterialPreset::Metal:    return {0.10f, 0.60f};
        case AudioOcclusionMaterialPreset::Water:    return {0.30f, 0.95f};
    }
    return {1.0f, 0.0f};
}

const char* occlusionMaterialLabel(AudioOcclusionMaterialPreset preset)
{
    switch (preset)
    {
        case AudioOcclusionMaterialPreset::Air:      return "Air";
        case AudioOcclusionMaterialPreset::Cloth:    return "Cloth";
        case AudioOcclusionMaterialPreset::Wood:     return "Wood";
        case AudioOcclusionMaterialPreset::Glass:    return "Glass";
        case AudioOcclusionMaterialPreset::Stone:    return "Stone";
        case AudioOcclusionMaterialPreset::Concrete: return "Concrete";
        case AudioOcclusionMaterialPreset::Metal:    return "Metal";
        case AudioOcclusionMaterialPreset::Water:    return "Water";
    }
    return "Unknown";
}

float computeObstructionGain(float openGain,
                              float transmissionCoefficient,
                              float fractionBlocked)
{
    const float f = std::max(0.0f, std::min(1.0f, fractionBlocked));
    const float t = std::max(0.0f, std::min(1.0f, transmissionCoefficient));
    // gain = openGain * (1 − f) + (openGain * t) * f
    //      = openGain * (1 − f * (1 − t))
    return openGain * (1.0f - f * (1.0f - t));
}

float computeObstructionLowPass(float lowPassAmount, float fractionBlocked)
{
    const float f = std::max(0.0f, std::min(1.0f, fractionBlocked));
    const float a = std::max(0.0f, std::min(1.0f, lowPassAmount));
    return a * f;
}

} // namespace Vestige
