// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file impact_audio_system.cpp
/// @brief AX4 S7 — object-collision impact emission implementation.
#include "systems/impact_audio_system.h"

#include "audio/audio_engine.h"
#include "core/engine.h"
#include "core/event_bus.h"
#include "core/system_registry.h"
#include "physics/collision_event.h"
#include "systems/audio_system.h"

namespace Vestige
{

namespace
{

/// @brief Impacts ring longer than footsteps (design §8). Non-curve constant.
/// TODO: revisit via Formula Workbench.
constexpr float kImpactEnvelopeScale = 1.5f;

}  // namespace

int materialHardnessRank(SurfaceMaterial material)
{
    switch (material)
    {
    case SurfaceMaterial::Metal:   return 9;
    case SurfaceMaterial::Stone:   return 8;
    case SurfaceMaterial::Glass:   return 7;
    case SurfaceMaterial::Wood:    return 6;
    case SurfaceMaterial::Dirt:    return 5;
    case SurfaceMaterial::Grass:   return 4;
    case SurfaceMaterial::Sand:    return 3;
    case SurfaceMaterial::Cloth:   return 2;
    case SurfaceMaterial::Water:   return 1;
    case SurfaceMaterial::Default: return 0;
    }
    return 0;
}

SurfaceMaterial harderMaterial(SurfaceMaterial a, SurfaceMaterial b)
{
    return materialHardnessRank(a) >= materialHardnessRank(b) ? a : b;
}

ImpactDecision decideImpact(const CollisionEvent& event, bool emitUntaggedCollisions)
{
    if (!event.isEnter)
    {
        return {};   // audio cares about the onset, not the separation
    }
    if (event.approachSpeed < kMinImpactSpeed)
    {
        return {};   // too gentle to hear
    }
    const bool bothUntagged = event.matA == SurfaceMaterial::Default
                           && event.matB == SurfaceMaterial::Default;
    if (bothUntagged && !emitUntaggedCollisions)
    {
        return {};   // don't thud on every untagged box in an unauthored scene
    }
    return {true, harderMaterial(event.matA, event.matB)};
}

bool ImpactAudioSystem::initialize(Engine& engine)
{
    if (AudioSystem* audioSys = engine.getSystemRegistry().getSystem<AudioSystem>())
    {
        m_audio = &audioSys->getAudioEngine();
    }
    m_bus = &engine.getEventBus();
    m_subId = m_bus->subscribe<CollisionEvent>(
        [this](const CollisionEvent& event) { onCollision(event); });
    return true;
}

void ImpactAudioSystem::shutdown()
{
    if (m_bus != nullptr && m_subId != 0)
    {
        m_bus->unsubscribe(m_subId);
    }
    m_subId = 0;
    m_bus = nullptr;
    m_audio = nullptr;
}

void ImpactAudioSystem::onCollision(const CollisionEvent& event)
{
    if (m_audio == nullptr || !m_audio->isAvailable())
    {
        return;
    }
    const ImpactDecision decision =
        decideImpact(event, m_audio->emitUntaggedCollisions());
    if (!decision.play)
    {
        return;
    }
    // Runs inside the synchronous EventBus dispatch on the main thread (S3
    // drains + publishes after the physics step) — a plain positional one-shot.
    m_audio->playSynth(decision.material, event.approachSpeed, event.point,
                       kImpactEnvelopeScale);
}

} // namespace Vestige
