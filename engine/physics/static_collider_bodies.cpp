// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file static_collider_bodies.cpp
/// @brief Static collider bodies kept in step with the scene (3D_E-0796).
#include "physics/static_collider_bodies.h"
#include "physics/physics_world.h"

#include <Jolt/Physics/Collision/Shape/BoxShape.h>

#include <cstring>

namespace Vestige
{

std::uint64_t hashColliders(const std::vector<AABB>& colliders)
{
    // FNV-1a over each box's six floats, in order. The order is the scene
    // walk's, which only changes when the scene does.
    std::uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&hash](float f)
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &f, sizeof bits);
        for (int i = 0; i < 4; ++i)
        {
            hash ^= (bits >> (8 * i)) & 0xFFu;
            hash *= 1099511628211ULL;
        }
    };
    for (const AABB& box : colliders)
    {
        mix(box.min.x); mix(box.min.y); mix(box.min.z);
        mix(box.max.x); mix(box.max.y); mix(box.max.z);
    }
    return hash;
}

bool StaticColliderBodies::update(PhysicsWorld& world, const std::vector<AABB>& colliders)
{
    if (!m_sync.shouldRebuild(hashColliders(colliders)))
    {
        return false;
    }
    rebuild(world, colliders);
    return true;
}

void StaticColliderBodies::rebuild(PhysicsWorld& world, const std::vector<AABB>& colliders)
{
    m_sync.markBuilt(hashColliders(colliders));
    clear(world);

    // Jolt's default convex radius is 0.05, so half-extents must be at least that.
    constexpr float MIN_HALF_EXTENT = 0.05f;
    for (const AABB& box : colliders)
    {
        const glm::vec3 halfExtents = box.getSize() * 0.5f;
        if (halfExtents.x < MIN_HALF_EXTENT || halfExtents.y < MIN_HALF_EXTENT ||
            halfExtents.z < MIN_HALF_EXTENT)
        {
            continue;
        }
        JPH::BoxShape* const shape = new JPH::BoxShape(
            JPH::Vec3(halfExtents.x, halfExtents.y, halfExtents.z));
        m_bodies.push_back(world.createStaticBody(shape, box.getCenter()));
    }
}

void StaticColliderBodies::clear(PhysicsWorld& world)
{
    for (const JPH::BodyID id : m_bodies)
    {
        world.destroyBody(id);
    }
    m_bodies.clear();
}

} // namespace Vestige
