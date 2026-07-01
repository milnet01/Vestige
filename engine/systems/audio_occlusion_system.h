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

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Vestige
{

class Engine;
class PhysicsWorld;

/// @brief Upper bound on rays cast per source, and the size of the offset
///        table. Single source of truth shared by the offset table
///        (`occlusionRayOffsets`), the §4.5 per-frame ray budget (S4), and the
///        §5 settings clamp (S5) — bump it here and all three follow.
inline constexpr int kMaxOcclusionRayCount = 16;

/// @brief Result of one source's occlusion measurement.
struct OcclusionMeasurement
{
    /// @brief Whether any sampled ray hit solid geometry.
    bool blocked = false;

    /// @brief Target occlusion fraction the source slews toward:
    ///        `blockedRays / rayCount` ∈ [0,1]. Binary when `rayCount == 1`
    ///        (0 = clear line of sight, 1 = blocked); graded across N rays.
    float targetFraction = 0.0f;

    /// @brief Surface preset of the nearest blocking body (smallest hit
    ///        distance across all blocked rays). Only meaningful when
    ///        `blocked`; a clear path leaves it `Air` and the caller holds the
    ///        source's previous material (irrelevant at fraction 0).
    AudioOcclusionMaterialPreset material = AudioOcclusionMaterialPreset::Air;
};

/// @brief The fixed per-ray offset table: entry 0 is the zero vector (ray 0
///        aims at the exact source centre); entries 1..N-1 are a
///        Fibonacci-sphere distribution of unit vectors over the source
///        sphere. Scaled by `sourceRadius` at the call site. Deterministic (no
///        RNG) so occlusion never shimmers and tests are reproducible.
///
/// A Fibonacci lattice needs `sin/cos/sqrt`, so the table is built once at
/// first use (thread-safe static init) rather than as a `constexpr` literal —
/// the property that matters (a stable, reproducible point set) is preserved.
const std::array<glm::vec3, kMaxOcclusionRayCount>& occlusionRayOffsets();

/// @brief Casts `rayCount` rays from the listener toward points sampled inside
///        a sphere of radius `sourceRadius` around the source, and returns the
///        volumetric occlusion measurement. `rayCount == 1` samples only the
///        centre → the binary single-ray case. A per-ray coincident
///        listener/target (zero-length direction) is skipped, not cast.
///
/// @param physics     Settled physics world (read-only narrow-phase query).
/// @param listenerPos World-space listener (camera) position — the ray origin.
/// @param sourcePos   World-space source position — the sphere centre.
/// @param rayCount    Rays to sample, clamped to [1, kMaxOcclusionRayCount].
/// @param sourceRadius Radius of the source sampling sphere (metres).
/// @param ignoreBody  The source's own physics body, excluded so a source can
///                    never occlude itself. Pass an invalid id ({}) when the
///                    source has no body.
/// @note Main-thread only — resolves the nearest blocking body's material via
///       `getSurfaceMaterial`, which takes a body lock (`physics_world.h`).
OcclusionMeasurement measureOcclusion(const PhysicsWorld& physics,
                                      const glm::vec3& listenerPos,
                                      const glm::vec3& sourcePos,
                                      int rayCount = 1,
                                      float sourceRadius = 0.0f,
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
