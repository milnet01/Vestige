// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file physics_world.h
/// @brief Jolt PhysicsSystem wrapper — the central physics subsystem.
#pragma once

#include "physics/physics_layers.h"
#include "physics/physics_constraint.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Body/Body.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <map>
#include <memory>
#include <vector>

namespace Vestige
{

/// @brief Configuration for PhysicsWorld initialization.
struct PhysicsWorldConfig
{
    float fixedTimestep = 1.0f / 60.0f;
    int collisionSteps = 1;
    unsigned int maxBodies = 4096;
    unsigned int numBodyMutexes = 0;       ///< 0 = auto
    unsigned int maxBodyPairs = 4096;
    unsigned int maxContactConstraints = 2048;
    int threadCount = -1;                  ///< -1 = auto (hardware_concurrency - 1)
};

/// @brief Wraps the Jolt PhysicsSystem and manages the physics simulation.
class PhysicsWorld
{
public:
    PhysicsWorld();
    ~PhysicsWorld();

    // Non-copyable
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    /// @brief Initializes the Jolt physics system.
    bool initialize(const PhysicsWorldConfig& config = {});

    /// @brief Shuts down the physics system and releases resources.
    void shutdown();

    /// @brief Steps the physics simulation with fixed-timestep accumulation.
    void update(float deltaTime);

    /// @brief Creates a static body and adds it to the world.
    /// @return The body ID, or invalid if creation failed.
    JPH::BodyID createStaticBody(const JPH::Shape* shape,
                                  const glm::vec3& position,
                                  const glm::quat& rotation = glm::quat(1, 0, 0, 0));

    /// @brief Creates a dynamic body and adds it to the world.
    JPH::BodyID createDynamicBody(const JPH::Shape* shape,
                                   const glm::vec3& position,
                                   const glm::quat& rotation = glm::quat(1, 0, 0, 0),
                                   float mass = 1.0f,
                                   float friction = 0.5f,
                                   float restitution = 0.3f);

    /// @brief Creates a kinematic body and adds it to the world.
    JPH::BodyID createKinematicBody(const JPH::Shape* shape,
                                     const glm::vec3& position,
                                     const glm::quat& rotation = glm::quat(1, 0, 0, 0));

    /// @brief Removes a body from the world and destroys it.
    void destroyBody(JPH::BodyID bodyId);

    /// @brief Gets the position of a body.
    glm::vec3 getBodyPosition(JPH::BodyID bodyId) const;

    /// @brief Gets the rotation of a body.
    glm::quat getBodyRotation(JPH::BodyID bodyId) const;

    /// @brief Sets the position and rotation of a kinematic body.
    void setBodyTransform(JPH::BodyID bodyId,
                           const glm::vec3& position,
                           const glm::quat& rotation);

    /// @brief Applies a force to a dynamic body.
    void applyForce(JPH::BodyID bodyId, const glm::vec3& force);

    /// @brief Applies an impulse to a dynamic body.
    void applyImpulse(JPH::BodyID bodyId, const glm::vec3& impulse);

    /// @brief Returns the number of active bodies.
    unsigned int getActiveBodyCount() const;

    /// @brief Returns true if the physics world has been initialized.
    bool isInitialized() const { return m_initialized; }

    /// @brief Direct access to the Jolt physics system (for advanced use).
    JPH::PhysicsSystem* getSystem() { return m_physicsSystem.get(); }
    const JPH::PhysicsSystem* getSystem() const { return m_physicsSystem.get(); }

    /// @brief Access the body interface (thread-safe, locking variant).
    JPH::BodyInterface& getBodyInterface();
    const JPH::BodyInterface& getBodyInterface() const;

    /// @brief Access the temp allocator (for CharacterVirtual, etc.).
    JPH::TempAllocator* getTempAllocator() { return m_tempAllocator.get(); }

    /// @brief Access the collision layer filters.
    const BroadPhaseLayerMapping& getBroadPhaseMapping() const { return m_broadPhaseMapping; }
    const ObjectLayerPairFilter& getObjectPairFilter() const { return m_objectPairFilter; }
    const ObjectVsBroadPhaseFilter& getObjectVsBroadPhaseFilter() const { return m_objectVsBpFilter; }

    // ----- Constraint management -----

    /// @brief Creates a hinge constraint between two bodies.
    /// Pass invalid BodyID for bodyA to attach bodyB to the world.
    ConstraintHandle addHingeConstraint(
        JPH::BodyID bodyA, JPH::BodyID bodyB,
        const glm::vec3& pivotPoint,
        const glm::vec3& hingeAxis,
        const glm::vec3& normalAxis,
        float limitsMinDeg = -180.0f,
        float limitsMaxDeg = 180.0f,
        float maxFrictionTorque = 0.0f);

    /// @brief Creates a fixed constraint welding two bodies together.
    ConstraintHandle addFixedConstraint(
        JPH::BodyID bodyA, JPH::BodyID bodyB);

