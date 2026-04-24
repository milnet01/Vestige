// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file pressure_plate_component.cpp
/// @brief PressurePlateComponent implementation -- sphere overlap detection for triggers.
#include "scene/pressure_plate_component.h"
#include "scene/entity.h"
#include "physics/physics_world.h"
#include "core/logger.h"

#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>

namespace Vestige
{

glm::vec3 computePressurePlateCenter(const Entity& owner,
                                     float detectionHeight)
{
    // Walk the parent chain composing each level's local matrix so
    // the world position is correct even when Entity::update() has
    // not run yet this frame. Relying on `getWorldPosition()` alone
    // would tie the overlap-query centre to update-call timing —
    // brittle inside physics steps, editor preview, and unit tests.
    glm::mat4 world(1.0f);
    for (const Entity* e = &owner; e != nullptr; e = e->getParent())
    {
        world = e->transform.getLocalMatrix() * world;
    }
    const glm::vec3 worldPos(world[3]);
    return worldPos + glm::vec3(0.0f, detectionHeight, 0.0f);
}

void PressurePlateComponent::update(float deltaTime)
{
    if (!m_isEnabled)
    {
        return;
    }

    m_timeSinceLastQuery += deltaTime;
    if (m_timeSinceLastQuery < queryInterval)
    {
        return;
    }
    m_timeSinceLastQuery = 0.0f;

    if (!m_physicsWorld || !getOwner())
    {
        return;
    }

    JPH::PhysicsSystem* system = m_physicsWorld->getSystem();
    if (!system)
    {
        return;
    }

    // Phase 10.9 Slice 3 S6 — world-space centre (was local position
    // pre-S6, which put the sphere at the wrong world coordinates
    // whenever the plate entity was parented under another entity).
    const glm::vec3 center =
        computePressurePlateCenter(*getOwner(), detectionHeight);

    JPH::SphereShape queryShape(detectionRadius);
    JPH::CollideShapeSettings settings;
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;

    JPH::RMat44 queryTransform = JPH::RMat44::sTranslation(
        JPH::RVec3(center.x, center.y, center.z));

    system->GetNarrowPhaseQuery().CollideShape(
        &queryShape,
        JPH::Vec3::sReplicate(1.0f),
        queryTransform,
        settings,
        JPH::RVec3::sZero(),
        collector);

    // Collect overlapping body IDs
    std::vector<uint32_t> currentBodies;
    currentBodies.reserve(collector.mHits.size());
    for (const auto& hit : collector.mHits)
    {
        currentBodies.push_back(hit.mBodyID2.GetIndexAndSequenceNumber());
    }

    // Determine activation transitions
    bool nowEmpty = currentBodies.empty();
    bool shouldActivate = false;
    bool shouldDeactivate = false;

    if (inverted)
    {
        // Inverted: activated when nothing overlaps
        shouldActivate = !m_activated && nowEmpty;
        shouldDeactivate = m_activated && !nowEmpty;
    }
    else
    {
        // Normal: activated when at least one body overlaps
        shouldActivate = !m_activated && !nowEmpty;
        shouldDeactivate = m_activated && nowEmpty;
    }

    if (shouldActivate)
    {
        m_activated = true;
        Logger::debug("PressurePlateComponent: activated ("
                      + std::to_string(currentBodies.size()) + " bodies)");
        if (onActivate)
        {
            onActivate();
        }
    }
    else if (shouldDeactivate)
    {
        m_activated = false;
        Logger::debug("PressurePlateComponent: deactivated");
        if (onDeactivate)
        {
            onDeactivate();
        }
    }

    m_overlappingBodies = std::move(currentBodies);
}

std::unique_ptr<Component> PressurePlateComponent::clone() const
{
    auto copy = std::make_unique<PressurePlateComponent>();
    copy->detectionRadius = detectionRadius;
    copy->detectionHeight = detectionHeight;
    copy->queryInterval = queryInterval;
    copy->inverted = inverted;
    // Callbacks and runtime state are NOT copied
    return copy;
}

} // namespace Vestige
