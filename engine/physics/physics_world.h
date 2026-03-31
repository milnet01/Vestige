/// @file physics_world.h
/// @brief Jolt PhysicsSystem wrapper — the central physics subsystem.
#pragma once

#include "physics/physics_layers.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyID.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>

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
};

} // namespace Vestige
