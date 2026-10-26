// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file static_collider_bodies.h
/// @brief Keeps Jolt's static boxes matching the scene's colliders.
#pragma once

#include "physics/collider_sync.h"
#include "utils/aabb.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

#include <cstdint>
#include <vector>

namespace Vestige
{

class PhysicsWorld;

/// @brief A fingerprint of a collider set: equal sets give equal hashes, and
///        adding, moving, resizing or removing a box changes it.
std::uint64_t hashColliders(const std::vector<AABB>& colliders);

/// @brief One static Jolt box per scene collider, rebuilt when the set changes.
///
/// The engine hands it the scene's colliders (blocking props and tree trunks)
/// every frame. Opening another scene, or adding, moving or deleting a
/// blocking prop, changes the set; once the change has held for a frame
/// (ColliderSync), the old boxes are removed and new ones built, so the
/// physics walker meets what the scene now holds (3D_E-0796).
class StaticColliderBodies
{
public:
    /// @brief Rebuilds when the set changed and has settled. Returns true when
    ///        it rebuilt.
    bool update(PhysicsWorld& world, const std::vector<AABB>& colliders);

    /// @brief Replaces every box with one per collider in @p colliders. Boxes
    ///        thinner than Jolt's convex radius are skipped.
    void rebuild(PhysicsWorld& world, const std::vector<AABB>& colliders);

    /// @brief Removes every box this object built.
    void clear(PhysicsWorld& world);

    /// @brief The bodies built by the last rebuild.
    const std::vector<JPH::BodyID>& bodies() const { return m_bodies; }

private:
    ColliderSync m_sync;
    std::vector<JPH::BodyID> m_bodies;
};

} // namespace Vestige
