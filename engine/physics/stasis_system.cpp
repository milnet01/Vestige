// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file stasis_system.cpp
/// @brief StasisSystem implementation -- per-body freeze and slow-motion effects.
#include "physics/stasis_system.h"
#include "physics/physics_world.h"
#include "core/logger.h"

#include <Jolt/Physics/Body/BodyInterface.h>

#include <string>
#include <vector>

namespace Vestige
{

void StasisSystem::applyStasis(uint32_t bodyId, float duration, float timeScale)
{
    if (!m_physicsWorld || isInStasis(bodyId))
    {
        return;
    }

    JPH::BodyInterface& bodyInterface = m_physicsWorld->getBodyInterface();
    JPH::BodyID joltId(bodyId);

    if (!bodyInterface.IsAdded(joltId))
    {
        return;
    }

    StasisState state;
    JPH::Vec3 linVel = bodyInterface.GetLinearVelocity(joltId);
    JPH::Vec3 angVel = bodyInterface.GetAngularVelocity(joltId);
    state.linearVelocity = glm::vec3(linVel.GetX(), linVel.GetY(), linVel.GetZ());
    state.angularVelocity = glm::vec3(angVel.GetX(), angVel.GetY(), angVel.GetZ());
    state.timeScale = timeScale;
    state.duration = duration;
    state.elapsed = 0.0f;

    if (timeScale < 0.001f)
    {
        // Full freeze: zero velocities and deactivate
        bodyInterface.SetLinearVelocity(joltId, JPH::Vec3::sZero());
        bodyInterface.SetAngularVelocity(joltId, JPH::Vec3::sZero());
        bodyInterface.DeactivateBody(joltId);
    }
    else
    {
        // Slow-motion: scale velocities down
        bodyInterface.SetLinearVelocity(joltId, linVel * timeScale);
        bodyInterface.SetAngularVelocity(joltId, angVel * timeScale);
    }

    m_stasisMap[bodyId] = state;
    Logger::info("StasisSystem: body " + std::to_string(bodyId)
                 + " entered stasis (timeScale=" + std::to_string(timeScale) + ")");
}

void StasisSystem::releaseStasis(uint32_t bodyId)
{
    auto it = m_stasisMap.find(bodyId);
    if (it == m_stasisMap.end())
    {
        return;
    }

    if (m_physicsWorld)
    {
        JPH::BodyInterface& bodyInterface = m_physicsWorld->getBodyInterface();
        JPH::BodyID joltId(bodyId);

        if (bodyInterface.IsAdded(joltId))
        {
            const StasisState& state = it->second;

            // Restore original velocities
            bodyInterface.SetLinearVelocity(
                joltId,
                JPH::Vec3(state.linearVelocity.x,
                           state.linearVelocity.y,
                           state.linearVelocity.z));
            bodyInterface.SetAngularVelocity(
                joltId,
                JPH::Vec3(state.angularVelocity.x,
                           state.angularVelocity.y,
                           state.angularVelocity.z));

            // Re-activate body if it was frozen
            if (state.timeScale < 0.001f)
            {
                bodyInterface.ActivateBody(joltId);
            }
        }
    }

    m_stasisMap.erase(it);
    Logger::info("StasisSystem: body " + std::to_string(bodyId) + " released from stasis");
}

bool StasisSystem::isInStasis(uint32_t bodyId) const
{
    return m_stasisMap.count(bodyId) > 0;
}

float StasisSystem::getRemainingDuration(uint32_t bodyId) const
{
    auto it = m_stasisMap.find(bodyId);
    if (it == m_stasisMap.end())
    {
        return 0.0f;
    }

    const StasisState& state = it->second;
    if (state.duration <= 0.0f)
    {
        // Indefinite stasis
        return 0.0f;
    }

    float remaining = state.duration - state.elapsed;
    return (remaining > 0.0f) ? remaining : 0.0f;
}

void StasisSystem::update(float deltaTime)
{
    if (!m_physicsWorld || m_stasisMap.empty())
    {
        return;
    }

    JPH::BodyInterface& bodyInterface = m_physicsWorld->getBodyInterface();

    // Collect expired body IDs to avoid iterator invalidation during release
    std::vector<uint32_t> expired;

    for (auto& [bodyId, state] : m_stasisMap)
    {
        // Check for timed expiration
        if (state.duration > 0.0f)
        {
            state.elapsed += deltaTime;
            if (state.elapsed >= state.duration)
            {
                expired.push_back(bodyId);
                continue;
            }
        }

        // For slow-motion bodies, keep velocities scaled each frame
        // (gravity and other forces would otherwise accelerate them normally)
        if (state.timeScale >= 0.001f)
        {
            JPH::BodyID joltId(bodyId);
            if (bodyInterface.IsAdded(joltId))
            {
                // Re-scale to keep velocities clamped to slow-motion range
                JPH::Vec3 targetLinVel(
                    state.linearVelocity.x * state.timeScale,
                    state.linearVelocity.y * state.timeScale,
                    state.linearVelocity.z * state.timeScale);
                JPH::Vec3 targetAngVel(
                    state.angularVelocity.x * state.timeScale,
                    state.angularVelocity.y * state.timeScale,
                    state.angularVelocity.z * state.timeScale);

                bodyInterface.SetLinearVelocity(joltId, targetLinVel);
                bodyInterface.SetAngularVelocity(joltId, targetAngVel);
            }
        }
    }

    // Release expired stasis effects
    for (uint32_t bodyId : expired)
    {
        releaseStasis(bodyId);
    }
}

void StasisSystem::releaseAll()
{
    // Collect all IDs first to avoid iterator invalidation
    std::vector<uint32_t> allIds;
    allIds.reserve(m_stasisMap.size());
    for (const auto& [bodyId, state] : m_stasisMap)
    {
        allIds.push_back(bodyId);
    }

    for (uint32_t bodyId : allIds)
    {
        releaseStasis(bodyId);
    }
}

} // namespace Vestige
