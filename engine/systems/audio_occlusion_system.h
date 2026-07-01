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
#include <unordered_map>
#include <vector>

namespace Vestige
{

class Engine;
class PhysicsWorld;
class JobSystem;
class AudioEngine;

/// @brief Upper bound on rays cast per source, and the size of the offset
///        table. Single source of truth shared by the offset table
///        (`occlusionRayOffsets`), the §4.5 per-frame ray budget (S4), and the
///        §5 settings clamp (S5) — bump it here and all three follow.
inline constexpr int kMaxOcclusionRayCount = 16;

/// @brief Hard ceiling on rays cast per frame across all sources (§4.5). The
///        pool-max product `MAX_SOURCES(32) × kMaxOcclusionRayCount(16)`, so at
///        the settings maximum the offered load exactly meets it and the
///        round-robin never engages within the shipped ranges. It engages only
///        if a future bump pushes the offered load past this.
/// TODO: revisit via Formula Workbench once measured cast-cost data exists.
inline constexpr int kMaxOcclusionRaysPerFrame = 512;

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

/// @brief One source's occlusion measurement request, for the batched
///        (parallel) path.
struct OcclusionQuery
{
    glm::vec3 listenerPos{0.0f};
    glm::vec3 sourcePos{0.0f};
    int rayCount = 1;
    float sourceRadius = 0.0f;
    JPH::BodyID ignoreBody;
};

/// @brief Measures many sources at once, casting every ray across MT2 workers
///        and resolving materials on the calling (main) thread afterwards.
///        Returns one measurement per query, in input order.
///
/// The casts (read-only narrow-phase queries on a settled world) run in a
/// `jobs.parallelFor`; the caller's thread `wait()`s and then resolves each
/// source's nearest-blocker material via `getSurfaceMaterial` (a main-thread
/// body lock). In a synchronous `JobSystem` (`numWorkers==0`) the parallelFor
/// runs inline, so the result is bit-identical to calling `measureOcclusion`
/// per query — the property the S4 determinism test asserts.
///
/// @note Main-thread only (same body-lock caveat as `measureOcclusion`).
std::vector<OcclusionMeasurement> measureOcclusionBatch(
    const PhysicsWorld& physics, JobSystem& jobs,
    const std::vector<OcclusionQuery>& queries);

/// @brief Round-robin servicing plan under the per-frame ray budget (§4.5).
struct OcclusionServicingPlan
{
    /// @brief Indices (into the eligible list) serviced — "measured" — this
    ///        frame. The rest are "deferred" and hold their previous target.
    std::vector<int> serviced;

    /// @brief Cursor to start from next frame (wraps the eligible list).
    int nextCursor = 0;

    /// @brief True when the budget forced deferral (offered load > budget).
    bool engaged = false;
};

/// @brief Decides which of `eligibleCount` sources (each costing `rayCount`
///        rays) to service this frame, starting at `cursor`, under `rayBudget`.
///        When the offered load fits, every source is serviced and the cursor
///        resets. When it exceeds the budget, the largest prefix that fits is
///        serviced starting at `cursor` (wrapping), the cursor advances past
///        it, and `engaged` is set. `rayCount <= 0` is treated as 1.
OcclusionServicingPlan planOcclusionServicing(int eligibleCount, int rayCount,
                                              int rayBudget, int cursor);

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
    /// @brief The occlusion target a source slews toward. A *measured* source
    ///        refreshes it each frame; a round-robin-*deferred* source holds it
    ///        (so fraction + material never diverge); a *culled* source's
    ///        fraction is forced to 0 to release it.
    struct OcclusionTarget
    {
        float fraction = 0.0f;
        AudioOcclusionMaterialPreset material = AudioOcclusionMaterialPreset::Air;
    };

    static inline const std::string m_name = "AudioOcclusion";
    Engine* m_engine = nullptr;

    /// @brief Cached at init from AudioSystem — the store the occlusion settings
    ///        (enabled / ray count / max distance / source radius) live on. Null
    ///        if there is no AudioSystem (some test harnesses); then the system
    ///        falls back to its built-in defaults, enabled.
    AudioEngine* m_audioEngine = nullptr;

    /// @brief Per-source target, keyed by entity id. Persists across frames so
    ///        a deferred source can hold its last measurement. Reaped for
    ///        entities no longer in the scene.
    std::unordered_map<std::uint32_t, OcclusionTarget> m_targets;

    /// @brief Round-robin cursor into the eligible list — where servicing
    ///        resumes next frame when the ray budget forces deferral (§4.5).
    int m_roundRobinCursor = 0;

    /// @brief One-shot guard so budget engagement is logged once, not per frame
    ///        (project no-silent-cap rule).
    bool m_budgetEngagedLogged = false;
};

} // namespace Vestige
