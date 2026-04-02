/// @file kd_tree.cpp
/// @brief KD-tree nearest-neighbor search implementation.

#include "animation/kd_tree.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace Vestige
{

void KDTree::build(const float* features, int numFrames, int numFeatures)
{
    m_features = features;
    m_numFrames = numFrames;
    m_numFeatures = numFeatures;
    m_built = false;

    if (numFrames <= 0 || numFeatures <= 0 || !features)
        return;

    // Initialize indices [0, 1, 2, ..., numFrames-1]
    m_indices.resize(static_cast<size_t>(numFrames));
    std::iota(m_indices.begin(), m_indices.end(), 0);

    // Pre-allocate nodes (upper bound: 2*numFrames for a complete binary tree)
    m_nodes.clear();
    m_nodes.reserve(static_cast<size_t>(numFrames * 2));

    // Create root node
    m_nodes.push_back({});
    buildRecursive(0, 0, numFrames, 0);

    m_built = true;
}

void KDTree::buildRecursive(int nodeIndex, int start, int count, int depth)
{
    Node& node = m_nodes[static_cast<size_t>(nodeIndex)];

    // Leaf node if count is small enough
    if (count <= MAX_LEAF_SIZE)
    {
        node.splitDimension = -1;
        node.frameStart = start;
        node.frameCount = count;
        return;
    }

    // Find dimension with highest variance
    int splitDim = findHighestVarianceDimension(start, count);
    node.splitDimension = splitDim;

    // Sort indices by the split dimension value and find the median
    int mid = count / 2;
    std::nth_element(
        m_indices.begin() + start,
        m_indices.begin() + start + mid,
        m_indices.begin() + start + count,
        [this, splitDim](int a, int b)
        {
            return m_features[a * m_numFeatures + splitDim]
                 < m_features[b * m_numFeatures + splitDim];
        });

    int medianFrame = m_indices[static_cast<size_t>(start + mid)];
    node.splitValue = m_features[medianFrame * m_numFeatures + splitDim];

    // Create child nodes
    int leftIdx = static_cast<int>(m_nodes.size());
    m_nodes.push_back({});
    int rightIdx = static_cast<int>(m_nodes.size());
    m_nodes.push_back({});

    node.leftChild = leftIdx;
    node.rightChild = rightIdx;

    // Recurse
    buildRecursive(leftIdx, start, mid, depth + 1);
    buildRecursive(rightIdx, start + mid, count - mid, depth + 1);
}

int KDTree::findHighestVarianceDimension(int start, int count) const
{
    int bestDim = 0;
    float bestVariance = -1.0f;

    for (int d = 0; d < m_numFeatures; ++d)
    {
        // Compute variance for this dimension across the subset
        float sum = 0.0f;
        float sumSq = 0.0f;
        for (int i = start; i < start + count; ++i)
        {
            float val = m_features[m_indices[static_cast<size_t>(i)] * m_numFeatures + d];
            sum += val;
            sumSq += val * val;
        }
        float mean = sum / static_cast<float>(count);
        float variance = sumSq / static_cast<float>(count) - mean * mean;

        if (variance > bestVariance)
        {
            bestVariance = variance;
            bestDim = d;
        }
    }

    return bestDim;
}

KDSearchResult KDTree::findNearest(const float* query, uint32_t tagMask,
                                   const uint32_t* frameTags) const
{
    if (!m_built || m_numFrames <= 0)
        return {};

    // For small databases, brute force is faster
    if (m_numFrames < 5000)
        return bruteForceSearch(query, tagMask, frameTags);

    KDSearchResult best;
    searchRecursive(0, query, best, tagMask, frameTags);
    return best;
}

void KDTree::searchRecursive(int nodeIndex, const float* query,
                             KDSearchResult& best, uint32_t tagMask,
                             const uint32_t* frameTags) const
{
    const Node& node = m_nodes[static_cast<size_t>(nodeIndex)];

    // Leaf node: check all frames
    if (node.splitDimension == -1)
    {
        for (int i = node.frameStart; i < node.frameStart + node.frameCount; ++i)
        {
            int frameIdx = m_indices[static_cast<size_t>(i)];

            // Tag filtering
            if (tagMask != 0 && frameTags)
            {
                if ((frameTags[frameIdx] & tagMask) == 0)
                    continue;
            }

            float dist = computeDistance(query,
                &m_features[frameIdx * m_numFeatures]);

            if (dist < best.cost)
            {
                best.cost = dist;
                best.frameIndex = frameIdx;
            }
        }
        return;
    }

    // Internal node: decide which child to visit first
    float queryVal = query[node.splitDimension];
    float diff = queryVal - node.splitValue;

    int nearChild = (diff <= 0.0f) ? node.leftChild : node.rightChild;
    int farChild = (diff <= 0.0f) ? node.rightChild : node.leftChild;

    // Always search near child
    searchRecursive(nearChild, query, best, tagMask, frameTags);

    // Only search far child if the splitting plane is closer than current best
    if (diff * diff < best.cost)
    {
        searchRecursive(farChild, query, best, tagMask, frameTags);
    }
}

KDSearchResult KDTree::bruteForceSearch(const float* query, uint32_t tagMask,
                                        const uint32_t* frameTags) const
{
    KDSearchResult best;

    for (int i = 0; i < m_numFrames; ++i)
    {
        // Tag filtering
        if (tagMask != 0 && frameTags)
        {
            if ((frameTags[i] & tagMask) == 0)
                continue;
        }

        float dist = computeDistance(query, &m_features[i * m_numFeatures]);

        if (dist < best.cost)
        {
            best.cost = dist;
            best.frameIndex = i;
        }
    }

    return best;
}

float KDTree::computeDistance(const float* a, const float* b) const
{
    float dist = 0.0f;
    for (int d = 0; d < m_numFeatures; ++d)
    {
        float diff = a[d] - b[d];
        dist += diff * diff;
    }
    return dist;
}

bool KDTree::isBuilt() const
{
    return m_built;
}

int KDTree::getFrameCount() const
{
    return m_numFrames;
}

} // namespace Vestige
