// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "environment/meadow_terrain.h"

#include <algorithm>
#include <cmath>

namespace Vestige
{

namespace
{

/// Integer avalanche hash (Chris Wellons' lowbias32). Maps a 32-bit key to a
/// well-distributed 32-bit value — the deterministic RNG substrate for the
/// scatter (no global RNG so results are reproducible run-to-run).
uint32_t hashU32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

/// A deterministic float in [0, 1) from three mixed keys.
float hash01(uint32_t a, uint32_t b, uint32_t c)
{
    const uint32_t h = hashU32(a ^ hashU32(b + 0x9e3779b9U) ^ hashU32(c + 0x85ebca6bU));
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);  // top 24 bits → [0,1)
}

/// Smooth 2D value noise in [0, 1). Lattice corner values come from hash01 and
/// are blended with a quintic (smootherstep) fade for C² continuity — so the
/// summed octaves give natural rolling relief rather than the regular
/// corrugation a sin·cos product would produce. Negative lattice coordinates
/// wrap through the cast, which is fine (the hash only needs determinism).
float valueNoise(float x, float z, uint32_t seed)
{
    const float fx = std::floor(x);
    const float fz = std::floor(z);
    const int ix = static_cast<int>(fx);
    const int iz = static_cast<int>(fz);
    const float tx = x - fx;
    const float tz = z - fz;
    const float ux = tx * tx * tx * (tx * (tx * 6.0f - 15.0f) + 10.0f);
    const float uz = tz * tz * tz * (tz * (tz * 6.0f - 15.0f) + 10.0f);

    const auto corner = [&](int cx, int cz) {
        return hash01(seed, static_cast<uint32_t>(cx), static_cast<uint32_t>(cz));
    };
    const float v00 = corner(ix, iz);
    const float v10 = corner(ix + 1, iz);
    const float v01 = corner(ix, iz + 1);
    const float v11 = corner(ix + 1, iz + 1);

    const float a = v00 + (v10 - v00) * ux;
    const float b = v01 + (v11 - v01) * ux;
    return a + (b - a) * uz;  // [0,1)
}

}  // namespace

float meadowHeight01(float nx, float nz, const MeadowShape& shape)
{
    float h = shape.baseHeight01;
    // fbm: sum octaves of value noise. Each octave gets its own decorrelated
    // seed; freq scales the lattice (a few cells across the meadow) and amp its
    // contribution, centred to [-amp, amp].
    uint32_t octSeed = 0x9e3779b9U;
    for (const auto& oct : shape.octaves)
    {
        const float n = valueNoise(nx * oct.freq, nz * oct.freq, octSeed);
        h += oct.amp * (n * 2.0f - 1.0f);
        octSeed = hashU32(octSeed);
    }

    // Carve a smooth radial bowl at the pond centre so the floor sits below the
    // water line. falloff is 1 at the centre and eases to 0 at the rim
    // (1 − smoothstep), leaving the surrounding hills untouched.
    if (shape.pondRadiusGrid > 0.0f)
    {
        const float dx = nx - shape.pondCenterGrid.x;
        const float dz = nz - shape.pondCenterGrid.y;
        const float dist = std::sqrt(dx * dx + dz * dz);
        if (dist < shape.pondRadiusGrid)
        {
            const float t = dist / shape.pondRadiusGrid;      // 0 centre → 1 rim
            const float falloff = 1.0f - t * t * (3.0f - 2.0f * t);  // 1 centre → 0 rim
            h -= shape.bowlDepth01 * falloff;
        }
    }

    return std::clamp(h, 0.0f, 1.0f);
}

std::vector<ScatterPoint> scatterProps(uint32_t seed, const ScatterParams& params)
{
    std::vector<ScatterPoint> out;

    const float regionW = params.regionMax.x - params.regionMin.x;
    const float regionD = params.regionMax.y - params.regionMin.y;
    if (params.cellSize <= 0.0f || regionW <= 0.0f || regionD <= 0.0f)
    {
        return out;
    }

    const int cellsX = std::max(1, static_cast<int>(std::floor(regionW / params.cellSize)));
    const int cellsZ = std::max(1, static_cast<int>(std::floor(regionD / params.cellSize)));
    const float exclR2 = params.exclusionRadius * params.exclusionRadius;
    const float minD2 = params.minDist * params.minDist;

    out.reserve(static_cast<size_t>(cellsX) * static_cast<size_t>(cellsZ));

    for (int cz = 0; cz < cellsZ; ++cz)
    {
        for (int cx = 0; cx < cellsX; ++cx)
        {
            const auto ucx = static_cast<uint32_t>(cx);
            const auto ucz = static_cast<uint32_t>(cz);

            // Jitter the cell centre. Distinct key salts keep the four random
            // draws (x, z, yaw, scale) independent for a given cell.
            const float jx = hash01(seed, ucx, ucz * 2u + 1u);
            const float jz = hash01(seed + 0x1000193u, ucx, ucz);
            const float baseX = params.regionMin.x + (static_cast<float>(cx) + 0.5f) * params.cellSize;
            const float baseZ = params.regionMin.y + (static_cast<float>(cz) + 0.5f) * params.cellSize;
            const float x = baseX + (jx - 0.5f) * params.jitter * params.cellSize;
            const float z = baseZ + (jz - 0.5f) * params.jitter * params.cellSize;

            // Keep points inside the requested region (jitter can push out).
            if (x < params.regionMin.x || x > params.regionMax.x ||
                z < params.regionMin.y || z > params.regionMax.y)
            {
                continue;
            }

            // Reject inside the exclusion disc (the pond).
            if (params.exclusionRadius > 0.0f)
            {
                const float ex = x - params.exclusionCenter.x;
                const float ez = z - params.exclusionCenter.y;
                if (ex * ex + ez * ez < exclR2)
                {
                    continue;
                }
            }

            // Min-distance reject against accepted points. O(n²), but a scatter
            // region holds at most a few hundred props, so this stays cheap.
            if (minD2 > 0.0f)
            {
                bool tooClose = false;
                for (const ScatterPoint& q : out)
                {
                    const float dx = q.x - x;
                    const float dz = q.z - z;
                    if (dx * dx + dz * dz < minD2)
                    {
                        tooClose = true;
                        break;
                    }
                }
                if (tooClose)
                {
                    continue;
                }
            }

            ScatterPoint p;
            p.x = x;
            p.z = z;
            p.yawDeg = hash01(seed, ucx * 3u + 7u, ucz) * 360.0f;
            p.scale = params.minScale +
                      hash01(seed, ucx, ucz * 5u + 3u) * (params.maxScale - params.minScale);
            out.push_back(p);
        }
    }

    return out;
}

}  // namespace Vestige
