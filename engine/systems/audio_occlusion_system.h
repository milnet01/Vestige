// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_occlusion_system.h
/// @brief Drives per-source geometric audio occlusion from scene geometry (AX1).
#pragma once

#include "core/i_system.h"
#include "audio/audio_occlusion.h"

#include <Jolt/Jolt.h>  // prelude: defines JPH_NAMESPACE_* + fixed-width types
#include <Jolt/Physics/Body/BodyID.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Vestige
{

class Engine;
class PhysicsWorld;

/// @brief Result of one source's occlusion measurement (S2: single centre ray).
struct OcclusionMeasurement
{
    /// @brief Whether the listener→source line hit solid geometry.
    bool blocked = false;

    /// @brief Target occlusion fraction the source slews toward. Binary in S2
    ///        (0 = clear line of sight, 1 = blocked); graded across N rays in S3.
    float targetFraction = 0.0f;

    /// @brief Surface preset of the nearest blocking body. Only meaningful when
    ///        `blocked`; a clear path leaves it `Air` and the caller holds the
    ///        source's previous material (irrelevant at fraction 0).
    AudioOcclusionMaterialPreset material = AudioOcclusionMaterialPreset::Air;
};

/// @brief Casts the S2 single centre ray from the listener toward the source and
///        returns the occlusion measurement. A coincident listener/source
///        (zero-length direction) is treated as unoccluded — no cast.
///
/// @param physics    Settled physics world (read-only narrow-phase query).
/// @param listenerPos World-space listener (camera) position — the ray origin.
/// @param sourcePos  World-space source position — the ray target.
/// @param ignoreBody The source's own physics body, excluded so a source can
///                   never occlude itself. Pass an invalid id ({}) when the
///                   source has no body.
/// @note Main-thread only — resolves the blocking body's material via
///       `getSurfaceMaterial`, which takes a body lock (`physics_world.h`).
OcclusionMeasurement measureOcclusion(const PhysicsWorld& physics,
                                      const glm::vec3& listenerPos,
                                      const glm::vec3& sourcePos,
                                      JPH::BodyID ignoreBody = JPH::BodyID());

/// @brief Eases `current` toward `target` by `slewAmount` ∈ [0,1] and returns
///        the new value (`slewAmount == 1` snaps). The temporal smoothing that
///        turns a discrete ray flip into a fade instead of a click.
float slewOcclusionFraction(float current, float target, float slewAmount);

/// @brief Fills each spatial audio source's `occlusionFraction` +
///        `occlusionMaterial` from actual scene geometry, each frame, by
///        ray-casting from the listener toward the source.
///
/// This is the geometry driver that closes the AX1 gap: a wall added between a
/// sound and the listener now muffles it without the level designer hand-tagging
/// `occlusionFraction`. The pure DSP layer (`audio_occlusion.h`) and the
/// compose→apply path (`AudioSystem`) already consume the two fields — this
/// system only populates them *before* `AudioSystem` reads them.
///
/// Design of record: docs/phases/phase_10_audio_occlusion_design.md.
///
/// S2 scope: a single centre ray per spatial source (binary blocked/clear),
/// resolved on the main thread (no jobs yet), with per-source temporal
/// smoothing so the fraction never snaps. Multi-ray volumetric sampling (S3),
/// MT2 parallelisation + budget/cull (S4), and settings/editor exposure (S5)
/// build on this skeleton.
class AudioOcclusionSystem : public ISystem
{
public:
    AudioOcclusionSystem() = default;

    // -- ISystem interface --
    const std::string& getSystemName() const override { return m_name; }
    bool initialize(Engine& engine) override;
    void shutdown() override;
    void update(float deltaTime) override;
    std::vector<uint32_t> getOwnedComponentTypes() const override;

    /// @brief Runs in `PostCamera` — the same phase as `AudioSystem` — and is
    ///        registered *before* it, so the stable-sort schedules occlusion
    ///        first. The listener is settled (camera already stepped) and the
    ///        occlusion fields written here are read by `AudioSystem`'s compose
    ///        loop later the same frame.
    UpdatePhase getUpdatePhase() const override { return UpdatePhase::PostCamera; }

private:
    static inline const std::string m_name = "AudioOcclusion";
    Engine* m_engine = nullptr;
};

} // namespace Vestige
