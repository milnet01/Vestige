// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_constraint_graph.cpp
/// @brief Implementation of cloth constraint generation + greedy graph colouring.

#include "physics/cloth_constraint_graph.h"

#include <algorithm>
#include <cassert>

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

} // namespace Vestige
