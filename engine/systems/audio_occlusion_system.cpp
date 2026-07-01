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

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

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

/// @brief Rays cast per spatial source. S3 default; becomes the
///        `occlusionRayCount` setting in S5. 8 rays give a smooth open-path
///        fraction near edges at 2× budget headroom (§4.5).
/// TODO: revisit via Formula Workbench once a perceptual target curve exists.
constexpr int kDefaultOcclusionRayCount = 8;

/// @brief Radius (metres) of the sphere the extra rays sample toward, so a
///        source near a doorway edge reads a partial fraction. S3 default;
///        becomes the `occlusionSourceRadius` setting in S5.
constexpr float kDefaultOcclusionSourceRadius = 0.5f;

} // namespace

const std::array<glm::vec3, kMaxOcclusionRayCount>& occlusionRayOffsets()
{
    static const std::array<glm::vec3, kMaxOcclusionRayCount> table = []
    {
        std::array<glm::vec3, kMaxOcclusionRayCount> t{};
        t[0] = glm::vec3(0.0f);  // ray 0 → exact source centre

        // Fibonacci lattice over the remaining entries: y linearly spaced away
        // from the poles, azimuth advanced by the golden angle. Each is a unit
        // vector, so scaling by the sampling radius places it on the source
        // sphere.
        constexpr int kSpherePoints = kMaxOcclusionRayCount - 1;
        const float goldenAngle =
            glm::pi<float>() * (3.0f - std::sqrt(5.0f));
        for (int i = 0; i < kSpherePoints; ++i)
        {
            const float y =
                1.0f - (2.0f * static_cast<float>(i) + 1.0f)
                           / static_cast<float>(kSpherePoints);
            const float ring   = std::sqrt(std::max(0.0f, 1.0f - y * y));
            const float theta  = goldenAngle * static_cast<float>(i);
            t[static_cast<std::size_t>(i + 1)] =
                glm::vec3(std::cos(theta) * ring, y, std::sin(theta) * ring);
        }
        return t;
    }();
    return table;
}

OcclusionMeasurement measureOcclusion(const PhysicsWorld& physics,
                                      const glm::vec3& listenerPos,
                                      const glm::vec3& sourcePos,
                                      int rayCount,
                                      float sourceRadius,
                                      JPH::BodyID ignoreBody)
{
    OcclusionMeasurement result;

    const int n = std::clamp(rayCount, 1, kMaxOcclusionRayCount);
    const std::array<glm::vec3, kMaxOcclusionRayCount>& offsets =
        occlusionRayOffsets();

    int blockedRays = 0;
    float nearestDistance = std::numeric_limits<float>::max();
    JPH::BodyID nearestBody;

    for (int i = 0; i < n; ++i)
    {
        const glm::vec3 target =
            sourcePos + offsets[static_cast<std::size_t>(i)] * sourceRadius;
        const glm::vec3 toTarget = target - listenerPos;
        const float distance     = glm::length(toTarget);

        // Coincident listener/target → zero-length direction. Skip this ray
        // (it counts toward the denominator as an un-blocked sample) rather
        // than normalise a zero vector. At rayCount==1 with a coincident
        // source this means the whole source reads unoccluded.
        if (distance <= 0.0f)
        {
            continue;
        }

        JPH::BodyID hitBody;
        float hitDistance = 0.0f;
        if (physics.rayCast(listenerPos, toTarget / distance, distance,
                            hitBody, hitDistance, ignoreBody))
        {
            ++blockedRays;
            if (hitDistance < nearestDistance)
            {
                nearestDistance = hitDistance;
                nearestBody     = hitBody;  // nearest wall along any sampled path
            }
        }
    }

    if (blockedRays > 0)
    {
        result.blocked        = true;
        result.targetFraction =
            static_cast<float>(blockedRays) / static_cast<float>(n);
        // Resolve the nearest blocking body's material (main-thread body lock).
        result.material = occlusionPresetForSurface(
            physics.getSurfaceMaterial(nearestBody));
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
                physics, listenerPos, entity.getWorldPosition(),
                kDefaultOcclusionRayCount, kDefaultOcclusionSourceRadius,
                ignoreBody);
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
