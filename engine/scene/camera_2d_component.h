// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file camera_2d_component.h
/// @brief Camera2DComponent — orthographic smooth-follow with deadzone
/// + world bounds clamp (Phase 9F-4).
///
/// Drives the engine's active Camera when a 2D scene is playing. The
/// component itself carries tuning + state; `updateCamera2DFollow()`
/// mutates the component's internal position from the target each
/// frame using a critically-damped spring, then clamps to world bounds.
///
/// Apply the result to a `Camera` via `Camera::setPosition()` at the
/// XY point the component tracks (Z is fixed by the authored
/// `cameraZDistance`).
#pragma once

#include "scene/component.h"

#include <glm/glm.hpp>

#include <memory>

namespace Vestige
{

class Entity;

class Camera2DComponent : public Component
{
public:
    Camera2DComponent() = default;
    ~Camera2DComponent() override = default;

    void update(float deltaTime) override;
    std::unique_ptr<Component> clone() const override;

    // --- Authored tuning ---

    /// @brief Orthographic half-height (world units). Half-width is
    /// derived from the viewport aspect ratio at render time.
    float orthoHalfHeight = 5.0f;

    /// @brief Camera position along Z — far enough that the 2D plane
    /// sits inside the frustum. Negative (looking down +Z) matches
    /// Vestige's standard camera convention.
    float cameraZDistance = -10.0f;

    /// @brief Translation offset from the target to the camera focus
    /// (designer-friendly "lead" — e.g. push up 2 units for a
    /// platformer so the player sits below centre).
    glm::vec2 followOffset = glm::vec2(0.0f, 0.0f);

    /// @brief Half-size of the deadzone rectangle, in world units. The
    /// camera does not move while the target stays inside this box.
    glm::vec2 deadzoneHalfSize = glm::vec2(0.0f, 0.0f);

    /// @brief Smoothing time (seconds). Smaller = snappier follow.
    /// Zero = instant snap (no spring).
    float smoothTimeSec = 0.2f;

    /// @brief Max speed the camera can move (world units / second).
    /// Infinity by default — set a finite value for cinematic pacing.
    float maxSpeed = 1.0e6f;

    /// @brief When true, the computed position is clamped inside
    /// `worldBounds` after smoothing so the view never shows outside
    /// the map.
    bool clampToBounds = false;

    /// @brief (minX, minY, maxX, maxY) — used when `clampToBounds` is
    /// on. The bounds are the camera *centre* extent, not the visible
    /// edge, so callers should subtract half-view when authoring.
    glm::vec4 worldBounds = glm::vec4(-1e6f, -1e6f, 1e6f, 1e6f);

    // --- Runtime ---

    /// @brief Current camera position in world XY — computed by
    /// `updateCamera2DFollow` each frame.
    glm::vec2 position = glm::vec2(0.0f);

    /// @brief Current camera velocity (used by the spring integrator).
    glm::vec2 velocity = glm::vec2(0.0f);

    /// @brief Whether this camera has ever been snapped to a target.
    /// Freshly-created cameras snap instantly to avoid a visible
    /// sweep-in from the origin on scene load.
    bool hasInitialized = false;
};

/// @brief Advances a Camera2DComponent toward @p targetWorldPos for
/// @p deltaTime seconds. Applies the deadzone, critical-damped spring,
/// max-speed cap, and world-bounds clamp in that order.
/// Writes into `camera.position` and `camera.velocity`.
///
/// Mutating the component lets systems / scripts read the result
/// without a return value; the free-function shape avoids tying this
/// to a specific Camera class.
void updateCamera2DFollow(Camera2DComponent& camera,
                          const glm::vec2& targetWorldPos,
                          float deltaTime);

} // namespace Vestige
