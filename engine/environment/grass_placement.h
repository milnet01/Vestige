// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file grass_placement.h
/// @brief GL-free placement predicates + deterministic per-blade seed builder for the GPU
///        grass field (design docs/phases/phase_10_meadow_gpu_grass_design.md §5.2/§8).
///
/// The CPU scatter loop in grass_renderer.cpp calls these; they are factored out here as
/// **pure functions** so the placement gating (grass-weight response, slope reject, pond
/// exclusion) is unit-testable without a GL context or a Terrain — the same pattern as the
/// billboard `scatterProps` predicate tests (§8). Seeds are derived from a deterministic
/// integer hash (reused from grass_clump.h) so a fixed chunk id + index reproduces the
/// exact field — reproducible and testable.
#pragma once

#include "environment/grass_blade.h"
#include "environment/grass_clump.h"   // grassHashU32 / grassU32ToUnit (reuse, Rule 3)
#include "environment/grass_config.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace Vestige
{

/// @brief Accept a scatter candidate given its terrain slope, grass-splat weight, and a
///        deterministic roll in [0,1). Steep slopes are rejected outright; otherwise a
///        blade spawns with probability ∝ the grass-splat weight, so the field thins out
///        over the C1 dirt patches and rock and grows where grass is painted (§5.2).
inline bool grassCandidateAccepted(float terrainNormalY, float grassWeight, float roll01,
                                   const GrassConfig& cfg)
{
    if (terrainNormalY < cfg.slopeCutoff)
    {
        return false;   // too steep
    }
    return roll01 < grassWeight;   // spawn probability ∝ grass weight (0 weight → 0 blades)
}

/// @brief True if world XZ falls inside the pond/water exclusion disc (no blades there).
///        Disabled when `radius <= 0`.
inline bool grassInExclusionDisc(float worldX, float worldZ, glm::vec2 center, float radius)
{
    if (radius <= 0.0f)
    {
        return false;
    }
    const float dx = worldX - center.x;
    const float dz = worldZ - center.y;
    return (dx * dx + dz * dz) < (radius * radius);
}

/// @brief Build a deterministic per-blade seed from a unique scatter key (chunk id + index)
///        and the config's tall/wild ranges. Facing/height/width/lean are hash-derived so
///        the same key reproduces the same blade; `hash` packs per-blade wind/tint/height
///        variation for the shader (§5.5). Clump height/lean/bend are applied later in the
///        VS (§5.2a) — they are a function of `rootPos`, not stored here (struct stays 32 B).
inline GrassBlade makeGrassBlade(const glm::vec3& rootPos, std::uint32_t key,
                                 const GrassConfig& cfg)
{
    const float r0 = grassU32ToUnit(grassHashU32(key ^ 0xA1B2C3D4u));
    const float r1 = grassU32ToUnit(grassHashU32(key ^ 0xB2C3D4E5u));
    const float r2 = grassU32ToUnit(grassHashU32(key ^ 0xC3D4E5F6u));
    const float r3 = grassU32ToUnit(grassHashU32(key ^ 0xD4E5F607u));

    GrassBlade b;
    b.rootPos     = rootPos;
    b.height      = glm::mix(cfg.minHeight, cfg.maxHeight, r0);
    b.facingAngle = r1 * 6.2831853f;
    b.lean        = glm::mix(cfg.minLean, cfg.maxLean, r2);
    b.width       = glm::mix(cfg.minWidth, cfg.maxWidth, r3);
    b.hash        = grassHashU32(key);
    return b;
}

} // namespace Vestige
