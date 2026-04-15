// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file grab_system.cpp
/// @brief Object grab/carry/throw system implementation.

#include "physics/grab_system.h"
#include "physics/jolt_helpers.h"
#include "physics/rigid_body.h"
#include "scene/interactable_component.h"
#include "scene/scene.h"
#include "core/logger.h"

#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <string>

namespace Vestige
{

GrabSystem::~GrabSystem()
{
    if (m_holding && m_world)
    {
        cleanupGrab(*m_world);
    }
    m_heldEntity = nullptr;
    m_lookedAtEntity = nullptr;
}

void GrabSystem::update(PhysicsWorld& world, const Camera& camera,
                        Scene& scene, float deltaTime)
{
    m_world = &world;

    updateLookAt(world, camera, scene);

    if (m_holding)
    {
        updateHeldPosition(world, camera, deltaTime);
    }
}

bool GrabSystem::tryGrab(PhysicsWorld& world, const Camera& camera, Scene& scene)
{
    if (m_holding)
        return false;

    // Raycast from camera
    glm::vec3 origin = camera.getPosition();
    glm::vec3 direction = camera.getFront() * maxLookDistance;

    JPH::BodyID hitBody;
    float hitFraction = 0.0f;

    if (!world.rayCast(origin, direction, hitBody, hitFraction))
        return false;

    // Find the entity that owns this body
    Entity* hitEntity = nullptr;
    scene.forEachEntity([&](Entity& entity)
    {
        auto* rb = entity.getComponent<RigidBody>();
        if (rb && rb->hasBody() && rb->getBodyId() == hitBody)
        {
            hitEntity = &entity;
        }
    });

    if (!hitEntity)
        return false;

    // Check for InteractableComponent
    auto* interactable = hitEntity->getComponent<InteractableComponent>();
    if (!interactable)
        return false;

    if (interactable->type != InteractionType::GRAB)
        return false;

    // Check mass limit
    auto* rb = hitEntity->getComponent<RigidBody>();
    if (!rb || !rb->hasBody())
        return false;

    // Cache interaction properties
    m_holdDistance = interactable->holdDistance;
    m_throwForce = interactable->throwForce;

    // Create invisible kinematic holder body at the hold position
    glm::vec3 holdPos = camera.getPosition() + camera.getFront() * m_holdDistance;
    auto holderShape = JPH::Ref<JPH::Shape>(new JPH::SphereShape(0.01f));
    m_holderBodyId = world.createKinematicBody(holderShape.GetPtr(), holdPos);

    if (m_holderBodyId.IsInvalid())
    {
        Logger::error("Failed to create holder body for grab system");
        return false;
    }

    // Create distance constraint between held object and holder
    glm::vec3 grabPoint = origin + camera.getFront() * (maxLookDistance * hitFraction);
    m_holdConstraint = world.addDistanceConstraint(
        m_holderBodyId, hitBody,
        holdPos, grabPoint,
        0.0f, 0.0f,  // auto-detect distance
        interactable->holdSpringFrequency,
        interactable->holdSpringDamping);

    if (!m_holdConstraint.isValid())
    {
        world.destroyBody(m_holderBodyId);
        m_holderBodyId = JPH::BodyID();
        return false;
    }

    m_holding = true;
    m_heldEntity = hitEntity;
    m_heldBodyId = hitBody;

    Logger::info("Grabbed entity '" + hitEntity->getName() + "'");
    return true;
}

void GrabSystem::release(PhysicsWorld& world)
{
    if (!m_holding)
        return;

    cleanupGrab(world);
    Logger::info("Released held object");
}

void GrabSystem::throwObject(PhysicsWorld& world, const glm::vec3& direction,
                              float forceMultiplier)
{
    if (!m_holding)
        return;

    JPH::BodyID bodyId = m_heldBodyId;
    float force = m_throwForce * forceMultiplier;

    cleanupGrab(world);

    // Apply throw impulse
    float dirLen = glm::length(direction);
    if (dirLen < 0.0001f)
        return;
    glm::vec3 impulse = (direction / dirLen) * force;
    world.applyImpulse(bodyId, impulse);

    Logger::info("Threw object with force " + std::to_string(force));
}

void GrabSystem::updateLookAt(PhysicsWorld& world, const Camera& camera, Scene& scene)
{
    // Clear previous highlight
    if (m_lookedAtEntity)
    {
        auto* interactable = m_lookedAtEntity->getComponent<InteractableComponent>();
        if (interactable)
            interactable->highlighted = false;
        m_lookedAtEntity = nullptr;
    }

    // Don't update look-at while holding
    if (m_holding)
        return;

    glm::vec3 origin = camera.getPosition();
    glm::vec3 direction = camera.getFront() * maxLookDistance;

    JPH::BodyID hitBody;
    float hitFraction = 0.0f;

    if (!world.rayCast(origin, direction, hitBody, hitFraction))
        return;

    // Find entity
    Entity* hitEntity = nullptr;
    scene.forEachEntity([&](Entity& entity)
    {
        auto* rb = entity.getComponent<RigidBody>();
        if (rb && rb->hasBody() && rb->getBodyId() == hitBody)
        {
            hitEntity = &entity;
        }
    });

    if (!hitEntity)
        return;

    auto* interactable = hitEntity->getComponent<InteractableComponent>();
    if (!interactable)
        return;

    float distance = maxLookDistance * hitFraction;
    if (distance > interactable->grabDistance)
        return;

    m_lookedAtEntity = hitEntity;
    m_lookAtDistance = distance;
    interactable->highlighted = true;
}

void GrabSystem::updateHeldPosition(PhysicsWorld& world, const Camera& camera,
                                     float /*deltaTime*/)
{
    if (!m_holding || m_holderBodyId.IsInvalid())
        return;

    // Move the kinematic holder body to the desired hold position
    glm::vec3 holdPos = camera.getPosition() + camera.getFront() * m_holdDistance;
    glm::quat holdRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    world.setBodyTransform(m_holderBodyId, holdPos, holdRot);
}

void GrabSystem::cleanupGrab(PhysicsWorld& world)
{
    if (m_holdConstraint.isValid())
    {
        world.removeConstraint(m_holdConstraint);
        m_holdConstraint = {};
    }

    if (!m_holderBodyId.IsInvalid())
    {
        world.destroyBody(m_holderBodyId);
        m_holderBodyId = JPH::BodyID();
    }

    m_holding = false;
    m_heldEntity = nullptr;
    m_heldBodyId = JPH::BodyID();
}

} // namespace Vestige
