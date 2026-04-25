// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file motion_preprocessor.h
/// @brief Offline motion database building pipeline.
#pragma once

#include "experimental/animation/motion_database.h"
#include "experimental/animation/feature_vector.h"

#include <memory>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief Configuration for motion database preprocessing.
struct MotionPreprocessConfig
{
    float sampleRate = 30.0f;          ///< Frames per second for sampling
    bool enableMirroring = false;      ///< Double database with mirrored frames
    std::vector<std::pair<int, int>> mirrorBonePairs; ///< Left/right bone pairs
};

/// @brief Offline pipeline for building a MotionDatabase from animation clips.
///
/// Handles clip loading, feature extraction, normalization, optional mirroring,
/// and KD-tree construction. Produces a ready-to-use MotionDatabase.
class MotionPreprocessor
{
public:
    /// @brief Adds an animation clip to the build queue.
    /// @param clip The animation clip.
    /// @param defaultTags Default tag mask for all frames in this clip.
    void addClip(std::shared_ptr<AnimationClip> clip, uint32_t defaultTags = 0);

    /// @brief Builds the motion database.
    /// @param schema Feature extraction schema.
    /// @param skeleton Character skeleton.
    /// @param config Preprocessing configuration.
    /// @return Built motion database.
    std::shared_ptr<MotionDatabase> build(const FeatureSchema& schema,
                                          const Skeleton& skeleton,
                                          const MotionPreprocessConfig& config);

    /// @brief Gets the number of clips added.
    int getClipCount() const;

    /// @brief Clears all added clips.
    void clear();

private:
    std::vector<AnimClipEntry> m_clips;
};

} // namespace Vestige
