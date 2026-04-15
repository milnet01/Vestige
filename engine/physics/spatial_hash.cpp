// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file spatial_hash.cpp
/// @brief Spatial hash implementation using counting-sort.
#include "physics/spatial_hash.h"

#include <algorithm>
#include <cmath>

namespace Vestige
{

// Teschner 2003 primes
static constexpr uint32_t HASH_P1 = 73856093u;
static constexpr uint32_t HASH_P2 = 19349663u;
static constexpr uint32_t HASH_P3 = 83492791u;

// ---------------------------------------------------------------------------
// Build (counting-sort, O(N))
// ---------------------------------------------------------------------------

void SpatialHash::build(const glm::vec3* positions, size_t count, float cellSize)
{
    m_positions = positions;
    m_count = count;
    m_cellSize = std::max(cellSize, 1e-6f);
    m_invCellSize = 1.0f / m_cellSize;

    // Use a prime table size >= 2× count to reduce hash collisions.
    // Small primes table for common sizes; fall back to 2*count+1 for large inputs.
    static constexpr size_t PRIMES[] = {
        7, 13, 29, 53, 97, 193, 389, 769, 1543, 3079, 6151, 12289, 24593
    };
    size_t target = std::max(count * 2, size_t(7));
    m_tableSize = target;
    for (size_t p : PRIMES)
    {
        if (p >= target)
        {
            m_tableSize = p;
            break;
        }
    }
    if (m_tableSize < target)
    {
        m_tableSize = target | 1;  // ensure odd
    }

    // Step 1: Count particles per cell
    m_cellStart.assign(m_tableSize + 1, 0);
    m_entries.resize(count);

    for (size_t i = 0; i < count; ++i)
    {
        glm::ivec3 cell = cellCoord(positions[i]);
        size_t h = hashCell(cell.x, cell.y, cell.z);
        m_cellStart[h]++;
    }

    // Step 2: Prefix sum (exclusive) — cellStart[h] = start offset for cell h
    uint32_t sum = 0;
    for (size_t i = 0; i < m_tableSize; ++i)
    {
        uint32_t count_i = m_cellStart[i];
        m_cellStart[i] = sum;
        sum += count_i;
    }
    m_cellStart[m_tableSize] = sum;  // sentinel

    // Step 3: Scatter particle IDs into entries array
    // We use a working copy of cellStart as write cursors
    std::vector<uint32_t> cursor(m_cellStart.begin(), m_cellStart.begin() +
                                 static_cast<ptrdiff_t>(m_tableSize));

    for (size_t i = 0; i < count; ++i)
    {
        glm::ivec3 cell = cellCoord(positions[i]);
        size_t h = hashCell(cell.x, cell.y, cell.z);
        m_entries[cursor[h]] = static_cast<uint32_t>(i);
        cursor[h]++;
    }
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

void SpatialHash::query(const glm::vec3& position, float radius, uint32_t selfIndex,
                         std::vector<uint32_t>& result) const
{
    if (m_count == 0)
    {
        return;
    }

    // Compute cell range to search (neighborhood around query cell)
    glm::ivec3 minCell = cellCoord(position - glm::vec3(radius));
    glm::ivec3 maxCell = cellCoord(position + glm::vec3(radius));

    float radiusSq = radius * radius;

    // Track which hash buckets we've already visited to avoid duplicates
    // from different cell coordinates hashing to the same bucket.
    // Use a dynamic vector instead of a fixed-size array so that
    // large-radius queries spanning many cells cannot overflow.
    std::vector<size_t> visitedBuckets;
    visitedBuckets.reserve(64);  // Pre-allocate for typical case

    for (int cz = minCell.z; cz <= maxCell.z; ++cz)
    {
        for (int cy = minCell.y; cy <= maxCell.y; ++cy)
        {
            for (int cx = minCell.x; cx <= maxCell.x; ++cx)
            {
                size_t h = hashCell(cx, cy, cz);

                // Skip if we've already processed this bucket
                bool alreadyVisited = false;
                for (size_t v = 0; v < visitedBuckets.size(); ++v)
                {
                    if (visitedBuckets[v] == h)
                    {
                        alreadyVisited = true;
                        break;
                    }
                }
                if (alreadyVisited)
                {
                    continue;
                }
                visitedBuckets.push_back(h);

                uint32_t start = m_cellStart[h];
                uint32_t end = m_cellStart[h + 1];

                for (uint32_t e = start; e < end; ++e)
                {
                    uint32_t idx = m_entries[e];
                    if (idx == selfIndex)
                    {
                        continue;
                    }

                    glm::vec3 diff = m_positions[idx] - position;
                    float distSq = glm::dot(diff, diff);
                    if (distSq < radiusSq)
                    {
                        result.push_back(idx);
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

float SpatialHash::getCellSize() const
{
    return m_cellSize;
}

size_t SpatialHash::getEntryCount() const
{
    return m_count;
}

size_t SpatialHash::hashCell(int cx, int cy, int cz) const
{
    // Teschner 2003 hash function
    uint32_t h = (static_cast<uint32_t>(cx) * HASH_P1) ^
                 (static_cast<uint32_t>(cy) * HASH_P2) ^
                 (static_cast<uint32_t>(cz) * HASH_P3);
    return static_cast<size_t>(h) % m_tableSize;
}

glm::ivec3 SpatialHash::cellCoord(const glm::vec3& pos) const
{
    return glm::ivec3(
        static_cast<int>(std::floor(pos.x * m_invCellSize)),
        static_cast<int>(std::floor(pos.y * m_invCellSize)),
        static_cast<int>(std::floor(pos.z * m_invCellSize))
    );
}

} // namespace Vestige
