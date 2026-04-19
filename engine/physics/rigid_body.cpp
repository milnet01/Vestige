// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file rigid_body.cpp
/// @brief RigidBody component implementation.
#include "physics/rigid_body.h"
#include "scene/entity.h"
#include "core/logger.h"

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>

namespace Vestige
{

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
        rot = glm::quat_cast(owner->getWorldMatrix());
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
        // Physics -> entity
        glm::vec3 pos = m_world->getBodyPosition(m_bodyId);
        glm::quat rot = m_world->getBodyRotation(m_bodyId);
        owner->transform.position = pos;
        owner->transform.rotation = glm::eulerAngles(rot);
    }
    else if (motionType == BodyMotionType::KINEMATIC)
    {
        // Entity -> physics
        glm::vec3 pos = owner->getWorldPosition();
        glm::quat rot = glm::quat(owner->transform.rotation);
        m_world->setBodyTransform(m_bodyId, pos, rot);
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
    copy->collisionVertices = collisionVertices;
    copy->collisionIndices = collisionIndices;
    copy->setEnabled(isEnabled());
    // Note: body is NOT cloned — must call createBody() on the clone
    return copy;
}

} // namespace Vestige
