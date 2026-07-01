// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file impact_audio_system.h
/// @brief AX4 S7 — procedural object-collision audio. Subscribes to the S3
///        `CollisionEvent` bus and synthesises a material-aware impact strike
///        per contact onset via AudioEngine::playSynth (the S5 play glue).
#pragma once

#include "core/i_system.h"
#include "physics/surface_material.h"

#include <cstdint>
#include <string>

namespace Vestige
{

class AudioEngine;
class EventBus;
struct CollisionEvent;

// ---------------------------------------------------------------------------
// Pure decision logic — no engine deps, unit-tested in isolation
// ---------------------------------------------------------------------------

/// @brief Loudness/timbre-dominance rank of a surface material when two bodies
///        strike. Higher = harder / brighter, so it wins the impact timbre.
///        Order (design §8): Metal > Stone > Glass > Wood > Dirt > Grass >
///        Sand > Cloth > Water > Default (untagged is the softest partner).
///        AX4 S7.
int materialHardnessRank(SurfaceMaterial material);

/// @brief The harder (higher-ranked) of two struck materials — the one whose
///        timbre dominates the impact sound. Ties keep `a`. AX4 S7.
SurfaceMaterial harderMaterial(SurfaceMaterial a, SurfaceMaterial b);

/// @brief What (if anything) an impact should sound like. `play == false`
///        means the collision stays silent (not an onset, sub-threshold, or a
///        suppressed untagged-vs-untagged contact).
struct ImpactDecision
{
    bool            play = false;
    SurfaceMaterial material = SurfaceMaterial::Default;
};

/// @brief Decide whether a collision event synthesises an impact, and with
///        which material. Pure — the whole S7 policy in one testable function:
///        - Exit events are silent (audio cares about the onset only).
///        - Below `kMinImpactSpeed` (contact_event.h) is inaudible → silent.
///        - Both bodies `Default` (untagged) → silent unless
///          `emitUntaggedCollisions` (else an unauthored scene full of untagged
///          boxes would thud on every contact — design §8, an explicit gate).
///        - Otherwise play the harder material's timbre.
/// AX4 S7.
ImpactDecision decideImpact(const CollisionEvent& event, bool emitUntaggedCollisions);

// ---------------------------------------------------------------------------
// The system
// ---------------------------------------------------------------------------

/// @brief Turns physics collisions into procedural impact sounds. It owns no
///        per-frame work: on initialize() it subscribes to the `CollisionEvent`
///        bus (published by PhysicsWorld's main-thread contact drain, S3) and
///        synthesises inside that synchronous dispatch. update() is a no-op.
///
/// Emission is naturally bounded by the contact-*onset* rate (one Enter per
/// body pair at first contact, then silence until they separate) and the
/// 32-voice source pool, so — mirroring S3's dropped per-pair throttle — no
/// per-frame synth cap is needed (see the S7 as-built note in the design doc).
class ImpactAudioSystem : public ISystem
{
public:
    ImpactAudioSystem() = default;

    // -- ISystem interface --
    const std::string& getSystemName() const override { return m_name; }
    bool initialize(Engine& engine) override;
    void shutdown() override;
    void update(float deltaTime) override { (void)deltaTime; }  // event-driven

    /// @brief Event-driven and global: stay registered/subscribed regardless of
    ///        scene contents. Mirrors AudioSystem / FootstepSystem.
    bool isForceActive() const override { return true; }

    /// @brief Force-enable Default↔Default (untagged) impact audio, off by
    ///        default. S9 wires this to the `proceduralAudio.emitUntaggedCollisions`
    ///        setting; exposed now so that wiring is a one-liner.
    void setEmitUntaggedCollisions(bool enabled) { m_emitUntaggedCollisions = enabled; }

private:
    /// @brief Bus callback: decide + synthesise one impact strike.
    void onCollision(const CollisionEvent& event);

    static inline const std::string m_name = "ImpactAudio";

    AudioEngine* m_audio = nullptr;
    EventBus*    m_bus = nullptr;
    std::uint32_t m_subId = 0;              ///< CollisionEvent subscription (0 = none).
    bool         m_emitUntaggedCollisions = false;
};

} // namespace Vestige
