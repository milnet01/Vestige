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
    return s;
}

} // namespace Vestige
