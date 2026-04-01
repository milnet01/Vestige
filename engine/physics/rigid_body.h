/// @file rigid_body.h
/// @brief RigidBody component — attaches a Jolt physics body to an entity.
#pragma once

#include "scene/component.h"
#include "physics/physics_world.h"

#include <Jolt/Physics/Body/BodyID.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

namespace Vestige
{

/// @brief Motion type for a rigid body.
enum class BodyMotionType : uint8_t
{
    STATIC,     ///< Never moves (walls, floors, pillars).
    DYNAMIC,    ///< Driven by forces and gravity.
    KINEMATIC   ///< Scripted movement that still collides (doors, platforms).
};

/// @brief Collision shape type.
enum class CollisionShapeType : uint8_t
{
    BOX,
    SPHERE,
    CAPSULE,
    CONVEX_HULL,  ///< Built from collisionVertices (Jolt builds hull automatically).
    MESH          ///< Static-only triangle mesh from collisionVertices + collisionIndices.
};

/// @brief Component that represents a physics body in the Jolt simulation.
///
/// Attach to any entity that should participate in physics. Static bodies
/// are synced once at creation. Dynamic bodies sync physics -> transform
/// each frame. Kinematic bodies sync transform -> physics each frame.
class RigidBody : public Component
{
public:
    RigidBody() = default;
    ~RigidBody() override;

    // --- Configuration (set before calling createBody) ---

    BodyMotionType motionType = BodyMotionType::STATIC;
    CollisionShapeType shapeType = CollisionShapeType::BOX;
    glm::vec3 shapeSize = glm::vec3(0.5f);  ///< Half-extents (box), radius (sphere), or (radius, halfHeight, 0) for capsule.
    float mass = 1.0f;
    float friction = 0.5f;
    float restitution = 0.3f;

    /// @brief Vertex positions for CONVEX_HULL or MESH shapes.
    /// For CONVEX_HULL, only positions are needed (Jolt builds the hull).
    /// For MESH, both vertices and indices are required.
    std::vector<glm::vec3> collisionVertices;

    /// @brief Triangle indices for MESH shapes (3 indices per triangle, CCW winding).
    std::vector<uint32_t> collisionIndices;

    /// @brief Populates collisionVertices and collisionIndices from mesh vertex data.
    /// Extracts position components from full vertex structs.
    void setCollisionMesh(const glm::vec3* positions, size_t vertexCount,
                          const uint32_t* indices = nullptr, size_t indexCount = 0);

    /// @brief Creates the Jolt body in the physics world.
    /// Must be called after the entity has a valid transform.
    void createBody(PhysicsWorld& world);

    /// @brief Removes the body from the physics world.
    void destroyBody();

    /// @brief Syncs the Jolt body state to/from the entity's transform.
    /// Dynamic: physics -> entity. Kinematic: entity -> physics.
    void syncTransform();

    /// @brief Applies a force (in Newtons) to the body center.
    void addForce(const glm::vec3& force);

    /// @brief Applies an impulse (instantaneous velocity change).
    void addImpulse(const glm::vec3& impulse);

    /// @brief Returns true if a Jolt body has been created.
    bool hasBody() const { return m_bodyId.IsInvalid() == false; }

    /// @brief Returns the Jolt body ID.
    JPH::BodyID getBodyId() const { return m_bodyId; }

    // --- Component interface ---
    void update(float deltaTime) override;
    std::unique_ptr<Component> clone() const override;

private:
    JPH::BodyID m_bodyId;
    PhysicsWorld* m_world = nullptr;
};

} // namespace Vestige
