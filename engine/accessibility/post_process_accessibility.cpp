// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file post_process_accessibility.cpp
/// @brief `safeDefaults()` factory for the accessibility preset.

#include "accessibility/post_process_accessibility.h"

namespace Vestige
{

PostProcessAccessibilitySettings safeDefaults()
{
    PostProcessAccessibilitySettings s;
    s.depthOfFieldEnabled = false;
    s.motionBlurEnabled   = false;
    // Fog stays on under the safe preset — turning it off makes a
    // harsh far-plane horizon cutoff which is worse for users with
    // low contrast sensitivity than having fog. Density scales to
    // half so overall visibility improves without losing the look.
    s.fogEnabled          = true;
    s.fogIntensityScale   = 0.5f;
    s.reduceMotionFog     = true;
    return s;
}

} // namespace Vestige
