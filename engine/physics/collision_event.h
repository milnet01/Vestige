// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file collision_event.h
/// @brief Jolt-free collision-event payload (AX4 S3; header split in S10).
///
/// The EventBus `CollisionEvent`, the `PendingContact` POD it is built from,
/// and the shared `kMinImpactSpeed` constant — none of which touch Jolt. Audio
/// (S7 `ImpactAudioSystem`) and scripting (S8 `OnCollisionEnter/Exit`) both
/// subscribe to `CollisionEvent`; including this header lets them do so without
/// dragging the Jolt physics backend into their translation unit.
///
/// The Jolt-side *producer* — the `ContactEventListener` that reads contacts
/// off Jolt bodies on job threads — lives in `physics/contact_event.h`, which
/// includes this header. See that file for the full threading contract and the
/// one-Enter-per-onset / one-Exit-per-removal model.
#pragma once

#include "core/event.h"
#include "physics/surface_material.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace Vestige
{

/// @brief Recommended minimum normal approach speed (m/s) below which a
///        contact is inaudible. NOT applied in the bus layer — every contact
///        onset is published; the *audio* subscriber (S7) uses this to decide
///        whether to synthesise an impact. Kept here so producer and consumer
///        share one number.
/// TODO: revisit via Formula Workbench (perceptual onset threshold).
inline constexpr float kMinImpactSpeed = 0.5f;

/// @brief POD enqueued from a Jolt job thread; drained on the main thread.
///        Not an EventBus event (those must inherit Event + are published
///        only on the main thread). Jolt-free by construction — the producer
///        fills it from body reads, but the struct itself names no Jolt type.
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

} // namespace Vestige
