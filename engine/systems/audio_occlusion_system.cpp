// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_occlusion_system.cpp
/// @brief AudioOcclusionSystem implementation (AX1 S2 — single-ray skeleton).
#include "systems/audio_occlusion_system.h"

#include "audio/audio_source_component.h"
#include "audio/occlusion_material_map.h"
#include "core/engine.h"
#include "physics/physics_world.h"
#include "physics/rigid_body.h"
#include "scene/component.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "scene/scene_manager.h"

#include <algorithm>

namespace Vestige
{

namespace
{

/// @brief Exponential slew rate for `occlusionFraction`, in units/second, so a
///        source crossing behind a wall muffles over ~125 ms instead of
///        clicking on the frame the ray flips. Hand-tuned — no measured
///        reference exists.
/// TODO: revisit via Formula Workbench once a perceptual target curve exists.
constexpr float kOcclusionSlewPerSec = 8.0f;

} // namespace

OcclusionMeasurement measureOcclusion(const PhysicsWorld& physics,
                                      const glm::vec3& listenerPos,
                                      const glm::vec3& sourcePos,
                                      JPH::BodyID ignoreBody)
{
    OcclusionMeasurement result;

    const glm::vec3 toSource = sourcePos - listenerPos;
    const float distance     = glm::length(toSource);

    // Coincident source/listener → zero-length direction. The listener is
    // effectively at the source, so there is nothing to occlude; skip the cast
    // rather than normalise a zero vector.
    if (distance <= 0.0f)
    {
        return result;  // unoccluded
    }

    JPH::BodyID hitBody;
    float hitDistance = 0.0f;
    const bool blocked = physics.rayCast(listenerPos, toSource / distance,
                                         distance, hitBody, hitDistance,
                                         ignoreBody);
    if (blocked)
    {
        result.blocked        = true;
        result.targetFraction = 1.0f;  // Binary in S2; graded across N rays in S3.
        // Resolve the blocking body's surface material (main-thread body lock).
        result.material = occlusionPresetForSurface(
            physics.getSurfaceMaterial(hitBody));
    }
    return result;
}

float slewOcclusionFraction(float current, float target, float slewAmount)
{
    return current + (target - current) * slewAmount;
}

bool AudioOcclusionSystem::initialize(Engine& engine)
{
    m_engine = &engine;
    return true;
}

void AudioOcclusionSystem::shutdown()
{
    m_engine = nullptr;
}

void AudioOcclusionSystem::update(float deltaTime)
{
    if (m_engine == nullptr)
    {
        return;
    }

    PhysicsWorld& physics = m_engine->getPhysicsWorld();
    if (!physics.isInitialized())
    {
        return;  // No geometry to cast against — leave the fields untouched.
    }

    Scene* scene = m_engine->getSceneManager().getActiveScene();
    if (scene == nullptr)
    {
        return;
    }

    // The listener is the camera (single-listener engine). PostCamera phase
    // guarantees the camera has been stepped this frame.
    const glm::vec3 listenerPos = m_engine->getCamera().getPosition();

    // Per-frame slew fraction: how far occlusionFraction moves toward its
    // target this frame. clamp guards a long frame from overshooting.
    const float slew =
        std::clamp(deltaTime * kOcclusionSlewPerSec, 0.0f, 1.0f);

    scene->forEachEntity([&physics, &listenerPos, slew](Entity& entity)
    {
        auto* comp = entity.getComponent<AudioSourceComponent>();
        if (comp == nullptr)
        {
            return;
        }

        // S2 two-state target: a spatial source is measured each frame (single
        // centre ray); a non-spatial (2D/UI) source is never occluded, so its
        // target is 0 and the slew releases it. The audibility/distance cull
        // and round-robin budget arrive in S4.
        float target = 0.0f;

        if (comp->spatial)
        {
            // A source with its own physics body must not occlude itself; a
            // source with no body casts with an invalid ignore id ({}).
            JPH::BodyID ignoreBody;
            if (auto* body = entity.getComponent<RigidBody>();
                body != nullptr && body->hasBody())
            {
                ignoreBody = body->getBodyId();
            }

            const OcclusionMeasurement m = measureOcclusion(
                physics, listenerPos, entity.getWorldPosition(), ignoreBody);
            target = m.targetFraction;

            // Snap the material only while blocked. At fraction 0 the DSP
            // ignores it (computeObstructionGain returns openGain), so a clear
            // path simply holds the last value.
            if (m.blocked)
            {
                comp->occlusionMaterial = m.material;
            }
        }

        // Temporal smoothing: ease the stored fraction toward the target so a
        // discrete ray flip is heard as a fade, not a click. occlusionFraction
        // is the only smoothing state — the driver owns it every frame.
        comp->occlusionFraction =
            slewOcclusionFraction(comp->occlusionFraction, target, slew);
    });
}

std::vector<uint32_t> AudioOcclusionSystem::getOwnedComponentTypes() const
{
    // Auto-activate whenever the scene has audio sources to occlude. Shares
    // ownership of the type with AudioSystem — ownership only drives
    // activation, so listing it in both systems is fine.
    return { ComponentTypeId::get<AudioSourceComponent>() };
}

} // namespace Vestige
