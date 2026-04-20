// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file physics2d_system.h
/// @brief Physics2DSystem — 2D rigid-body simulation on top of the shared
/// 3D Jolt PhysicsSystem (Phase 9F-2).
///
/// Shares the Engine's single `PhysicsWorld` so broadphase, contacts, and
/// debug draw stay unified. Per-body `EAllowedDOFs::Plane2D` locks Z
/// translation and X/Y rotation, making each body behave as true 2D even
/// inside the 3D world. Colliders are thin extruded slabs so narrowphase
/// works against both 2D and 3D objects — you can drop a 2D character
/// onto a 3D slab in a mixed scene if you need to.
///
/// Events emitted on the shared EventBus:
///   - `CollisionEnter2D(entityA, entityB, contact)`
///   - `CollisionExit2D(entityA, entityB)`
///   - `TriggerEnter2D(entityA, entityB)`
///   - `TriggerExit2D(entityA, entityB)`
///
/// Phase 9E-2 script nodes (`OnCollisionEnter`, `OnTriggerEnter`) can
/// subscribe to these once the naming bridge lands.
#pragma once

#include "core/i_system.h"

#include <glm/glm.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

#include <string>
#include <unordered_map>

namespace Vestige
{

class Entity;
class PhysicsWorld;
class RigidBody2DComponent;
class Collider2DComponent;
class Scene;

class Physics2DSystem : public ISystem
{
public:
    Physics2DSystem() = default;

    // -- ISystem --
    const std::string& getSystemName() const override { return m_name; }
    bool initialize(Engine& engine) override;
    void shutdown() override;
    void update(float deltaTime) override;
    void onSceneLoad(Scene& scene) override;
    void onSceneUnload(Scene& scene) override;

    /// @brief Ensures a body exists for the entity. Idempotent — safe to
    /// call if the entity already has a live body. Returns the body id
    /// (invalid if creation failed).
    JPH::BodyID ensureBody(Entity& entity);

    /// @brief Removes the body associated with the entity. Idempotent.
    void removeBody(Entity& entity);

    /// @brief Applies an impulse in the XY plane to the entity's body.
    /// No-op if the body doesn't exist or isn't dynamic.
    void applyImpulse(Entity& entity, const glm::vec2& impulse);

    /// @brief Sets the linear velocity of a dynamic body in the XY plane.
    /// Z component is forced to 0. Angular velocity is left unchanged.
    void setLinearVelocity(Entity& entity, const glm::vec2& velocity);

    /// @brief Returns the linear velocity of a dynamic body in the XY plane.
    /// Returns {0, 0} if the body doesn't exist.
    glm::vec2 getLinearVelocity(const Entity& entity) const;

    /// @brief Teleports a body to the given position (XY) and Z-rotation.
    /// For kinematic bodies this is the intended way to move them.
    void setTransform(Entity& entity, const glm::vec2& position,
                      float rotationRadians);

    // -- Headless (test-facing) --

    /// @brief Whether the underlying PhysicsWorld is initialised. False
    /// in unit-test contexts that don't spin up the engine.
    bool isInitialized() const;

    /// @brief Accessor for the shared PhysicsWorld. May be nullptr in
    /// headless tests.
    PhysicsWorld* getPhysicsWorld() { return m_physicsWorld; }
    const PhysicsWorld* getPhysicsWorld() const { return m_physicsWorld; }

    /// @brief Test seam — point the system at a pre-initialised PhysicsWorld
    /// without going through Engine::initialize.
    void setPhysicsWorldForTesting(PhysicsWorld* world) { m_physicsWorld = world; }

    /// @brief Number of live 2D bodies currently tracked by the system.
    std::size_t liveBodyCount() const { return m_bodyByEntity.size(); }

private:
    static inline const std::string m_name = "Physics2D";
    Engine* m_engine = nullptr;
    PhysicsWorld* m_physicsWorld = nullptr;

    // Body registry keyed by entity id so the system can find the JPH
    // body without storing a raw pointer on the component (bodyId is on
    // the component for fast access from gameplay code).
    std::unordered_map<uint32_t, JPH::BodyID> m_bodyByEntity;
};

} // namespace Vestige
