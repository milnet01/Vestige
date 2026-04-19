// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_constraint_graph.cpp
/// @brief Implementation of cloth constraint generation + greedy graph colouring.

#include "physics/cloth_constraint_graph.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace Vestige
{

namespace
{
inline GpuConstraint makeConstraint(uint32_t i0, uint32_t i1,
                                     const std::vector<glm::vec3>& positions,
                                     float compliance)
{
    GpuConstraint c{};
    c.i0 = i0;
    c.i1 = i1;
    c.restLength = glm::length(positions[i0] - positions[i1]);
    c.compliance = compliance;
    return c;
}
} // namespace

void generateGridConstraints(
    uint32_t gridW, uint32_t gridH,
    const std::vector<glm::vec3>& positions,
    float stretchCompliance,
    float shearCompliance,
    float bendCompliance,
    std::vector<GpuConstraint>& outConstraints)
{
    if (gridW < 2 || gridH < 2) return;

    // Pre-reserve to avoid repeated reallocations on large grids. Bend term
    // is non-negative because we already early-out on grids smaller than 2x2.
    const uint32_t stretchCount = 2 * gridW * gridH - gridW - gridH;
    const uint32_t shearCount   = 2 * (gridW - 1) * (gridH - 1);
    const uint32_t bendCount    = (gridW >= 3 ? (gridW - 2) * gridH : 0)
                                + (gridH >= 3 ? gridW * (gridH - 2) : 0);
    outConstraints.reserve(outConstraints.size()
                            + stretchCount + shearCount + bendCount);

    for (uint32_t z = 0; z < gridH; ++z)
    {
        for (uint32_t x = 0; x < gridW; ++x)
        {
            const uint32_t idx = z * gridW + x;

            // Structural — right neighbour
            if (x + 1 < gridW)
            {
                outConstraints.push_back(
                    makeConstraint(idx, idx + 1, positions, stretchCompliance));
            }
            // Structural — down neighbour
            if (z + 1 < gridH)
            {
                outConstraints.push_back(
                    makeConstraint(idx, idx + gridW, positions, stretchCompliance));
            }
            // Shear — diagonal down-right
            if (x + 1 < gridW && z + 1 < gridH)
            {
                outConstraints.push_back(
                    makeConstraint(idx, idx + gridW + 1, positions, shearCompliance));
            }
            // Shear — diagonal down-left
            if (x > 0 && z + 1 < gridH)
            {
                outConstraints.push_back(
                    makeConstraint(idx, idx + gridW - 1, positions, shearCompliance));
            }
            // Bend — skip-one right
            if (x + 2 < gridW)
            {
                outConstraints.push_back(
                    makeConstraint(idx, idx + 2, positions, bendCompliance));
            }
            // Bend — skip-one down
            if (z + 2 < gridH)
            {
                outConstraints.push_back(
                    makeConstraint(idx, idx + 2 * gridW, positions, bendCompliance));
            }
        }
    }
}

std::vector<ColourRange> colourConstraints(
    std::vector<GpuConstraint>& constraints,
    uint32_t particleCount)
{
    if (constraints.empty()) return {};
    assert(particleCount > 0);

    // Per-particle "colours seen so far" bitset. 64 colours is far more than
    // a regular grid needs (4 typical) but cheap and handles irregular topology.
    std::vector<uint64_t> particleColours(particleCount, 0ULL);
    std::vector<uint32_t> assignedColour(constraints.size(), 0);
    uint32_t maxColour = 0;

    for (size_t i = 0; i < constraints.size(); ++i)
    {
        const auto& c = constraints[i];
        const uint64_t forbidden = particleColours[c.i0] | particleColours[c.i1];

        // Find the lowest bit not set in `forbidden`. ~forbidden flips the bits;
        // ffsll gives the lowest set bit (1-indexed). 0 means "all 64 used",
        // which would only happen on absurdly degenerate topologies.
        uint32_t colour = 0;
        for (; colour < 64; ++colour)
        {
            if ((forbidden & (1ULL << colour)) == 0) break;
        }
        assert(colour < 64 && "Constraint graph has a particle of degree >= 64");

        assignedColour[i] = colour;
        particleColours[c.i0] |= (1ULL << colour);
        particleColours[c.i1] |= (1ULL << colour);
        if (colour + 1 > maxColour) maxColour = colour + 1;
    }

    // Stable-sort constraints by their assigned colour. We re-derive the colour
    // for each comparison rather than carrying a parallel array through the
    // sort because std::stable_sort doesn't expose original indices.
    std::vector<size_t> indices(constraints.size());
    for (size_t i = 0; i < indices.size(); ++i) indices[i] = i;
    std::stable_sort(indices.begin(), indices.end(),
        [&](size_t a, size_t b) {
            return assignedColour[a] < assignedColour[b];
        });

    std::vector<GpuConstraint> reordered(constraints.size());
    for (size_t i = 0; i < indices.size(); ++i)
    {
        reordered[i] = constraints[indices[i]];
    }
    constraints = std::move(reordered);

    // Build per-colour ranges by re-walking the sorted assignedColour array
    // in the new order.
    std::vector<uint32_t> sortedColours(constraints.size());
    for (size_t i = 0; i < indices.size(); ++i)
    {
        sortedColours[i] = assignedColour[indices[i]];
    }

    std::vector<ColourRange> ranges(maxColour, {0, 0});
    uint32_t cursor = 0;
    for (uint32_t colour = 0; colour < maxColour; ++colour)
    {
        ranges[colour].offset = cursor;
        uint32_t count = 0;
        while (cursor < sortedColours.size() && sortedColours[cursor] == colour)
        {
            ++cursor;
            ++count;
        }
        ranges[colour].count = count;
    }
    return ranges;
}

// ---------------------------------------------------------------------------
// Dihedral constraints
// ---------------------------------------------------------------------------

namespace
{
inline uint64_t packEdgeKey(uint32_t a, uint32_t b)
{
    if (a > b) std::swap(a, b);
    return (static_cast<uint64_t>(a) << 32) | static_cast<uint64_t>(b);
}
} // namespace

void generateDihedralConstraints(
    const std::vector<uint32_t>& indices,
    const std::vector<glm::vec3>& positions,
    float compliance,
    std::vector<GpuDihedralConstraint>& outConstraints)
{
    if (indices.size() < 6) return;  // Need at least two triangles.

    struct EdgeInfo
    {
        uint32_t wing[2];
        int      count;
    };

    std::unordered_map<uint64_t, EdgeInfo> edgeMap;
    edgeMap.reserve(indices.size());

    for (size_t t = 0; t + 2 < indices.size(); t += 3)
    {
        const uint32_t tri[3] = {indices[t], indices[t + 1], indices[t + 2]};
        for (int e = 0; e < 3; ++e)
        {
            const uint32_t e0   = tri[e];
            const uint32_t e1   = tri[(e + 1) % 3];
            const uint32_t wing = tri[(e + 2) % 3];
            const uint64_t key  = packEdgeKey(e0, e1);

            auto it = edgeMap.find(key);
            if (it == edgeMap.end())
            {
                EdgeInfo info{};
                info.wing[0] = wing;
                info.count = 1;
                edgeMap[key] = info;
            }
            else if (it->second.count == 1)
            {
                it->second.wing[1] = wing;
                it->second.count = 2;
            }
            // count > 2: non-manifold edge — skip silently.
        }
    }

    for (const auto& [key, info] : edgeMap)
    {
        if (info.count != 2) continue;

        const uint32_t edgeV0 = static_cast<uint32_t>(key >> 32);
        const uint32_t edgeV1 = static_cast<uint32_t>(key & 0xFFFFFFFFu);

        // p0 = wing 0, p1 = wing 1, p2 = edge start, p3 = edge end (matches CPU
        // ClothSimulator's dihedral solver and the GLSL shader's binding).
        const glm::vec3& p0 = positions[info.wing[0]];
        const glm::vec3& p1 = positions[info.wing[1]];
        const glm::vec3& p2 = positions[edgeV0];
        const glm::vec3& p3 = positions[edgeV1];

        glm::vec3 n1 = glm::cross(p2 - p0, p3 - p0);
        glm::vec3 n2 = glm::cross(p3 - p1, p2 - p1);
        const float len1 = glm::length(n1);
        const float len2 = glm::length(n2);

        float restAngle = 0.0f;
        if (len1 > 1e-7f && len2 > 1e-7f)
        {
            n1 /= len1;
            n2 /= len2;
            const float cosAngle = std::clamp(glm::dot(n1, n2), -1.0f, 1.0f);
            restAngle = std::acos(cosAngle);
        }

        GpuDihedralConstraint c{};
        c.p0         = info.wing[0];
        c.p1         = info.wing[1];
        c.p2         = edgeV0;
        c.p3         = edgeV1;
        c.restAngle  = restAngle;
        c.compliance = compliance;
        outConstraints.push_back(c);
    }
}

std::vector<ColourRange> colourDihedralConstraints(
    std::vector<GpuDihedralConstraint>& constraints,
    uint32_t particleCount)
{
    if (constraints.empty()) return {};
    assert(particleCount > 0);

    std::vector<uint64_t> particleColours(particleCount, 0ULL);
    std::vector<uint32_t> assignedColour(constraints.size(), 0);
    uint32_t maxColour = 0;

    for (size_t i = 0; i < constraints.size(); ++i)
    {
        const auto& c = constraints[i];
        const uint64_t forbidden = particleColours[c.p0] | particleColours[c.p1]
                                  | particleColours[c.p2] | particleColours[c.p3];

        uint32_t colour = 0;
        for (; colour < 64; ++colour)
        {
            if ((forbidden & (1ULL << colour)) == 0) break;
        }
        assert(colour < 64 && "Dihedral graph has a particle of degree >= 64");

        assignedColour[i] = colour;
        const uint64_t bit = 1ULL << colour;
        particleColours[c.p0] |= bit;
        particleColours[c.p1] |= bit;
        particleColours[c.p2] |= bit;
        particleColours[c.p3] |= bit;
        if (colour + 1 > maxColour) maxColour = colour + 1;
    }

    std::vector<size_t> idx(constraints.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    std::stable_sort(idx.begin(), idx.end(),
        [&](size_t a, size_t b) {
            return assignedColour[a] < assignedColour[b];
        });

    std::vector<GpuDihedralConstraint> reordered(constraints.size());
    for (size_t i = 0; i < idx.size(); ++i) reordered[i] = constraints[idx[i]];
    constraints = std::move(reordered);

    std::vector<uint32_t> sortedColours(constraints.size());
    for (size_t i = 0; i < idx.size(); ++i)
        sortedColours[i] = assignedColour[idx[i]];

    std::vector<ColourRange> ranges(maxColour, {0, 0});
    uint32_t cursor = 0;
    for (uint32_t colour = 0; colour < maxColour; ++colour)
    {
        ranges[colour].offset = cursor;
        uint32_t count = 0;
        while (cursor < sortedColours.size() && sortedColours[cursor] == colour)
        {
            ++cursor;
            ++count;
        }
        ranges[colour].count = count;
    }
    return ranges;
}

// ---------------------------------------------------------------------------
// LRA constraints
// ---------------------------------------------------------------------------

void generateLraConstraints(
    const std::vector<glm::vec3>& positions,
    const std::vector<uint32_t>& pinIndices,
    std::vector<GpuLraConstraint>& outConstraints)
{
    if (pinIndices.empty()) return;

    // Mark which particles are pinned so we can skip them in the outer loop.
    std::vector<bool> isPinned(positions.size(), false);
    for (uint32_t p : pinIndices)
    {
        if (p < isPinned.size()) isPinned[p] = true;
    }

    outConstraints.reserve(outConstraints.size() + positions.size() - pinIndices.size());

    for (uint32_t i = 0; i < positions.size(); ++i)
    {
        if (isPinned[i]) continue;

        // Nearest pin (Euclidean). For typical hanging cloths the pin set is
        // a top edge (≤ W pins), so this O(P) loop per particle is cheap.
        float    bestDist2 = std::numeric_limits<float>::max();
        uint32_t bestPin   = pinIndices[0];
        for (uint32_t p : pinIndices)
        {
            const glm::vec3 diff = positions[i] - positions[p];
            const float     d2   = glm::dot(diff, diff);
            if (d2 < bestDist2)
            {
                bestDist2 = d2;
                bestPin   = p;
            }
        }

        GpuLraConstraint lra{};
        lra.particleIndex = i;
        lra.pinIndex      = bestPin;
        lra.maxDistance   = std::sqrt(bestDist2);
        outConstraints.push_back(lra);
    }
}

} // namespace Vestige
