// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file rigid_body.cpp
/// @brief RigidBody component implementation.
#define GLM_ENABLE_EXPERIMENTAL
#include "physics/rigid_body.h"
#include "scene/entity.h"
#include "core/logger.h"

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

namespace Vestige
{

namespace
{

/// The rotation in a matrix, with scale, shear and mirroring removed.
/// Gram-Schmidt keeps the X axis's direction and the X-Y plane, and Z is
/// X cross Y. So a mirrored basis (negative scale) still gives a proper
/// rotation, and a zero Z scale does not matter. A zero-length X axis, or
/// a Y axis parallel to X, has no rotation to recover: that gives identity,
/// never NaN, because Jolt asserts on a quaternion that is not unit length.
glm::quat rotationOf(const glm::mat4& matrix)
{
    constexpr float kMinAxisLength = 1e-6f;
    const glm::quat identity(1.0f, 0.0f, 0.0f, 0.0f);

    const glm::vec3 xIn(matrix[0]);
    const float xLength = glm::length(xIn);
    if (xLength < kMinAxisLength)
    {
        return identity;
    }
    const glm::vec3 x = xIn / xLength;

    const glm::vec3 yColumn(matrix[1]);
    const glm::vec3 yIn = yColumn - glm::dot(yColumn, x) * x;
    const float yLength = glm::length(yIn);
    if (yLength < kMinAxisLength)
    {
        return identity;
    }
    const glm::vec3 y = yIn / yLength;

    return glm::normalize(glm::quat_cast(glm::mat3(x, y, glm::cross(x, y))));
}

} // namespace

RigidBody::~RigidBody()
{
    destroyBody();
}

void RigidBody::createBody(PhysicsWorld& world)
{
    if (hasBody())
    {
        destroyBody();
    }

    m_world = &world;

    // Get initial transform from the owning entity
    Entity* owner = getOwner();
    glm::vec3 pos(0.0f);
    glm::quat rot(1.0f, 0.0f, 0.0f, 0.0f);
    if (owner)
    {
        pos = owner->getWorldPosition();
        rot = rotationOf(owner->getWorldMatrix());
    }

    // Create the collision shape with dimension validation per shape type
    JPH::ShapeRefC shape;
    switch (shapeType)
    {
    case CollisionShapeType::BOX:
        if (shapeSize.x < 0.05f || shapeSize.y < 0.05f || shapeSize.z < 0.05f) return;
        shape = new JPH::BoxShape(JPH::Vec3(shapeSize.x, shapeSize.y, shapeSize.z));
        break;
    case CollisionShapeType::SPHERE:
        if (shapeSize.x <= 0.0f) return;
        shape = new JPH::SphereShape(shapeSize.x);
        break;
    case CollisionShapeType::CAPSULE:
        if (shapeSize.x <= 0.0f || shapeSize.y <= 0.0f) return;  // radius, halfHeight
        shape = new JPH::CapsuleShape(shapeSize.y, shapeSize.x);
        break;
    case CollisionShapeType::CONVEX_HULL:
    {
        if (collisionVertices.size() < 4)
        {
            Logger::error("Convex hull requires at least 4 vertices, got "
                          + std::to_string(collisionVertices.size()));
            return;
        }

        // Convert glm::vec3 to Jolt Vec3
        JPH::Array<JPH::Vec3> joltVerts;
        joltVerts.reserve(collisionVertices.size());
        for (const auto& v : collisionVertices)
        {
            joltVerts.push_back(JPH::Vec3(v.x, v.y, v.z));
        }

        JPH::ConvexHullShapeSettings settings(joltVerts, JPH::cDefaultConvexRadius);
        JPH::ShapeSettings::ShapeResult result = settings.Create();
        if (result.HasError())
        {
            Logger::error("Convex hull creation failed: " + std::string(result.GetError().c_str()));
            return;
        }
        shape = result.Get();
        break;
    }
    case CollisionShapeType::MESH:
    {
        if (collisionVertices.size() < 3 || collisionIndices.size() < 3)
        {
            Logger::error("Mesh shape requires at least 1 triangle");
            return;
        }

        // Mesh shapes must be static — force it and warn
        if (motionType != BodyMotionType::STATIC)
        {
            Logger::warning("Mesh collision shapes must be static — forcing STATIC motion type");
            motionType = BodyMotionType::STATIC;
        }

        // Build indexed triangle mesh
        JPH::VertexList joltVerts;
        joltVerts.reserve(collisionVertices.size());
        for (const auto& v : collisionVertices)
        {
            joltVerts.push_back(JPH::Float3(v.x, v.y, v.z));
        }

        JPH::IndexedTriangleList joltTris;
        joltTris.reserve(collisionIndices.size() / 3);
        for (size_t i = 0; i + 2 < collisionIndices.size(); i += 3)
        {
            joltTris.push_back(JPH::IndexedTriangle(
                collisionIndices[i], collisionIndices[i + 1], collisionIndices[i + 2]));
        }

        JPH::MeshShapeSettings settings(std::move(joltVerts), std::move(joltTris));
        JPH::ShapeSettings::ShapeResult result = settings.Create();
        if (result.HasError())
        {
            Logger::error("Mesh shape creation failed: " + std::string(result.GetError().c_str()));
            return;
        }
        shape = result.Get();
        break;
    }
    default:
        return;
    }

    // Create the body based on motion type
    switch (motionType)
    {
    case BodyMotionType::STATIC:
        m_bodyId = world.createStaticBody(shape, pos, rot);
        break;
    case BodyMotionType::DYNAMIC:
        m_bodyId = world.createDynamicBody(shape, pos, rot, mass, friction, restitution);
        break;
    case BodyMotionType::KINEMATIC:
        m_bodyId = world.createKinematicBody(shape, pos, rot);
        break;
    }

    // Tag the body with its owning entity id + surface material so the
    // collision-event bus (S3) and procedural-audio emission (S6/S7) can
    // recover both from a Jolt contact without a side map. Done post-create
    // so the three create*Body signatures stay untouched. A body with no
    // owning entity (e.g. static world geometry created outside a RigidBody)
    // would get entity id 0 — here `owner` is the component's entity.
    if (!m_bodyId.IsInvalid())
    {
        const EntityId entityId = owner ? owner->getId() : 0;
        world.setBodyTags(m_bodyId, entityId, surfaceMaterial);
    }
}

void RigidBody::destroyBody()
{
    if (m_world && hasBody())
    {
        m_world->destroyBody(m_bodyId);
        m_bodyId = JPH::BodyID();
    }
    m_world = nullptr;
}

void RigidBody::syncTransform()
{
    if (!m_world || !hasBody())
    {
        return;
    }

    Entity* owner = getOwner();
    if (!owner)
    {
        return;
    }

    if (motionType == BodyMotionType::DYNAMIC)
    {
        // Physics -> entity. Phase 10.9 Ph9: write the quaternion-derived
        // local matrix directly into the Transform's matrix override
        // instead of round-tripping through Euler. Past ±90° pitch
        // `glm::eulerAngles` loses information (gimbal lock), so a
        // tumbling body's orientation drifts within a frame; the matrix
        // path bypasses that conversion entirely. The Euler `rotation`
        // field is still updated as a best-effort approximation so any
        // legacy reader (editor inspector display, scripting) sees a
        // sensible — if lossy at ±90° — value.
        const glm::vec3 pos = m_world->getBodyPosition(m_bodyId);
        const glm::quat rot = m_world->getBodyRotation(m_bodyId);

        // The body's pose is in WORLD space; the Transform is local to the
        // parent. With no parent the two are the same. With one, build the
        // world matrix the body wants, keeping the size the entity has in
        // the world now, and take it into the parent's space. A parent
        // with zero scale cannot be inverted: then the pose is written as
        // if there were no parent, as before.
        glm::mat4 local = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot)
                          * glm::scale(glm::mat4(1.0f), owner->transform.scale);
        glm::quat localRot = rot;
        const Entity* parent = owner->getParent();
        if (parent)
        {
            const glm::mat4 parentWorld = parent->getWorldMatrix();
            if (std::abs(glm::determinant(glm::mat3(parentWorld))) > 1e-12f)
            {
                const glm::mat4 world = owner->getWorldMatrix();
                const glm::vec3 worldScale =
                    glm::vec3(glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])),
                              glm::length(glm::vec3(world[2])))
                    * glm::sign(owner->transform.scale);
                local = glm::inverse(parentWorld) * glm::translate(glm::mat4(1.0f), pos)
                        * glm::mat4_cast(rot) * glm::scale(glm::mat4(1.0f), worldScale);
                localRot = rotationOf(local);
            }
        }

        owner->transform.position = glm::vec3(local[3]);
        // Transform::rotation is Euler DEGREES applied X, then Y, then Z
        // (entity.h), so extract in that order and convert. glm::eulerAngles
        // gave radians in another order, so the inspector showed nonsense.
        glm::vec3 xyz(0.0f);
        glm::extractEulerAngleXYZ(glm::mat4_cast(localRot), xyz.x, xyz.y, xyz.z);
        owner->transform.rotation = glm::degrees(xyz);
        owner->transform.setLocalMatrix(local);
    }
    else if (motionType == BodyMotionType::KINEMATIC)
    {
        // Entity -> physics
        // Position and rotation both from the world matrix, so a parented
        // entity's body turns with its parent. Transform::rotation is Euler
        // degrees applied X, Y, Z; reading it as radians turned a kinematic
        // body about 57x too little (lane-07 lead rigid_body.cpp:210).
        const glm::mat4 world = owner->getWorldMatrix();
        m_world->setBodyTransform(m_bodyId, glm::vec3(world[3]), rotationOf(world));
    }
    // Static bodies: no sync needed
}

