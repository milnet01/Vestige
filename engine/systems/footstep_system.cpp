// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file footstep_system.cpp
/// @brief AX4 S6 — footstep + landing emission implementation.
#include "systems/footstep_system.h"

#include "audio/audio_engine.h"
#include "core/engine.h"
#include "core/system_registry.h"
#include "physics/physics_character_controller.h"
#include "physics/physics_world.h"
#include "physics/surface_material.h"
#include "systems/audio_system.h"
#include "systems/character_system.h"

#include <algorithm>
#include <cmath>

namespace Vestige
{

namespace
{

// --- Cadence / landing tuning ---------------------------------------------
// Non-curve constants (the speed→loudness/pitch curves live in the Formula
// Workbench, §9). TODO: revisit via Formula Workbench.
constexpr float kMinFootstepSpeedMps = 0.5f;   ///< Below this = standing; no walk steps.
constexpr float kStrideBaseMeters    = 0.85f;  ///< Walk stride at the speed floor.
constexpr float kStridePerSpeed      = 0.18f;  ///< Extra metres of stride per m/s.
constexpr float kLandingMinSpeedMps  = 1.5f;   ///< Descent below this = a soft step-down.
constexpr float kLandingEnvelopeScale = 1.3f;  ///< Landing rings longer than a step.

/// @brief Surface material under the foot, read from the controller's ground
///        contact body. Invalid / removed ground → Default (handled by
///        getSurfaceMaterial). Main thread only.
SurfaceMaterial surfaceUnderFoot(const PhysicsCharacterController& controller,
                                 const PhysicsWorld& world)
{
    return world.getSurfaceMaterial(controller.getGroundBodyId());
}

}  // namespace

float strideLengthMeters(float horizontalSpeedMps)
{
    const float s = std::max(0.0f, horizontalSpeedMps);
    return kStrideBaseMeters + kStridePerSpeed * s;
}

bool landingTriggered(bool wasOnGround, bool onGround, float descentSpeedMps)
{
    return onGround && !wasOnGround && descentSpeedMps > kLandingMinSpeedMps;
}

bool StrideAccumulator::tick(float horizontalSpeedMps, float dt)
{
    if (horizontalSpeedMps < kMinFootstepSpeedMps)
    {
        m_distance = 0.0f;   // don't bank distance while standing still
        return false;
    }
    m_distance += horizontalSpeedMps * dt;
    const float stride = strideLengthMeters(horizontalSpeedMps);
    if (m_distance >= stride)
    {
        m_distance -= stride;   // carry the remainder → steady cadence
        return true;
    }
    return false;
}

bool FootstepSystem::initialize(Engine& engine)
{
    SystemRegistry& reg = engine.getSystemRegistry();
    if (CharacterSystem* charSys = reg.getSystem<CharacterSystem>())
    {
        m_controller = &charSys->getPhysicsCharController();
    }
    if (AudioSystem* audioSys = reg.getSystem<AudioSystem>())
    {
        m_audio = &audioSys->getAudioEngine();
    }
    m_world = &engine.getPhysicsWorld();
    return true;
}

void FootstepSystem::shutdown()
{
    m_audio = nullptr;
    m_controller = nullptr;
    m_world = nullptr;
}

void FootstepSystem::emitStep(float speedMps, float envelopeScale)
{
    const SurfaceMaterial mat = surfaceUnderFoot(*m_controller, *m_world);
    m_audio->playSynth(mat, speedMps, m_controller->getPosition(), envelopeScale);
}

void FootstepSystem::update(float deltaTime)
{
    if (m_controller == nullptr || !m_controller->isInitialized())
    {
        return;
    }
    if (m_audio == nullptr || !m_audio->isAvailable())
    {
        return;
    }

    const glm::vec3 vel = m_controller->getLinearVelocity();
    const float horizSpeed = std::sqrt(vel.x * vel.x + vel.z * vel.z);
    const bool onGround = m_controller->isOnGround();

    if (landingTriggered(m_wasOnGround, onGround, m_airborneDownSpeed))
    {
        // A hard landing is one louder strike, not a walk step: energy from the
        // descent speed, a slightly longer ring (§7 step 4-5). Reset the walk
        // cadence so the first stride after landing starts fresh.
        emitStep(m_airborneDownSpeed, kLandingEnvelopeScale);
        m_stride.reset();
    }
    else if (onGround && m_stride.tick(horizSpeed, deltaTime))
    {
        emitStep(horizSpeed, 1.0f);   // footstep envelopeScale = 1.0 (§7 step 5)
    }

    // Track descent for the next landing; cleared once grounded. Read AFTER the
    // landing check so the landing frame still sees the airborne peak.
    m_airborneDownSpeed = onGround ? 0.0f : std::max(m_airborneDownSpeed, -vel.y);
    m_wasOnGround = onGround;
}

} // namespace Vestige
