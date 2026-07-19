// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file grass_config.h
/// @brief Data-driven tuning surface for the GPU grass field (design
///        docs/phases/phase_10_meadow_gpu_grass_design.md §5.2/§5.2a). GL-free so the
///        pure placement predicates (grass_placement.h) and their unit tests can share it.
///
/// This is the "paint vast expanses" control surface: the meadow tunes density, gating,
/// tall/wild blade shape, and clumping here without new code. Defaults are the **tall &
/// wild** meadow (user direction 2026-07-19): long blades (~0.6–1.2 m), strong lean, high
/// clump strength.
#pragma once

#include <glm/glm.hpp>

namespace Vestige
{

/// @brief Rule set for procedural blade placement + clumping. One struct per meadow.
struct GrassConfig
{
    // --- placement (§5.2) ---
    float chunkSize    = 16.0f;   ///< Meadow partitioned into chunkSize×chunkSize cells (m).
    float bladesPerSqM = 20.0f;   ///< v1 base/near density (§7: ≤ ~130/m² under the SSBO budget).
    float slopeCutoff  = 0.55f;   ///< Reject a candidate if terrain normal.y < this (too steep).
    float edgeMargin   = 5.0f;    ///< Inset from the terrain border — getSplatWeight reads
                                  ///< grass=1 out of bounds (terrain.cpp:548), so an unclamped
                                  ///< border candidate would over-spawn (§5.2).
    glm::vec2 exclusionCenter{0.0f}; ///< Pond/water disc centre (world XZ).
    float exclusionRadius = 0.0f;    ///< No blades inside this disc (0 = disabled).

    // --- tall & wild blade shape (§5.2a) ---
    float minHeight = 0.6f, maxHeight = 1.2f;   ///< Base blade height range (m).
    float minWidth  = 0.02f, maxWidth = 0.05f;  ///< Base blade width range (m).
    float minLean   = 0.20f, maxLean  = 0.55f;  ///< Tip-lean fraction (strong = floppy).

    // --- clumping (§5.2a) ---
    float clumpScale    = 1.0f;   ///< grassClump cell size = clumpScale (m); committed kernelR=cellSize.
    float clumpStrength = 0.7f;   ///< Wild↔tidy dial [0,1]; default wild ≈ 0.7 (§5.2a).
};

} // namespace Vestige
