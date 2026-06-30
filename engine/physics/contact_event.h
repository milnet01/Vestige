// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file contact_event.h
/// @brief Collision-event bus (AX4 S3): a single Jolt ContactListener that
///        turns physics contacts into EventBus `CollisionEvent`s for the
///        procedural-audio impact path (S7) and the scripting
///        OnCollisionEnter/Exit nodes (S8).
///
/// Threading contract (Jolt 5.3.0, verified against ContactListener.h:66-116):
/// the OnContact* callbacks run on multiple Jolt job threads at once and may
/// only *read* from the bodies — never touch a BodyInterface or any locking
/// accessor. We therefore read velocity + user-data straight off the
/// already-locked `Body&` refs (lock-free), and the only lock taken is the
/// listener's OWN std::mutex guarding its private pending-queue + pair-cache.
/// The main thread drains the queue after PhysicsSystem::Update and publishes
/// on the (synchronous) EventBus, so audio/scripting subscribers never run in
/// a Jolt callback.
///
/// Model: one Enter event per contact *onset* (OnContactAdded), one Exit per
/// removal (OnContactRemoved). OnContactPersisted is ignored — impact audio
/// cares about the moment of contact, not its continuation, and a resting pair
/// must stay silent. Jolt guarantees a matching OnContactRemoved for every
/// OnContactAdded, so Enter/Exit stay balanced for the scripting nodes (S8).
/// The Enter event carries `approachSpeed`; the *audio* subscriber (S7) applies
/// the min-speed threshold + Default-material suppression, so a feather-touch
/// still raises a scripting enter but plays no sound. Keeping the threshold out
/// of this layer is what makes Enter/Exit balanced (a thresholded Enter with an
/// un-thresholded Exit would not be) and keeps the bus generic.
#pragma once

