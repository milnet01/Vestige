/// @file grab_system.h
/// @brief Object grab/carry/throw system.
#pragma once

#include "physics/physics_world.h"
#include "renderer/camera.h"
#include "scene/entity.h"

#include <Jolt/Physics/Body/BodyID.h>

#include <glm/glm.hpp>

namespace Vestige
{

class Scene;

/// @brief Manages first-person object interaction: raycasting for look-at detection,
/// picking up objects with a spring constraint, carrying, and throwing.
class GrabSystem
{
public:
    GrabSystem() = default;
    ~GrabSystem();

    // Non-copyable
    GrabSystem(const GrabSystem&) = delete;
    GrabSystem& operator=(const GrabSystem&) = delete;

    /// @brief Per-frame update: moves held object, updates look-at detection.
    /// @param world The physics world.
    /// @param camera The player camera (for ray origin and direction).
    /// @param scene The current scene (for entity lookup).
    /// @param deltaTime Frame time.
    void update(PhysicsWorld& world, const Camera& camera, Scene& scene, float deltaTime);

    /// @brief Attempts to grab the currently looked-at interactable object.
    /// @return True if an object was grabbed.
    bool tryGrab(PhysicsWorld& world, const Camera& camera, Scene& scene);

    /// @brief Releases the currently held object (drops in place).
    void release(PhysicsWorld& world);

    /// @brief Throws the currently held object in the given direction.
    /// @param world The physics world.
    /// @param direction Throw direction (typically camera forward).
    /// @param forceMultiplier Additional force multiplier (1.0 = use component's throwForce).
    void throwObject(PhysicsWorld& world, const glm::vec3& direction,
                     float forceMultiplier = 1.0f);

    /// @brief Returns true if currently holding an object.
    bool isHolding() const { return m_holding; }

    /// @brief Returns the entity currently being held, or nullptr.
    Entity* getHeldEntity() const { return m_heldEntity; }

    /// @brief Returns the entity currently being looked at, or nullptr.
    Entity* getLookedAtEntity() const { return m_lookedAtEntity; }

    /// @brief Returns the distance to the looked-at entity.
    float getLookAtDistance() const { return m_lookAtDistance; }

    /// @brief Maximum ray distance for look-at detection.
    float maxLookDistance = 5.0f;

private:
    /// @brief Updates the look-at detection raycast.
    void updateLookAt(PhysicsWorld& world, const Camera& camera, Scene& scene);

    /// @brief Moves the holder body to follow the camera.
    void updateHeldPosition(PhysicsWorld& world, const Camera& camera, float deltaTime);

    /// @brief Cleans up grab state (constraint, holder body).
    void cleanupGrab(PhysicsWorld& world);

    // Held object state
    bool m_holding = false;
    Entity* m_heldEntity = nullptr;
    JPH::BodyID m_heldBodyId;
    JPH::BodyID m_holderBodyId;        ///< Invisible kinematic body at hold position
    ConstraintHandle m_holdConstraint;
    float m_holdDistance = 1.5f;        ///< Cached from InteractableComponent
    float m_throwForce = 10.0f;        ///< Cached from InteractableComponent

    // Look-at state
    Entity* m_lookedAtEntity = nullptr;
    float m_lookAtDistance = 0.0f;
};

} // namespace Vestige
