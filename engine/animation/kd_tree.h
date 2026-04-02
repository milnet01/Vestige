/// @file kd_tree.h
/// @brief KD-tree for fast nearest-neighbor search in feature space.
#pragma once

#include <cstdint>
#include <vector>

namespace Vestige
{

/// @brief Result of a KD-tree nearest-neighbor query.
struct KDSearchResult
{
    int frameIndex = -1;    ///< Index of the nearest frame
    float cost = 1e30f;     ///< Squared distance to the nearest frame
};

/// @brief KD-tree for nearest-neighbor search over feature vectors.
///
/// Built offline from a normalized feature matrix. Supports weighted distance
/// by pre-scaling features by sqrt(weight) before construction.
///
/// Leaf nodes contain up to MAX_LEAF_SIZE frames. Splits on the dimension
/// with highest variance using the median value.
class KDTree
{
public:
    /// @brief Maximum frames per leaf node.
    static constexpr int MAX_LEAF_SIZE = 32;

    /// @brief Builds the KD-tree from a feature matrix.
    /// @param features Row-major feature matrix (numFrames × numFeatures).
    ///                 Must remain valid for the lifetime of the tree.
    /// @param numFrames Number of frames (rows).
    /// @param numFeatures Number of feature dimensions (columns).
    void build(const float* features, int numFrames, int numFeatures);

    /// @brief Finds the nearest neighbor to the query vector.
    /// @param query Feature vector (numFeatures floats).
    /// @param tagMask If non-zero, only frames whose tag ANDs with this mask
    ///               are considered. Pass 0 to search all frames.
    /// @param frameTags Per-frame tag bitmask array (same size as numFrames).
    ///                  May be nullptr if tagMask is 0.
    /// @return The frame index and squared distance of the best match.
    KDSearchResult findNearest(const float* query, uint32_t tagMask = 0,
                               const uint32_t* frameTags = nullptr) const;

    /// @brief Brute-force search (fallback for small databases).
    KDSearchResult bruteForceSearch(const float* query, uint32_t tagMask = 0,
                                    const uint32_t* frameTags = nullptr) const;

    /// @brief Whether the tree has been built.
    bool isBuilt() const;

    /// @brief Gets the number of frames in the tree.
    int getFrameCount() const;

private:
    struct Node
    {
        int splitDimension = -1;  ///< -1 = leaf node
        float splitValue = 0.0f;
        int leftChild = -1;       ///< Index into m_nodes
        int rightChild = -1;

        // Leaf data
        int frameStart = 0;       ///< Start index in m_indices
        int frameCount = 0;       ///< Number of frames in this leaf
    };

    void buildRecursive(int nodeIndex, int start, int count, int depth);
    void searchRecursive(int nodeIndex, const float* query,
                         KDSearchResult& best, uint32_t tagMask,
                         const uint32_t* frameTags) const;
    float computeDistance(const float* a, const float* b) const;
    int findHighestVarianceDimension(int start, int count) const;

    const float* m_features = nullptr;
    int m_numFrames = 0;
    int m_numFeatures = 0;
    bool m_built = false;

    std::vector<Node> m_nodes;
    std::vector<int> m_indices;  ///< Permuted frame indices
};

} // namespace Vestige