#include "core/event.h"
#include "physics/surface_material.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/Shape/SubShapeIDPair.h>

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Vestige
{

/// @brief Recommended minimum normal approach speed (m/s) below which a
///        contact is inaudible. NOT applied in this layer — the bus publishes
///        every contact onset; the *audio* subscriber (S7) uses this to decide
///        whether to synthesise an impact. Kept here so producer and consumer
///        share one number.
/// TODO: revisit via Formula Workbench (perceptual onset threshold).
inline constexpr float kMinImpactSpeed = 0.5f;

/// @brief POD enqueued from a Jolt job thread; drained on the main thread.
///        Not an EventBus event (those must inherit Event + are published
///        only on the main thread).
struct PendingContact
{
    enum class Phase { Enter, Exit };

    EntityId        entityA = 0;
    EntityId        entityB = 0;
    glm::vec3       point{0.0f};            ///< Enter only (zero on Exit).
    glm::vec3       normal{0.0f};           ///< Enter only.
    float           approachSpeed = 0.0f;   ///< Enter only, m/s.
    SurfaceMaterial matA = SurfaceMaterial::Default;  ///< Enter only.
    SurfaceMaterial matB = SurfaceMaterial::Default;  ///< Enter only.
    Phase           phase = Phase::Enter;
};

/// @brief EventBus payload. Fields after the entity ids are valid only when
///        `isEnter` (Exit cannot carry geometry — Jolt's OnContactRemoved has
///        no body access at all).
struct CollisionEvent : public Event
{
    EntityId        entityA = 0;
    EntityId        entityB = 0;
    glm::vec3       point{0.0f};
    glm::vec3       normal{0.0f};
    float           approachSpeed = 0.0f;
    SurfaceMaterial matA = SurfaceMaterial::Default;
    SurfaceMaterial matB = SurfaceMaterial::Default;
    bool            isEnter = true;

    CollisionEvent() = default;

    explicit CollisionEvent(const PendingContact& pc)
        : entityA(pc.entityA)
        , entityB(pc.entityB)
        , point(pc.point)
        , normal(pc.normal)
        , approachSpeed(pc.approachSpeed)
        , matA(pc.matA)
        , matB(pc.matB)
        , isEnter(pc.phase == PendingContact::Phase::Enter)
    {
    }
};

/// @brief The single registered Jolt ContactListener. Enqueues contacts on
///        job threads; the owning PhysicsWorld drains + publishes them.
class ContactEventListener final : public JPH::ContactListener
{
public:
    ContactEventListener() = default;

    /// @brief Swaps the accumulated pending contacts out for main-thread
    ///        draining. Reuses the internal buffer across frames (no per-frame
    ///        allocation in steady state). Must be called every frame even
    ///        when there is no EventBus, or the queue would grow unbounded.
    const std::vector<PendingContact>& takeDrained()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_drained.clear();
        m_drained.swap(m_pending);
        return m_drained;
    }

    void OnContactAdded(const JPH::Body& b1, const JPH::Body& b2,
                        const JPH::ContactManifold& manifold,
                        JPH::ContactSettings&) override
    {
        if (manifold.mRelativeContactPointsOn1.empty())
        {
            return;
        }

        // All body reads here are lock-free and sanctioned inside the callback
        // (the bodies are locked by Jolt). The manifold stores points relative
        // to mBaseOffset; GetPointVelocity wants world space, so use the
        // world-space accessor for the first (deepest) contact point.
        const JPH::RVec3 worldPoint = manifold.GetWorldSpaceContactPointOn1(0);
        const JPH::Vec3  normal = manifold.mWorldSpaceNormal;
        const JPH::Vec3  relVel = b1.GetPointVelocity(worldPoint) - b2.GetPointVelocity(worldPoint);
        const float      approachSpeed = std::abs(relVel.Dot(normal));

        const std::uint64_t ud1 = b1.GetUserData();
        const std::uint64_t ud2 = b2.GetUserData();
        const EntityId      eA = unpackEntity(ud1);
        const EntityId      eB = unpackEntity(ud2);

        const JPH::SubShapeIDPair key(b1.GetID(), manifold.mSubShapeID1,
                                      b2.GetID(), manifold.mSubShapeID2);

        PendingContact pc;
        pc.entityA = eA;
        pc.entityB = eB;
        pc.point = glm::vec3(worldPoint.GetX(), worldPoint.GetY(), worldPoint.GetZ());
        pc.normal = glm::vec3(normal.GetX(), normal.GetY(), normal.GetZ());
        pc.approachSpeed = approachSpeed;
        pc.matA = unpackMaterial(ud1);
        pc.matB = unpackMaterial(ud2);
        pc.phase = PendingContact::Phase::Enter;

        std::lock_guard<std::mutex> lock(m_mutex);
        m_pairCache[key] = PairInfo{eA, eB};  // remember the pair for the Exit.
        m_pending.push_back(pc);
    }

    // OnContactPersisted intentionally NOT overridden — an ongoing contact is
    // not a new impact, so the base no-op is correct (a resting pair stays
    // silent).

    void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_pairCache.find(pair);
        if (it == m_pairCache.end())
        {
            return;  // contact we never recorded (e.g. predates the listener).
        }
        PendingContact pc;
        pc.entityA = it->second.entityA;
        pc.entityB = it->second.entityB;
        pc.phase = PendingContact::Phase::Exit;
        m_pending.push_back(pc);
        m_pairCache.erase(it);
    }

private:
    /// @brief Per-contact record kept so OnContactRemoved (which has no body
    ///        access at all) can still name the entity pair on Exit.
    struct PairInfo
    {
        EntityId entityA = 0;
        EntityId entityB = 0;
    };

    std::mutex m_mutex;
    std::vector<PendingContact> m_pending;   ///< Filled on job threads.
    std::vector<PendingContact> m_drained;   ///< Reused main-thread swap buffer.
    std::unordered_map<JPH::SubShapeIDPair, PairInfo> m_pairCache;  ///< Live contacts.
};

} // namespace Vestige