    /// @brief Creates a distance constraint between attachment points.
    /// Set minDist/maxDist to -1 for auto-detection from body positions.
    ConstraintHandle addDistanceConstraint(
        JPH::BodyID bodyA, JPH::BodyID bodyB,
        const glm::vec3& pointA, const glm::vec3& pointB,
        float minDist = -1.0f, float maxDist = -1.0f,
        float springFrequency = 0.0f, float springDamping = 0.0f);

    /// @brief Creates a point (ball-and-socket) constraint.
    ConstraintHandle addPointConstraint(
        JPH::BodyID bodyA, JPH::BodyID bodyB,
        const glm::vec3& pivotPoint);

    /// @brief Creates a slider constraint along an axis.
    ConstraintHandle addSliderConstraint(
        JPH::BodyID bodyA, JPH::BodyID bodyB,
        const glm::vec3& slideAxis,
        float limitsMin = -1.0f, float limitsMax = 1.0f,
        float maxFrictionForce = 0.0f);

    /// @brief Returns a constraint by handle, or nullptr if invalid/stale.
    PhysicsConstraint* getConstraint(ConstraintHandle handle);
    const PhysicsConstraint* getConstraint(ConstraintHandle handle) const;

    /// @brief Removes a constraint by handle.
    void removeConstraint(ConstraintHandle handle);

    /// @brief Removes all constraints that reference the given body.
    void removeConstraintsForBody(JPH::BodyID bodyId);

    /// @brief Checks breakable constraints and disables those exceeding their threshold.
    /// Call after update() each frame.
    void checkBreakableConstraints(float deltaTime);

    /// @brief Returns all active constraint handles (for debug drawing, etc.).
    std::vector<ConstraintHandle> getConstraintHandles() const;

    // ----- Raycasting -----

    /// @brief Casts a ray and returns the closest hit body.
    /// @param origin Ray origin in world space.
    /// @param direction Ray direction (not normalized — length = max distance).
    /// @param outBodyId Set to the hit body ID if hit occurred.
    /// @param outFraction Set to the fraction along the ray [0,1] where the hit occurred.
    /// @return True if a body was hit.
    /// @note The newer overload that takes a `maxDistance` and writes
    ///       a world-unit `outHitDistance` is preferred for new code —
    ///       it avoids the `dir * range` then `fraction * range`
    ///       double-scaling pattern at the call site.
    bool rayCast(const glm::vec3& origin, const glm::vec3& direction,
                 JPH::BodyID& outBodyId, float& outFraction) const;

    /// @brief Casts a ray with an optional ignore-self filter.
    ///
    /// Direction is treated as a unit vector and `maxDistance` carries
    /// the range — separating them removes the
    ///     dir = unit * range; outFraction; hitPoint = origin + unit * outFraction * range
    /// pattern that the older overload encourages, where `range` has
    /// to appear at two call-sites in lockstep.
    ///
    /// @param origin Ray origin in world space.
    /// @param direction Unit-length ray direction.
    /// @param maxDistance Maximum cast distance in world units.
    /// @param outBodyId Set to the hit body ID if hit occurred.
    /// @param outHitDistance Set to the world-unit distance from origin to hit.
    /// @param ignoreBodyId Optional body to exclude (e.g. the player's
    ///        own collider for combat / grab raycasts). Pass an
    ///        invalid `JPH::BodyID` (the default) to disable.
    /// @return True if a body was hit.
    bool rayCast(const glm::vec3& origin, const glm::vec3& direction,
                 float maxDistance,
                 JPH::BodyID& outBodyId, float& outHitDistance,
                 JPH::BodyID ignoreBodyId = JPH::BodyID()) const;

    /// @brief Sweeps a sphere along a direction and returns the closest hit.
    ///
    /// Phase 10.9 Slice 7 Ph3 — pulled forward from Phase 10.8 CM3 (third-
    /// person camera boom-arm wall sweep). Mirrors the preferred `rayCast`
    /// shape: unit direction + range separated, world-unit hit distance,
    /// optional self-exclude filter for grab / boom cameras that own a
    /// collider in the swept volume.
    ///
    /// @param origin World-space sphere centre at start of sweep.
    /// @param direction Unit-length sweep direction.
    /// @param radius Sphere radius (world units, must be > 0).
    /// @param maxDistance Maximum sweep distance in world units (must be > 0).
    /// @param outBodyId Set to the hit body ID if a hit occurred.
    /// @param outHitDistance Set to world-unit distance from origin to first
    ///        contact along the sweep.
    /// @param ignoreBodyId Optional body to exclude (e.g. the camera rig's
    ///        own collider). Pass an invalid `JPH::BodyID` (the default)
    ///        to disable.
    /// @return True if the sphere hit something during the sweep.
    bool sphereCast(const glm::vec3& origin, const glm::vec3& direction,
                    float radius, float maxDistance,
                    JPH::BodyID& outBodyId, float& outHitDistance,
                    JPH::BodyID ignoreBodyId = JPH::BodyID()) const;

