// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file physics2d_system.cpp
/// @brief Physics2DSystem implementation.
#include "systems/physics2d_system.h"
#include "core/engine.h"
#include "core/logger.h"
#include "physics/physics_world.h"
#include "scene/collider_2d_component.h"
#include "scene/entity.h"
#include "scene/rigid_body_2d_component.h"
#include "scene/scene.h"
#include "scene/scene_manager.h"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/AllowedDOFs.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include <algorithm>

namespace Vestige
{

namespace
{

/// @brief Builds a refcounted Jolt shape matching a 2D collider.
/// Returns an empty Ref on malformed input. Z-extrusion makes the shape
/// thin but finite so it interacts with the shared 3D broadphase.
JPH::ShapeRefC makeShape(const Collider2DComponent& collider)
{
    // Jolt's default convex radius is 0.05 m. Box / cylinder shapes
    // require every half-extent to be ≥ that radius or Create() rejects
    // the settings, so clamp the extruded Z slab thickness above it.
    // Authored zThickness smaller than this is silently widened so
    // designers don't have to think about Jolt's internal margin.
    constexpr float kMinHalfZ = 0.06f;
    const float halfZ = std::max(kMinHalfZ, collider.zThickness * 0.5f);
    switch (collider.shape)
    {
        case ColliderShape2D::Box:
        {
            JPH::Vec3 extents(std::max(0.01f, collider.halfExtents.x),
                              std::max(0.01f, collider.halfExtents.y),
                              halfZ);
            JPH::BoxShapeSettings settings(extents);
            auto result = settings.Create();
            if (result.HasError()) { return {}; }
            return result.Get();
        }
        case ColliderShape2D::Circle:
        {
            // Cylinder extruded along Z is a 2D disc when the sim is
            // locked to XY. A SphereShape would work too but a cylinder
            // is closer to the actual 2D semantics (hard edges on the
            // Z faces instead of a rounded sphere surface).
            JPH::CylinderShapeSettings settings(halfZ,
                                                std::max(0.01f, collider.radius));
            // Rotate the cylinder so its axis lies along Z (default Jolt
            // cylinder is along Y).
            auto result = settings.Create();
            if (result.HasError()) { return {}; }
            return result.Get();
        }
        case ColliderShape2D::Capsule:
        {
            const float halfH = std::max(collider.radius,
                                         collider.height * 0.5f);
            JPH::CapsuleShapeSettings settings(
                std::max(0.01f, halfH - collider.radius),
                std::max(0.01f, collider.radius));
            auto result = settings.Create();
            if (result.HasError()) { return {}; }
            return result.Get();
        }
        case ColliderShape2D::Polygon:
        {
            if (collider.vertices.size() < 3)
            {
                return {};
            }
            JPH::Array<JPH::Vec3> points;
            points.reserve(collider.vertices.size() * 2);
            for (const auto& v : collider.vertices)
            {
                points.emplace_back(v.x, v.y, -halfZ);
                points.emplace_back(v.x, v.y,  halfZ);
            }
            JPH::ConvexHullShapeSettings settings(points);
            auto result = settings.Create();
            if (result.HasError()) { return {}; }
            return result.Get();
        }
        case ColliderShape2D::EdgeChain:
        {
            // Static-only. Encoded as a mesh of triangles: each segment
            // becomes two triangles (front + back of the extruded slab)
            // so contacts resolve on either side.
            if (collider.vertices.size() < 2)
            {
                return {};
            }
            JPH::TriangleList triangles;
            triangles.reserve((collider.vertices.size() - 1) * 2);
            for (std::size_t i = 0; i + 1 < collider.vertices.size(); ++i)
            {
                const auto& a = collider.vertices[i];
                const auto& b = collider.vertices[i + 1];
                JPH::Float3 fa(a.x, a.y, -halfZ);
                JPH::Float3 fb(b.x, b.y, -halfZ);
                JPH::Float3 fc(b.x, b.y,  halfZ);
                JPH::Float3 fd(a.x, a.y,  halfZ);
                triangles.emplace_back(fa, fb, fc);
                triangles.emplace_back(fa, fc, fd);
            }
            JPH::MeshShapeSettings settings(triangles);
            auto result = settings.Create();
            if (result.HasError()) { return {}; }
            return result.Get();
        }
    }
    return {};
}

JPH::EAllowedDOFs resolveDofs(const RigidBody2DComponent& rb)
{
    JPH::EAllowedDOFs dofs = JPH::EAllowedDOFs::Plane2D;
    if (rb.fixedRotation)
    {
        // Plane2D allows RotationZ — strip it for fixed-rotation bodies
        // like the platformer character.
        dofs = dofs & ~JPH::EAllowedDOFs::RotationZ;
    }
    return dofs;
}

} // namespace

bool Physics2DSystem::initialize(Engine& engine)
{
    m_engine = &engine;
    m_physicsWorld = &engine.getPhysicsWorld();
    Logger::info("[Physics2DSystem] Initialized (shared PhysicsWorld, Plane2D DOF)");
    return true;
}

void Physics2DSystem::shutdown()
{
    // Remove any lingering bodies — the shared PhysicsWorld might outlive
    // us if another subsystem holds a reference, and orphaned bodies
    // would silently leak.
    if (m_physicsWorld && m_physicsWorld->isInitialized())
    {
        for (auto& [_, id] : m_bodyByEntity)
        {
            if (!id.IsInvalid())
            {
                m_physicsWorld->destroyBody(id);
            }
        }
    }
    m_bodyByEntity.clear();
    m_physicsWorld = nullptr;
    m_engine = nullptr;
}

void Physics2DSystem::update(float /*deltaTime*/)
{
    if (!m_physicsWorld || !m_physicsWorld->isInitialized())
    {
        return;
    }

    // Phase 10.9 Slice 13 Pe2 — write cached velocities back onto components
    // so gameplay code / script nodes can read them without touching Jolt
    // directly. Walking `m_bodyByEntity` directly skips the O(N) entity-tree
    // visit per frame: only entities that own a tracked body are queried, and
    // every one of them already has a known body id, so the prior
    // `forEachEntity` + `getComponent + IsInvalid` filter chain was redundant
    // work proportional to scene size rather than 2D-physics-body count.
    auto& bodyInterface = m_physicsWorld->getBodyInterface();
    if (!m_engine)
    {
        return;
    }
    Scene* scene = m_engine->getSceneManager().getActiveScene();
    if (!scene)
    {
        return;
    }
    for (const auto& [entityId, bodyId] : m_bodyByEntity)
    {
        if (bodyId.IsInvalid())
        {
            continue;
        }
        Entity* entity = scene->findEntityById(entityId);
        if (!entity)
        {
            continue;
        }
        auto* rb = entity->getComponent<RigidBody2DComponent>();
        if (!rb)
        {
            continue;
        }
        const auto vLinear  = bodyInterface.GetLinearVelocity(bodyId);
        const auto vAngular = bodyInterface.GetAngularVelocity(bodyId);
        rb->linearVelocity  = glm::vec2(vLinear.GetX(), vLinear.GetY());
        rb->angularVelocity = vAngular.GetZ();
    }
}

void Physics2DSystem::onSceneLoad(Scene& scene)
{
    if (!m_physicsWorld || !m_physicsWorld->isInitialized())
    {
        return;
    }
    scene.forEachEntity([this](Entity& e) {
        if (e.getComponent<RigidBody2DComponent>() &&
            e.getComponent<Collider2DComponent>())
        {
            ensureBody(e);
        }
    });
}

void Physics2DSystem::onSceneUnload(Scene& scene)
{
    scene.forEachEntity([this](Entity& e) { removeBody(e); });
}

JPH::BodyID Physics2DSystem::ensureBody(Entity& entity)
{
    if (!m_physicsWorld || !m_physicsWorld->isInitialized())
    {
        return {};
    }
    const auto existing = m_bodyByEntity.find(entity.getId());
    if (existing != m_bodyByEntity.end() && !existing->second.IsInvalid())
    {
        return existing->second;
    }

    auto* rb = entity.getComponent<RigidBody2DComponent>();
    auto* cc = entity.getComponent<Collider2DComponent>();
    if (!rb || !cc)
    {
        return {};
    }

    JPH::ShapeRefC shape = makeShape(*cc);
    if (shape == nullptr)
    {
        Logger::warning("[Physics2DSystem] Collider on entity '" +
                        entity.getName() +
                        "' produced no valid shape; body skipped.");
        return {};
    }

    const glm::vec3 pos = entity.getWorldPosition();
    JPH::Vec3 position(pos.x, pos.y, pos.z + cc->zOffset);

    // Z rotation only — 2D.
    const float zRadians = 0.0f;  // TODO: extract Z rotation from transform when needed.
    JPH::Quat rotation = JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), zRadians);

    // BodyCreationSettings with 2D DOF lock. We bypass PhysicsWorld's
    // convenience wrappers because they don't expose mAllowedDOFs.
    auto& bodyInterface = m_physicsWorld->getBodyInterface();

    JPH::EMotionType motion = JPH::EMotionType::Dynamic;
    JPH::ObjectLayer layer = 1;  // DYNAMIC
    switch (rb->type)
    {
        case BodyType2D::Static:    motion = JPH::EMotionType::Static;    layer = 0; break;
        case BodyType2D::Kinematic: motion = JPH::EMotionType::Kinematic; layer = 1; break;
        case BodyType2D::Dynamic:   motion = JPH::EMotionType::Dynamic;   layer = 1; break;
    }

    JPH::BodyCreationSettings settings(shape, position, rotation, motion, layer);
    settings.mAllowedDOFs = resolveDofs(*rb);
    settings.mFriction = rb->friction;
    settings.mRestitution = rb->restitution;
    settings.mLinearDamping = rb->linearDamping;
    settings.mAngularDamping = rb->angularDamping;
    settings.mGravityFactor = rb->gravityScale;
    settings.mIsSensor = cc->isSensor;
    if (rb->type == BodyType2D::Dynamic)
    {
        settings.mOverrideMassProperties =
            JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = std::max(0.001f, rb->mass);
    }

    const JPH::EActivation activation =
        (rb->type == BodyType2D::Static) ? JPH::EActivation::DontActivate
                                         : JPH::EActivation::Activate;
    const JPH::BodyID id = bodyInterface.CreateAndAddBody(settings, activation);
    if (id.IsInvalid())
    {
        Logger::warning("[Physics2DSystem] CreateAndAddBody failed for '" +
                        entity.getName() + "'");
        return {};
    }

    rb->bodyId = id;
    m_bodyByEntity[entity.getId()] = id;
    return id;
}

