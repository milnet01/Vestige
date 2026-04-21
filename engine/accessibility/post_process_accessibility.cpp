// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file post_process_accessibility.cpp
/// @brief `safeDefaults()` factory for the accessibility preset.

#include "accessibility/post_process_accessibility.h"

namespace Vestige
{

namespace
{
/// Fog-density multiplier applied by the accessibility "safe" preset.
/// Half of the authored density: loud enough to preserve the horizon
/// cue (avoiding the harsh far-plane cutoff that hurts low-contrast-
/// sensitivity users), quiet enough to keep distant detail readable.
constexpr float SAFE_PRESET_FOG_INTENSITY = 0.5f;
}

PostProcessAccessibilitySettings safeDefaults()
{
    PostProcessAccessibilitySettings s;
    s.depthOfFieldEnabled = false;
    s.motionBlurEnabled   = false;
    // Fog stays on under the safe preset — turning it off makes a
    // harsh far-plane horizon cutoff which is worse for users with
    // low contrast sensitivity than having fog. Density scales to
    // SAFE_PRESET_FOG_INTENSITY so overall visibility improves
    // without losing the look.
    s.fogEnabled          = true;
    s.fogIntensityScale   = SAFE_PRESET_FOG_INTENSITY;
    s.reduceMotionFog     = true;
    return s;
}

} // namespace Vestige