void RigidBody::addForce(const glm::vec3& force)
{
    if (m_world && hasBody() && motionType == BodyMotionType::DYNAMIC)
    {
        m_world->applyForce(m_bodyId, force);
    }
}

void RigidBody::addImpulse(const glm::vec3& impulse)
{
    if (m_world && hasBody() && motionType == BodyMotionType::DYNAMIC)
    {
        m_world->applyImpulse(m_bodyId, impulse);
    }
}

void RigidBody::setCollisionMesh(const glm::vec3* positions, size_t vertexCount,
                                  const uint32_t* indices, size_t indexCount)
{
    collisionVertices.assign(positions, positions + vertexCount);
    if (indices && indexCount > 0)
    {
        collisionIndices.assign(indices, indices + indexCount);
    }
    else
    {
        collisionIndices.clear();
    }
}

void RigidBody::update(float /*deltaTime*/)
{
    syncTransform();
}

std::unique_ptr<Component> RigidBody::clone() const
{
    auto copy = std::make_unique<RigidBody>();
    copy->motionType = motionType;
    copy->shapeType = shapeType;
    copy->shapeSize = shapeSize;
    copy->mass = mass;
    copy->friction = friction;
    copy->restitution = restitution;
    copy->surfaceMaterial = surfaceMaterial;
    copy->collisionVertices = collisionVertices;
    copy->collisionIndices = collisionIndices;
    copy->setEnabled(isEnabled());
    // Note: body is NOT cloned — must call createBody() on the clone
    return copy;
}

} // namespace Vestige
