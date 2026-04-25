// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file motion_database.h
/// @brief Motion matching database — stores feature vectors and supports search.
#pragma once

#include "experimental/animation/feature_vector.h"
#include "experimental/animation/kd_tree.h"
#include "animation/animation_clip.h"
#include "animation/skeleton.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief Information about a single frame in the motion database.
struct FrameInfo
{
    int clipIndex = -1;        ///< Which animation clip this frame belongs to
    float clipTime = 0.0f;     ///< Time within that clip
    uint32_t tags = 0;         ///< Bitmask of annotation tags
    bool mirrored = false;     ///< Whether this is a mirrored frame
};

/// @brief Common tag bits for frame annotation.
namespace MotionTags
{
    constexpr uint32_t LOCOMOTION = 0x01;
    constexpr uint32_t IDLE       = 0x02;
    constexpr uint32_t TURNING    = 0x04;
    constexpr uint32_t STOPPING   = 0x08;
    constexpr uint32_t STARTING   = 0x10;
    constexpr uint32_t JUMPING    = 0x20;
    constexpr uint32_t NO_ENTRY   = 0x80; ///< Do not transition TO this frame
}

/// @brief An animation clip entry with optional per-frame tags.
struct AnimClipEntry
{
    std::shared_ptr<AnimationClip> clip;
    uint32_t defaultTags = 0;  ///< Applied to all frames unless overridden
};

/// @brief Result of a motion database search.
struct MotionSearchResult
{
    int frameIndex = -1;       ///< Index in the database
    float cost = 1e30f;        ///< Squared distance cost
    int clipIndex = -1;        ///< Source clip index
    float clipTime = 0.0f;     ///< Time within source clip
    bool mirrored = false;     ///< Whether the frame is mirrored
};

/// @brief Preprocessed motion matching database.
///
/// Stores a feature matrix (N frames × M features), normalization parameters,
/// per-frame metadata, and a KD-tree for fast search. Built offline from
/// animation clips using a FeatureSchema.
class MotionDatabase
{
public:
    /// @brief Builds the database from animation clips.
    /// @param schema Feature schema defining extraction.
    /// @param clips Animation clips to include.
    /// @param skeleton The character skeleton.
    /// @param sampleRate Frames per second for sampling (default 30).
    void build(const FeatureSchema& schema,
               const std::vector<AnimClipEntry>& clips,
               const Skeleton& skeleton,
               float sampleRate = 30.0f);

    /// @brief Searches for the best matching frame.
    /// @param query Unnormalized feature vector (will be normalized internally).
    /// @param tagMask If non-zero, only frames matching this tag mask are considered.
    /// @param excludeFrame Frame index to exclude (current playback, -1 to skip).
    /// @param excludeRadius Frames within this range of excludeFrame are also excluded.
    /// @return Best matching frame info.
    MotionSearchResult search(const float* query, uint32_t tagMask = 0,
                              int excludeFrame = -1, int excludeRadius = 0) const;

    /// @brief Normalizes a query vector in-place using the database's mean/stddev.
    void normalizeQuery(float* query) const;

    /// @brief Gets the number of frames in the database.
    int getFrameCount() const;

    /// @brief Gets the number of feature dimensions.
    int getFeatureCount() const;

    /// @brief Gets frame info for a given frame index.
    const FrameInfo& getFrameInfo(int frameIndex) const;

    /// @brief Gets the skeleton pose data for a frame.
    const SkeletonPose& getPose(int frameIndex) const;

    /// @brief Gets the feature schema.
    const FeatureSchema& getSchema() const;

    /// @brief Gets the normalizer.
    const FeatureNormalizer& getNormalizer() const;

    /// @brief Whether the database has been built.
    bool isBuilt() const;

    /// @brief Adds mirrored frames to the database (doubles frame count).
    /// @param leftRightPairs Bone index pairs (left, right) for mirroring.
    void addMirroredFrames(const std::vector<std::pair<int, int>>& leftRightPairs);

private:
    void sampleClip(int clipIdx, const AnimClipEntry& entry,
                    const Skeleton& skeleton, float sampleRate);
    void extractFeatures();
    void buildSearchStructure();

    FeatureSchema m_schema;
    int m_numFeatures = 0;

    // Per-frame data
    std::vector<float> m_features;       ///< Row-major: numFrames × numFeatures
    std::vector<FrameInfo> m_frameInfo;
    std::vector<SkeletonPose> m_poses;

    // Normalization
    FeatureNormalizer m_normalizer;

    // Search acceleration
    KDTree m_kdTree;
    bool m_built = false;

    // Clips (kept for reference)
    std::vector<AnimClipEntry> m_clips;
    float m_sampleRate = 30.0f;
};

} // namespace Vestige
