// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file camera_mode.cpp
/// @brief Implementation of free-function helpers for CameraMode.
///
/// `CameraMode` itself is abstract (concrete modes live in
/// camera_modes/*.cpp starting with Slice CM2). The only code here
/// is `blendCameraView` — the transition-lerp primitive driving the
/// 1st↔3rd toggle per design §4.5.

#include "scene/camera_mode.h"

#include <algorithm>

#include <glm/gtc/quaternion.hpp>

namespace Vestige
{

CameraViewOutput blendCameraView(const CameraViewOutput& from,
                                 const CameraViewOutput& to,
                                 float t)
{
    const float clamped = std::clamp(t, 0.0f, 1.0f);

    CameraViewOutput out;
    out.position    = glm::mix(from.position, to.position, clamped);
    out.orientation = glm::slerp(from.orientation, to.orientation, clamped);
    out.fov         = glm::mix(from.fov, to.fov, clamped);
    out.orthoSize   = glm::mix(from.orthoSize, to.orthoSize, clamped);
    out.nearPlane   = glm::mix(from.nearPlane, to.nearPlane, clamped);
    out.farPlane    = glm::mix(from.farPlane, to.farPlane, clamped);

    // Projection type is discrete — perspective/ortho don't interpolate
    // meaningfully, so snap at the mid-point. 1st↔3rd toggle never
    // crosses projection types (both perspective), so this only
    // affects hypothetical isometric↔first-person transitions which
    // are not part of Phase 10.8 scope (§4.5).
    out.projection  = (clamped < 0.5f) ? from.projection : to.projection;

    return out;
}

} // namespace Vestige
