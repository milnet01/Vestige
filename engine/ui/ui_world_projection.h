// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_world_projection.h
/// @brief Pure world-to-screen projection helper for in-world UI elements.
///
/// Extracted as a free function so the projection + frustum-cull logic is
/// testable without a GL context (`tests/test_ui_world_projection.cpp`).
#pragma once

#include <glm/glm.hpp>

namespace Vestige
{

/// @brief Result of projecting a world-space point to screen-space pixels.
struct WorldToScreenResult
{
    bool      visible;     ///< false if behind camera or outside the NDC clip box.
    glm::vec2 screenPos;   ///< Top-left-origin pixel coordinates (only valid when visible).
    float     ndcDepth;    ///< NDC z in [-1, 1] (useful for distance-based effects).
};

/// @brief Projects a world point to screen-space pixels.
///
/// Returns `visible=false` if the point is behind the camera (`clip.w <= 0`)
/// or outside the [-1, 1] NDC clip box (frustum-culled). Uses the
/// top-left-origin convention (Y flipped) to match the engine's
/// `SpriteBatchRenderer` and `TextRenderer::renderText2D` coordinate system.
///
/// @param worldPos    Point in world coordinates.
/// @param viewProj    Combined `projection * view` matrix.
/// @param screenWidth  Viewport width in pixels.
/// @param screenHeight Viewport height in pixels.
WorldToScreenResult projectWorldToScreen(const glm::vec3& worldPos,
                                          const glm::mat4& viewProj,
                                          int screenWidth, int screenHeight);

} // namespace Vestige