    /// @brief Applies an impulse at a specific world-space point on a body.
    void applyImpulseAtPoint(JPH::BodyID bodyId,
                              const glm::vec3& impulse,
                              const glm::vec3& worldPoint);

    /// @brief Returns the motion type of a body.
    JPH::EMotionType getBodyMotionType(JPH::BodyID bodyId) const;

    /// @brief Hughes-Möller orthonormalize: produce a unit vector
    /// perpendicular to `slideAxis` whose value depends only on
    /// `slideAxis` (no world-axis bias). Used for slider-constraint
    /// `normalAxis`. See Phase 10.9 Ph7 in ROADMAP.md.
    static glm::vec3 computeSliderNormalAxis(const glm::vec3& slideAxis);

private:
    std::unique_ptr<JPH::PhysicsSystem> m_physicsSystem;
    std::unique_ptr<JPH::TempAllocatorImpl> m_tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> m_jobSystem;

    BroadPhaseLayerMapping m_broadPhaseMapping;
    ObjectLayerPairFilter m_objectPairFilter;
    ObjectVsBroadPhaseFilter m_objectVsBpFilter;

    float m_fixedTimestep = 1.0f / 60.0f;
    int m_collisionSteps = 1;
    float m_accumulator = 0.0f;
    bool m_initialized = false;

    // Constraint storage. std::map gives deterministic (sorted-by-index)
    // iteration so break-order tests and Phase 11A replay are reproducible
    // — a hash-dependent order would not be. Per-slot generation lives on
    // each PhysicsConstraint::m_handle (initialised to 1 on insert), not
    // a global counter; indices are not reused today, so no global state
    // is needed to disambiguate stale handles.
    std::map<uint32_t, PhysicsConstraint> m_constraints;
    uint32_t m_nextConstraintIndex = 0;

    /// @brief Resolves bodyA for constraint creation. Invalid ID = Body::sFixedToWorld.
    /// @deprecated Phase 10.9 Ph8 — kept for callers that explicitly want a
    ///             single-body lookup; constraint creation paths should
    ///             use `withBodyPair` so the lock spans both bodies for
    ///             the duration of `settings.Create()`.
    JPH::Body* resolveBodyA(JPH::BodyID bodyA);

    /// @brief Locks @a bodyA and @a bodyB together for write, then invokes
    ///        @a fn with the resolved `Body&` references. If @a bodyA is
    ///        invalid (i.e. constraint anchored to the world), only
    ///        @a bodyB is locked and `JPH::Body::sFixedToWorld` is passed
    ///        as `bA`. Returns the result of @a fn (a `ConstraintHandle`),
    ///        or an empty handle if the lock did not succeed.
    ///
    ///        Phase 10.9 Slice 7 Ph8: replaces the prior pattern of
    ///        calling `resolveBodyA` (single-body transient lock) and then
    ///        a separate `BodyLockWrite` on @a bodyB — that pattern leaked
    ///        the `Body*` from the bodyA scope and used it after unlock,
    ///        which is UB under concurrent broadphase update.
    template <typename Fn>
    ConstraintHandle withBodyPair(JPH::BodyID bodyA, JPH::BodyID bodyB, Fn&& fn);

    /// @brief Registers a newly created constraint and returns its handle.
    ConstraintHandle registerConstraint(JPH::TwoBodyConstraint* constraint,
                                         ConstraintType type,
                                         JPH::BodyID bodyA, JPH::BodyID bodyB);
};

template <typename Fn>
inline ConstraintHandle PhysicsWorld::withBodyPair(JPH::BodyID bodyA, JPH::BodyID bodyB, Fn&& fn)
{
    if (!m_initialized || bodyB.IsInvalid())
    {
        return {};
    }

    // World-anchored constraint: only bodyB needs to be locked. bA falls
    // back to the always-valid `sFixedToWorld` singleton so the call to
    // settings.Create() inside @a fn works without dereferencing a stale
    // pointer.
    if (bodyA.IsInvalid())
    {
        const JPH::BodyID ids[1] = { bodyB };
        JPH::BodyLockMultiWrite lock(m_physicsSystem->GetBodyLockInterface(), ids, 1);
        JPH::Body* bB = lock.GetBody(0);
        if (bB == nullptr)
        {
            return {};
        }
        return fn(JPH::Body::sFixedToWorld, *bB);
    }

    // Both bodies locked together. BodyLockMultiWrite acquires the
    // mutexes for both bodies under one operation so no UB-prone window
    // exists between bodyA's lookup and bodyB's lock.
    const JPH::BodyID ids[2] = { bodyA, bodyB };
    JPH::BodyLockMultiWrite lock(m_physicsSystem->GetBodyLockInterface(), ids, 2);
    JPH::Body* bA = lock.GetBody(0);
    JPH::Body* bB = lock.GetBody(1);
    if (bA == nullptr || bB == nullptr)
    {
        return {};
    }
    return fn(*bA, *bB);
}

} // namespace Vestige
