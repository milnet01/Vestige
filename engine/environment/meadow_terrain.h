// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <glm/glm.hpp>

namespace Vestige
{

/// @file meadow_terrain.h
/// @brief Pure, GL-free helpers for the meadow benchmark scene (3D_E-0027).
///
/// The scene builder (`Engine::setupDemoScene`) is GL-bound and cannot run in
/// the headless unit-test binary. These two deterministic functions carry the
/// numerical work — the terrain height field and the prop scatter — so they can
/// be unit-tested without a GL context (design §5.2). They take no global RNG
/// and no wall-clock, so the scene is an identical run-to-run profiling fixture
/// (design §8 determinism).

/// @brief Shape parameters for the meadow height field.
struct MeadowShape
{
    /// @brief One summed sine term of the low-frequency hill relief.
    struct Octave
    {
        float freq;  ///< Spatial frequency (cycles across the terrain, both axes).
        float amp;   ///< Amplitude added to the normalized height.
    };

    std::vector<Octave> octaves;         ///< Summed hill relief.
    glm::vec2 pondCenterGrid{0.5f, 0.5f}; ///< Bowl centre in 0..1 grid space.
    float pondRadiusGrid = 0.11f;        ///< Bowl radius in grid space.
    float bowlDepth01 = 0.09f;           ///< Max normalized dip at the bowl centre.
    float baseHeight01 = 0.14f;          ///< Flat offset before octaves.
};

/// @brief Normalized (0..1) terrain height at grid fraction (nx, nz).
///
/// Sums the shape's sine octaves for gentle rolling hills, then subtracts a
/// smooth radial bowl (smoothstep falloff) centred on the pond so its floor
/// sits below the water level. GL-free and deterministic; the result is clamped
/// to [0, 1] (the range `Terrain::setRawHeight` expects).
float meadowHeight01(float nx, float nz, const MeadowShape& shape);

/// @brief Parameters for a jittered-grid prop scatter over a world-XZ region.
struct ScatterParams
{
    glm::vec2 regionMin{0.0f};        ///< World-XZ scatter bounds (min corner).
    glm::vec2 regionMax{0.0f};        ///< World-XZ scatter bounds (max corner).
    float cellSize = 8.0f;            ///< Jittered-grid cell spacing (world units).
    float jitter = 0.8f;              ///< Per-cell position jitter [0..1] of a cell.
    float minDist = 0.0f;             ///< Reject a point closer than this to an accepted one.
    glm::vec2 exclusionCenter{0.0f};  ///< Disc centre to reject inside (the pond).
    float exclusionRadius = 0.0f;     ///< Reject points within this radius (0 = no disc).
    float minScale = 1.0f;            ///< Per-point uniform scale range (min).
    float maxScale = 1.0f;            ///< Per-point uniform scale range (max).
};

/// @brief One placed prop: world-XZ position, yaw in degrees, uniform scale.
struct ScatterPoint
{
    float x = 0.0f;
    float z = 0.0f;
    float yawDeg = 0.0f;
    float scale = 1.0f;
};

/// @brief Deterministic jittered-grid scatter within a region.
///
/// Walks a grid of `cellSize` cells over [regionMin, regionMax], jitters each
/// cell centre by `jitter`, rejects points inside the exclusion disc or closer
/// than `minDist` to an already-accepted point, and assigns a per-point yaw and
/// scale. Fully determined by `seed` — same seed + params yields the same
/// points in the same order.
std::vector<ScatterPoint> scatterProps(uint32_t seed, const ScatterParams& params);

/// @brief World-space terrain height sampler: `float(worldX, worldZ)`.
using HeightSampler = std::function<float(float, float)>;

/// @brief Geometric knobs for the pond fill/size solve (design §8). Defaults are
/// the derived provisional values for the authored bowl; all in world metres
/// except `nRays`. `desiredDepth` is the one art-directed value (Formula-Workbench
/// TODO at the call site).
struct PondFillParams
{
    float desiredDepth = 1.5f;   ///< Target depth above the floor when the rim allows.
    float rimMargin = 0.5f;      ///< Keep water this far below the spill height (≥ cross-ray crest variation).
    float minDepth = 0.5f;       ///< Floor for a degenerate basin; OVERRIDES containment (design §3.1).
    float edgePad = 2.0f;        ///< Sheet extends this far past the flood radius (onto dry ground).
    int   nRays = 128;           ///< Angular ray count (shoreline-radius change per gap < edgePad).
    float scanFactor = 1.5f;     ///< R_SCAN = scanFactor · rRimWorld (reach saddles past the carve boundary).
    float marchStep = 0.5f;      ///< Radial sample step (≤ grid spacing, so no crest is stepped over).
};

/// @brief Result of the pond fill solve.
struct PondFill
{
    float waterLevelY = 0.0f;   ///< World Y of the level water surface.
    float floodRadius = 0.0f;   ///< Max world radius of the centre-connected flooded region.
    float spillHeight = 0.0f;   ///< Lowest ridge crest water would have to cross to escape.
};

/// @brief Solve a physically-contained pond level and footprint for the meadow
/// bowl (design §3.1/§3.2). Marches `nRays` rays out to `scanFactor·rRimWorld`,
/// storing each ray's height profile; pass 1 sets `spillHeight` = min over rays of
/// the per-ray ridge crest and clamps `waterLevelY` below it (with a `minDepth`
/// degenerate guard that overrides containment); pass 2 sets `floodRadius` = max
/// over rays of the first outward crossing of `waterLevelY` (the centre-connected
/// shoreline). GL-free and deterministic — the scene passes a `Terrain::getHeight`
/// lambda, the unit test a `meadowHeight01` wrapper. Assumes a single radial bowl
/// with a monotone carve (design §9); §7 tests validate containment + coverage.
PondFill computePondFill(const HeightSampler& sampleHeight, glm::vec2 centreWorld,
                         float rRimWorld, float bowlFloorY, const PondFillParams& params);

}  // namespace Vestige
