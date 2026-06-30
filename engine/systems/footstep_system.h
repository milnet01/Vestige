// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file footstep_system.h
/// @brief AX4 S6 — procedural footstep + landing audio for the character
///        controller. The surface under the foot comes from the controller's
///        existing ground-contact body (no raycast); the sound is synthesised
///        on the fly via AudioEngine::playSynth (the S5 play glue).
#pragma once

#include "core/i_system.h"

#include <string>

namespace Vestige
{

class AudioEngine;
class PhysicsCharacterController;
class PhysicsWorld;

// ---------------------------------------------------------------------------
// Pure cadence logic — no engine deps, unit-tested in isolation
// ---------------------------------------------------------------------------

/// @brief Stride length in metres for a given horizontal speed. Strides
///        lengthen with speed (running), so cadence (= speed / stride)
///        rises but saturates rather than growing without bound. AX4 S6.
/// TODO: revisit via Formula Workbench (non-curve tuning constants).
float strideLengthMeters(float horizontalSpeedMps);

/// @brief True when the foot has just landed hard enough for a distinct
///        landing strike: airborne→grounded with descent speed above the
///        floor. A gentle step-down stays a normal step. AX4 S6.
/// TODO: revisit via Formula Workbench (landing-speed floor).
bool landingTriggered(bool wasOnGround, bool onGround, float descentSpeedMps);

/// @brief Distance-based stride accumulator. Pure: fed horizontal speed + dt,
///        returns true on the frame a footstep should fire. Distance-based so
///        cadence tracks speed naturally — walk vs run falls out, no separate
///        timers. AX4 S6.
struct StrideAccumulator
{
    /// @brief Advance one frame. Returns true exactly when a step fires; the
    ///        crossing remainder carries forward so cadence stays steady frame
    ///        to frame. Below the walk-speed floor the accumulator is bled to
    ///        zero so the next step after stopping doesn't fire instantly.
    bool tick(float horizontalSpeedMps, float dt);

    void reset() { m_distance = 0.0f; }

    float m_distance = 0.0f;   ///< Horizontal distance banked since last step (m).
};

// ---------------------------------------------------------------------------
// The system
// ---------------------------------------------------------------------------

/// @brief Emits procedural footstep + landing sounds for the physics character
///        controller. Each frame it reads the controller's ground state,
///        horizontal speed and ground body, looks up the surface material under
///        the foot, and plays a synthesised strike via AudioEngine::playSynth.
///
/// No physics-core change: the surface is read from the controller's existing
/// ground contact (`CharacterVirtual::GetGroundBodyID`), not a raycast.
class FootstepSystem : public ISystem
{
public:
    FootstepSystem() = default;

    // -- ISystem interface --
    const std::string& getSystemName() const override { return m_name; }
    bool initialize(Engine& engine) override;
    void shutdown() override;
    void update(float deltaTime) override;

    /// @brief Global, controller-driven — keep ticking even with no owned
    ///        components in the scene. update() guards internally on controller
    ///        + audio availability, so this is cheap when there is no player.
    ///        Mirrors AudioSystem's force-active policy.
    bool isForceActive() const override { return true; }

private:
    /// @brief Synthesise + play one strike at the foot, with the surface under
    ///        the foot as the timbre and `speedMps` as the impact energy.
    void emitStep(float speedMps, float envelopeScale);

    static inline const std::string m_name = "Footstep";

    AudioEngine* m_audio = nullptr;
    PhysicsCharacterController* m_controller = nullptr;
    PhysicsWorld* m_world = nullptr;

    StrideAccumulator m_stride;
    bool m_wasOnGround = true;          ///< Ground state last frame (landing edge).
    float m_airborneDownSpeed = 0.0f;   ///< Peak descent speed while airborne (m/s).
};

} // namespace Vestige
