// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file grass_blade.h
/// @brief GL-free blade-seed struct + the pure vertex-generator mirror for the GPU
///        grass field (design docs/phases/phase_10_meadow_gpu_grass_design.md §5.1/§8).
///
/// Each blade is a vertex-shader-generated quadratic Bézier ribbon. `grass.vert.glsl`
/// builds the geometry on the GPU from a per-blade `GrassBlade` seed; this header is the
/// **CPU hand-mirror** of that math (curve eval + width taper + tip), so a Rule-7 parity
/// unit test can pin the two formulas together **without a GL context** — the same
/// pattern as `terrain_material_blend.h`. Only the STATIC blade is mirrored; wind bend
/// and view-facing widening (both time-/view-dependent) are validated by the visual
/// check, not this seam (design §8).
#pragma once

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>

namespace Vestige
{

/// @brief Per-blade seed — `std430`, exactly 32 bytes, field-for-field identical to the
///        `GrassBlade` struct in `grass.vert.glsl`. `rootPos`(0..11)+`height`(12..15) fill
///        the first 16-byte row; `facingAngle`/`lean`/`width`/`hash` the second. The GPU
///        SSBO is an array of these, indexed by `gl_InstanceID + chunkBaseOffset`.
struct GrassBlade
{
    glm::vec3 rootPos{0.0f};   ///< Blade base, world space (Bézier P0).
    float height = 1.0f;       ///< Blade height (m); P1 = root + up·height.

    float facingAngle = 0.0f;  ///< Facing yaw about +Y (rad); bend direction.
    float lean = 0.3f;         ///< P2 = P1 + facingDir·height·lean (tip lean fraction).
    float width = 0.08f;       ///< Full blade width at the base (m); tapers to 0 at tip.
    std::uint32_t hash = 0u;   ///< Wind phase + per-blade tint/height variation (G4).
};
static_assert(sizeof(GrassBlade) == 32, "GrassBlade must be 32 bytes to match std430");

/// @brief Quadratic Bézier point (GPUOpen form), t in [0,1]. Passes through P0 at t=0 and
///        P2 at t=1 (NOT P1). Shared by the CPU mirror and — as the same three lines — by
///        `grass.vert.glsl`.
inline glm::vec3 grassBezier(const glm::vec3& p0, const glm::vec3& p1,
                             const glm::vec3& p2, float t)
{
    const float u = 1.0f - t;
    return (u * u) * p0 + (2.0f * u * t) * p1 + (t * t) * p2;
}

/// @brief Full blade width at curve parameter t — linear taper to 0 at the tip (t=1).
inline float grassBladeWidth(float baseWidth, float t)
{
    return baseWidth * (1.0f - t);
}

/// @brief GL-free mirror of the `grass.vert.glsl` vertex generator (design §5.1).
///        Returns the world position of the strip vertex at row `row` (0..segments) and
///        `side` (-1 = left, +1 = right; the tip row's width is 0 so `side` is a no-op
///        there). An N-segment blade is a `GL_TRIANGLE_STRIP` of `2N+1` verts: rows
///        0..N-1 emit a left+right pair, row N a single centred tip.
/// @param b        Blade seed (root, height, facing, lean, width).
/// @param row      Strip row, 0 (base, on the curve at P0) .. segments (tip, at P2).
/// @param side     -1 or +1 (left/right of the ribbon centre); irrelevant at the tip.
/// @param segments N — the LOD segment count (near 7 / mid 5 / far 3).
inline glm::vec3 grassBladeVertex(const GrassBlade& b, int row, int side, int segments)
{
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec3 dir(std::cos(b.facingAngle), 0.0f, std::sin(b.facingAngle));

    const glm::vec3 p0 = b.rootPos;
    const glm::vec3 p1 = p0 + up * b.height;
    const glm::vec3 p2 = p1 + dir * (b.height * b.lean);

    const float t = static_cast<float>(row) / static_cast<float>(segments);
    const glm::vec3 curve = grassBezier(p0, p1, p2, t);

    // Width axis: horizontal, perpendicular to the facing direction — cross(up, dir).
    const glm::vec3 widthAxis(std::sin(b.facingAngle), 0.0f, -std::cos(b.facingAngle));
    const float halfW = 0.5f * grassBladeWidth(b.width, t);

    return curve + (static_cast<float>(side) * halfW) * widthAxis;
}

} // namespace Vestige