void Physics2DSystem::removeBody(Entity& entity)
{
    const auto it = m_bodyByEntity.find(entity.getId());
    if (it == m_bodyByEntity.end())
    {
        return;
    }
    if (m_physicsWorld && m_physicsWorld->isInitialized() &&
        !it->second.IsInvalid())
    {
        m_physicsWorld->destroyBody(it->second);
    }
    if (auto* rb = entity.getComponent<RigidBody2DComponent>())
    {
        rb->bodyId = {};
    }
    m_bodyByEntity.erase(it);
}

void Physics2DSystem::applyImpulse(Entity& entity, const glm::vec2& impulse)
{
    if (!m_physicsWorld || !m_physicsWorld->isInitialized())
    {
        return;
    }
    auto* rb = entity.getComponent<RigidBody2DComponent>();
    if (!rb || rb->bodyId.IsInvalid())
    {
        return;
    }
    m_physicsWorld->getBodyInterface().AddImpulse(
        rb->bodyId, JPH::Vec3(impulse.x, impulse.y, 0.0f));
}

void Physics2DSystem::setLinearVelocity(Entity& entity, const glm::vec2& velocity)
{
    if (!m_physicsWorld || !m_physicsWorld->isInitialized())
    {
        return;
    }
    auto* rb = entity.getComponent<RigidBody2DComponent>();
    if (!rb || rb->bodyId.IsInvalid())
    {
        return;
    }
    m_physicsWorld->getBodyInterface().SetLinearVelocity(
        rb->bodyId, JPH::Vec3(velocity.x, velocity.y, 0.0f));
}

