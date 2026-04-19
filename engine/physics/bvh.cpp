// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file bvh.cpp
/// @brief AABB BVH construction (binned SAH), refit, and proximity queries.
#include "physics/bvh.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace Vestige
{

// ---------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------

void BVH::build(const glm::vec3* vertices, size_t vertexCount,
                const uint32_t* indices, size_t indexCount)
{
    m_nodes.clear();
    m_sortedTriIndices.clear();
    m_triCount = indexCount / 3;

    if (m_triCount == 0 || vertices == nullptr || indices == nullptr || vertexCount == 0)
    {
        return;
    }

    // Compute per-triangle centroid and AABB
    std::vector<TriInfo> tris(m_triCount);
    for (size_t t = 0; t < m_triCount; ++t)
    {
        const glm::vec3& a = vertices[indices[t * 3 + 0]];
        const glm::vec3& b = vertices[indices[t * 3 + 1]];
        const glm::vec3& c = vertices[indices[t * 3 + 2]];

        tris[t].centroid = (a + b + c) / 3.0f;
        tris[t].boundsMin = glm::min(glm::min(a, b), c);
        tris[t].boundsMax = glm::max(glm::max(a, b), c);
        tris[t].originalIndex = static_cast<uint32_t>(t);
    }

    // Reserve approximate node count (2N-1 for N leaves)
    m_nodes.reserve(m_triCount * 2);

    buildRecursive(0, static_cast<int>(m_triCount), tris);

    // Build sorted triangle index array from final TriInfo order
    m_sortedTriIndices.resize(m_triCount);
    for (size_t i = 0; i < m_triCount; ++i)
    {
        m_sortedTriIndices[i] = tris[i].originalIndex;
    }
}

int BVH::buildRecursive(int start, int end, std::vector<TriInfo>& tris)
{
    int nodeIdx = static_cast<int>(m_nodes.size());
    m_nodes.push_back({});

    // Compute bounds for this node
    glm::vec3 bMin(FLT_MAX);
    glm::vec3 bMax(-FLT_MAX);
    for (int i = start; i < end; ++i)
    {
        bMin = glm::min(bMin, tris[static_cast<size_t>(i)].boundsMin);
        bMax = glm::max(bMax, tris[static_cast<size_t>(i)].boundsMax);
    }
    m_nodes[static_cast<size_t>(nodeIdx)].boundsMin = bMin;
    m_nodes[static_cast<size_t>(nodeIdx)].boundsMax = bMax;

    int count = end - start;

    // Leaf node
    if (count <= MAX_LEAF_TRIS)
    {
        m_nodes[static_cast<size_t>(nodeIdx)].triStart = start;
        m_nodes[static_cast<size_t>(nodeIdx)].triCount = count;
        return nodeIdx;
    }

    // Find best split using binned SAH
    int bestAxis = 0;
    float bestPos = 0.0f;
    float bestCost = FLT_MAX;
    findBestSplit(start, end, tris, bMin, bMax, bestAxis, bestPos, bestCost);

    // Partition triangles
    auto mid = std::partition(
        tris.begin() + start, tris.begin() + end,
        [bestAxis, bestPos](const TriInfo& t)
        {
            return t.centroid[bestAxis] < bestPos;
        });
    int splitIdx = static_cast<int>(mid - tris.begin());

    // Fallback: if partition degenerates (all on one side), split in half
    if (splitIdx == start || splitIdx == end)
    {
        splitIdx = (start + end) / 2;
        std::nth_element(
            tris.begin() + start, tris.begin() + splitIdx, tris.begin() + end,
            [bestAxis](const TriInfo& a, const TriInfo& b)
            {
                return a.centroid[bestAxis] < b.centroid[bestAxis];
            });
    }

    // Recurse — children are allocated after parent, so they have higher indices
    int left = buildRecursive(start, splitIdx, tris);
    int right = buildRecursive(splitIdx, end, tris);

    m_nodes[static_cast<size_t>(nodeIdx)].leftChild = left;
    m_nodes[static_cast<size_t>(nodeIdx)].rightChild = right;
    m_nodes[static_cast<size_t>(nodeIdx)].triStart = 0;
    m_nodes[static_cast<size_t>(nodeIdx)].triCount = 0;

    return nodeIdx;
}

/*static*/ void BVH::findBestSplit(int start, int end, const std::vector<TriInfo>& tris,
                                    const glm::vec3& nodeMin, const glm::vec3& nodeMax,
                                    int& bestAxis, float& bestPos, float& bestCost)
{
    bestCost = FLT_MAX;
    bestAxis = 0;
    bestPos = 0.0f;

    struct Bin
    {
        glm::vec3 bMin = glm::vec3(FLT_MAX);
        glm::vec3 bMax = glm::vec3(-FLT_MAX);
        int count = 0;
    };

    for (int axis = 0; axis < 3; ++axis)
    {
        float axisMin = nodeMin[axis];
        float axisMax = nodeMax[axis];
        float extent = axisMax - axisMin;
        if (extent < 1e-6f)
        {
            continue;
        }

        float scale = static_cast<float>(NUM_BINS) / extent;

        Bin bins[NUM_BINS];
        for (int i = start; i < end; ++i)
        {
            int b = std::min(
                static_cast<int>((tris[static_cast<size_t>(i)].centroid[axis] - axisMin) * scale),
                NUM_BINS - 1);
            bins[b].count++;
            bins[b].bMin = glm::min(bins[b].bMin, tris[static_cast<size_t>(i)].boundsMin);
            bins[b].bMax = glm::max(bins[b].bMax, tris[static_cast<size_t>(i)].boundsMax);
        }

        // Sweep from left to compute prefix areas/counts
        float leftArea[NUM_BINS - 1];
        int leftCount[NUM_BINS - 1];
        glm::vec3 lMin(FLT_MAX), lMax(-FLT_MAX);
        int lCount = 0;
        for (int i = 0; i < NUM_BINS - 1; ++i)
        {
            lMin = glm::min(lMin, bins[i].bMin);
            lMax = glm::max(lMax, bins[i].bMax);
            lCount += bins[i].count;
            leftArea[i] = surfaceArea(lMin, lMax);
            leftCount[i] = lCount;
        }

        // Sweep from right
        float rightArea[NUM_BINS - 1];
        int rightCount[NUM_BINS - 1];
        glm::vec3 rMin(FLT_MAX), rMax(-FLT_MAX);
        int rCount = 0;
        for (int i = NUM_BINS - 1; i > 0; --i)
        {
            rMin = glm::min(rMin, bins[i].bMin);
            rMax = glm::max(rMax, bins[i].bMax);
            rCount += bins[i].count;
            rightArea[i - 1] = surfaceArea(rMin, rMax);
            rightCount[i - 1] = rCount;
        }

        // Evaluate SAH cost for each split plane
        for (int i = 0; i < NUM_BINS - 1; ++i)
        {
            if (leftCount[i] == 0 || rightCount[i] == 0)
            {
                continue;
            }
            float cost = leftArea[i] * static_cast<float>(leftCount[i]) +
                         rightArea[i] * static_cast<float>(rightCount[i]);
            if (cost < bestCost)
            {
                bestCost = cost;
                bestAxis = axis;
                bestPos = axisMin + static_cast<float>(i + 1) * extent /
                          static_cast<float>(NUM_BINS);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Refit
// ---------------------------------------------------------------------------

void BVH::refit(const glm::vec3* vertices, const uint32_t* indices)
{
    if (m_nodes.empty() || vertices == nullptr || indices == nullptr)
    {
        return;
    }

    // Bottom-up: children have higher indices than parents (top-down construction).
    // Process in reverse order so leaves are updated before their parents.
    for (int i = static_cast<int>(m_nodes.size()) - 1; i >= 0; --i)
    {
        auto& node = m_nodes[static_cast<size_t>(i)];
        if (node.isLeaf())
        {
            // Recompute AABB from triangle vertices
            node.boundsMin = glm::vec3(FLT_MAX);
            node.boundsMax = glm::vec3(-FLT_MAX);
            for (int t = node.triStart; t < node.triStart + node.triCount; ++t)
            {
                uint32_t triIdx = m_sortedTriIndices[static_cast<size_t>(t)];
                const glm::vec3& a = vertices[indices[triIdx * 3 + 0]];
                const glm::vec3& b = vertices[indices[triIdx * 3 + 1]];
                const glm::vec3& c = vertices[indices[triIdx * 3 + 2]];
                node.boundsMin = glm::min(node.boundsMin, glm::min(glm::min(a, b), c));
                node.boundsMax = glm::max(node.boundsMax, glm::max(glm::max(a, b), c));
            }
        }
        else
        {
            // Merge children
            auto& left = m_nodes[static_cast<size_t>(node.leftChild)];
            auto& right = m_nodes[static_cast<size_t>(node.rightChild)];
            node.boundsMin = glm::min(left.boundsMin, right.boundsMin);
            node.boundsMax = glm::max(left.boundsMax, right.boundsMax);
        }
    }
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

bool BVH::queryClosest(const glm::vec3& point, float maxDist,
                        const glm::vec3* vertices, const uint32_t* indices,
                        BVHQueryResult& result) const
{
    if (m_nodes.empty())
    {
        return false;
    }

    result.distance = maxDist;
    bool found = false;

    // Stack-based traversal (max depth ~64 is more than sufficient)
    int stack[64];
    int stackPtr = 0;
    stack[stackPtr++] = 0;  // root

    while (stackPtr > 0)
    {
        int nodeIdx = stack[--stackPtr];
        const Node& node = m_nodes[static_cast<size_t>(nodeIdx)];

        // Prune if AABB is too far
        if (distToAABB(point, node.boundsMin, node.boundsMax) > result.distance)
        {
            continue;
        }

        if (node.isLeaf())
        {
            // Test each triangle in leaf
            for (int t = node.triStart; t < node.triStart + node.triCount; ++t)
            {
                uint32_t triIdx = m_sortedTriIndices[static_cast<size_t>(t)];
                const glm::vec3& a = vertices[indices[triIdx * 3 + 0]];
                const glm::vec3& b = vertices[indices[triIdx * 3 + 1]];
                const glm::vec3& c = vertices[indices[triIdx * 3 + 2]];

                glm::vec3 closest = closestPointOnTriangle(point, a, b, c);
                glm::vec3 diff = point - closest;
                float dist = glm::length(diff);

                if (dist < result.distance)
                {
                    result.distance = dist;
                    result.closestPoint = closest;
                    result.triangleIndex = triIdx;

                    // Compute face normal
                    glm::vec3 edge1 = b - a;
                    glm::vec3 edge2 = c - a;
                    glm::vec3 faceNormal = glm::cross(edge1, edge2);
                    float normalLen = glm::length(faceNormal);
                    if (normalLen > 1e-7f)
                    {
                        faceNormal /= normalLen;
                        // Ensure normal points toward the query point
                        if (glm::dot(faceNormal, diff) < 0.0f)
                        {
                            faceNormal = -faceNormal;
                        }
                        result.normal = faceNormal;
                    }
                    else
                    {
                        result.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                    }
                    found = true;
                }
            }
        }
        else
        {
            // Push children — closer child last so it's popped first for better pruning
            float distL = distToAABB(point,
                m_nodes[static_cast<size_t>(node.leftChild)].boundsMin,
                m_nodes[static_cast<size_t>(node.leftChild)].boundsMax);
            float distR = distToAABB(point,
                m_nodes[static_cast<size_t>(node.rightChild)].boundsMin,
                m_nodes[static_cast<size_t>(node.rightChild)].boundsMax);

            if (distL < distR)
            {
                if (distR <= result.distance && stackPtr < 64)
                {
                    stack[stackPtr++] = node.rightChild;
                }
                if (distL <= result.distance && stackPtr < 64)
                {
                    stack[stackPtr++] = node.leftChild;
                }
            }
            else
            {
                if (distL <= result.distance && stackPtr < 64)
                {
                    stack[stackPtr++] = node.leftChild;
                }
                if (distR <= result.distance && stackPtr < 64)
                {
                    stack[stackPtr++] = node.rightChild;
                }
            }
        }
    }

    return found;
}

// ---------------------------------------------------------------------------
// Closest point on triangle (Ericson, Real-Time Collision Detection, Ch. 5)
// ---------------------------------------------------------------------------

glm::vec3 BVH::closestPointOnTriangle(const glm::vec3& p,
                                        const glm::vec3& a,
                                        const glm::vec3& b,
                                        const glm::vec3& c)
{
    glm::vec3 ab = b - a;
    glm::vec3 ac = c - a;
    glm::vec3 ap = p - a;

    float d1 = glm::dot(ab, ap);
    float d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f)
    {
        return a;  // Vertex region A
    }

    glm::vec3 bp = p - b;
    float d3 = glm::dot(ab, bp);
    float d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3)
    {
        return b;  // Vertex region B
    }

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
    {
        float v = d1 / (d1 - d3);
        return a + v * ab;  // Edge AB
    }

    glm::vec3 cp = p - c;
    float d5 = glm::dot(ab, cp);
    float d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6)
    {
        return c;  // Vertex region C
    }

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
    {
        float w = d2 / (d2 - d6);
        return a + w * ac;  // Edge AC
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
    {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return b + w * (c - b);  // Edge BC
    }

    // Inside triangle
    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    return a + ab * v + ac * w;
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

float BVH::surfaceArea(const glm::vec3& bMin, const glm::vec3& bMax)
{
    glm::vec3 d = bMax - bMin;
    // Clamp negative extents (degenerate/empty bounds)
    d = glm::max(d, glm::vec3(0.0f));
    return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
}

float BVH::distToAABB(const glm::vec3& point,
                       const glm::vec3& bMin, const glm::vec3& bMax)
{
    float dx = std::max({bMin.x - point.x, 0.0f, point.x - bMax.x});
    float dy = std::max({bMin.y - point.y, 0.0f, point.y - bMax.y});
    float dz = std::max({bMin.z - point.z, 0.0f, point.z - bMax.z});
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

size_t BVH::getNodeCount() const
{
    return m_nodes.size();
}

size_t BVH::getTriangleCount() const
{
    return m_triCount;
}

bool BVH::isBuilt() const
{
    return !m_nodes.empty();
}

} // namespace Vestige
