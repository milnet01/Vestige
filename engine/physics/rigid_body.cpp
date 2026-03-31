/// @file rigid_body.cpp
/// @brief RigidBody component implementation.
#include "physics/rigid_body.h"
#include "physics/jolt_helpers.h"
#include "scene/entity.h"

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>

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

    // Create the collision shape
    JPH::ShapeRefC shape;
    switch (shapeType)
    {
    case CollisionShapeType::BOX:
        shape = new JPH::BoxShape(JPH::Vec3(shapeSize.x, shapeSize.y, shapeSize.z));
        break;
    case CollisionShapeType::SPHERE:
        shape = new JPH::SphereShape(shapeSize.x);
        break;
    case CollisionShapeType::CAPSULE:
        shape = new JPH::CapsuleShape(shapeSize.y, shapeSize.x);  // halfHeight, radius
        break;
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
    copy->setEnabled(isEnabled());
    // Note: body is NOT cloned — must call createBody() on the clone
    return copy;
}

} // namespace Vestige
