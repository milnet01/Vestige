// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file camera_mode.h
/// @brief Per-entity camera-mode strategy component — base interface
///        for Phase 10.8 camera modes (first-person, third-person,
///        isometric, top-down, cinematic).
///
/// See docs/phases/phase_10_8_camera_modes_design.md §4.1–§4.2 for the
/// architecture rationale (ECS activation + CameraBlender utility;
/// rejected Cinemachine priority-queue and UE5 modifier-stack models).
///
/// The contract is a pure-function update:
///
///     CameraViewOutput = CameraMode::computeOutput(inputs, dt)
///
/// Same inputs + same game state must produce the same output — that
/// makes every mode deterministically unit-testable without a live
/// scene or renderer. Phase 11A's camera-shake offset layers on top
/// of the view output at render time; the mode itself is unaware of
/// shake (design §4.6 — the #1 camera-shake footgun from Eiserloh's
/// GDC 2016 talk is applying shake before mode output and letting it
/// accumulate into the authoritative state).
#pragma once

#include "scene/component.h"
#include "scene/camera_component.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <memory>

namespace Vestige
{

class Entity;
class InputActionMap;
class PhysicsWorld;

/// @brief Discriminates the concrete mode at runtime (inspector
/// display, serialisation, debug overlay).
enum class CameraModeType : uint8_t
{
    FirstPerson,
    ThirdPerson,
    Isometric,
    TopDown,
    Cinematic
};

/// @brief Pure-data output of a mode's per-frame computation.
///
/// Everything `CameraComponent` needs to produce a view + projection
/// matrix is carried here. Keeping this a POD (no behaviour) means
/// `CameraBlender` can interpolate between two outputs with a single
/// free function, and tests can assert equality cheaply.
struct CameraViewOutput
{
    glm::vec3      position    = glm::vec3(0.0f);
    glm::quat      orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    float          fov         = 75.0f;        ///< Vertical FOV (perspective only).
    float          orthoSize   = 10.0f;        ///< Half-height (ortho only).
    float          nearPlane   = 0.1f;
    float          farPlane    = 1000.0f;
    ProjectionType projection  = ProjectionType::PERSPECTIVE;

    bool operator==(const CameraViewOutput& o) const
    {
        return position == o.position
            && orientation == o.orientation
            && fov == o.fov
            && orthoSize == o.orthoSize
            && nearPlane == o.nearPlane
            && farPlane == o.farPlane
            && projection == o.projection;
    }

    bool operator!=(const CameraViewOutput& o) const { return !(*this == o); }
};

/// @brief Per-frame context a mode needs. Keeps `CameraMode::computeOutput`
///        a pure function of its arguments (§4.2 testability anchor).
///
/// Fields are raw pointers / references, never owning — the caller
/// (scene update tick) holds all lifetimes. A mode may legitimately
/// leave any pointer null if it doesn't need that input (first-person
/// doesn't need physics; isometric doesn't need mouseLook deltas).
struct CameraInputs
{
    /// Target entity the mode follows / anchors to. First-person
    /// typically points at the player; cinematic may leave null
    /// because its position comes from the spline.
    const Entity*         target       = nullptr;

    /// Mouse-look delta in radians this frame. Driven modes
    /// (first-person, third-person orbit) read this; scripted modes
    /// (cinematic) ignore it.
    glm::vec2             mouseLookDelta = glm::vec2(0.0f);

    /// Rebindable input action map (toggle camera, vehicle entry,
    /// etc.). Never null in production; nullable in tests.
    const InputActionMap* actions      = nullptr;

    /// Physics world — used by ThirdPersonCameraMode for the
    /// spring-arm wall-probe. Nullable for modes that don't care.
    const PhysicsWorld*   physics      = nullptr;

    /// Viewport aspect ratio (width / height). Modes don't need this
    /// for the view matrix but cinematic FOV tracks can benefit.
    float                 aspectRatio  = 16.0f / 9.0f;
};

/// @brief Base class for a camera-mode strategy.
///
/// Concrete modes (FirstPerson, ThirdPerson, Isometric, TopDown,
/// Cinematic) inherit and implement `computeOutput` + `type` +
/// `clone`. The mode is a `Component` so it participates in the
/// existing scene graph / serialisation flow; attach alongside a
/// `CameraComponent` on the same entity.
///
/// Only one `CameraMode` should be active per scene at any time —
/// enforced by `Scene::setActiveCamera` continuing to track a
/// single pointer (§4.1: ECS-activation pattern).
class CameraMode : public Component
{
public:
    ~CameraMode() override = default;

    /// @brief Produce the view output for this frame. Pure function
    /// of @p inputs + internal state; no side effects on the scene.
    virtual CameraViewOutput computeOutput(const CameraInputs& inputs,
                                           float deltaTime) = 0;

    /// @brief Type discriminant (inspector + debug overlay use this).
    virtual CameraModeType type() const = 0;

    /// @brief Component clone — every mode must implement this.
    std::unique_ptr<Component> clone() const override = 0;
};

/// @brief Linear/slerp blend between two view outputs — drives the
///        1st↔3rd transition lerp (§4.5).
///
/// @param from  Snapshot taken when the transition started.
/// @param to    Live output of the target mode this frame.
/// @param t     Progress in [0, 1]. Clamped internally.
/// @return Blended view output.
CameraViewOutput blendCameraView(const CameraViewOutput& from,
                                 const CameraViewOutput& to,
                                 float t);

} // namespace Vestige