glm::vec2 Physics2DSystem::getLinearVelocity(const Entity& entity) const
{
    if (!m_physicsWorld || !m_physicsWorld->isInitialized())
    {
        return glm::vec2(0.0f);
    }
    const auto* rb = entity.getComponent<RigidBody2DComponent>();
    if (!rb || rb->bodyId.IsInvalid())
    {
        return glm::vec2(0.0f);
    }
    auto v = m_physicsWorld->getBodyInterface().GetLinearVelocity(rb->bodyId);
    return glm::vec2(v.GetX(), v.GetY());
}

void Physics2DSystem::setTransform(Entity& entity, const glm::vec2& position,
                                   float rotationRadians)
{
    if (!m_physicsWorld || !m_physicsWorld->isInitialized())
    {
        return;
    }
    auto* rb = entity.getComponent<RigidBody2DComponent>();
    if (!rb || rb->bodyId.IsInvalid())
    {
        return;
    }
    auto rot = JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), rotationRadians);
    m_physicsWorld->getBodyInterface().SetPositionAndRotation(
        rb->bodyId,
        JPH::Vec3(position.x, position.y, 0.0f),
        rot,
        JPH::EActivation::Activate);
}

bool Physics2DSystem::isInitialized() const
{
    return m_physicsWorld && m_physicsWorld->isInitialized();
}

} // namespace Vestige
