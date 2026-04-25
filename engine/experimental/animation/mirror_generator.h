// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file mirror_generator.h
/// @brief Animation mirroring for motion matching database doubling.
#pragma once

#include "animation/skeleton.h"

#include <string>
#include <utility>
#include <vector>

namespace Vestige
{

/// @brief Configuration for animation mirroring.
struct MirrorConfig
{
    /// @brief Left/right bone pairs (indices into skeleton).
    std::vector<std::pair<int, int>> bonePairs;
};

/// @brief Utilities for generating mirrored bone pair mappings from skeletons.
///
/// Automatically detects left/right bone pairs by name convention
/// (e.g., "LeftFoot" <-> "RightFoot", "Left_Hand" <-> "Right_Hand").
class MirrorGenerator
{
public:
    /// @brief Automatically detects left/right bone pairs from joint names.
    /// @param skeleton The skeleton to analyze.
    /// @return Mirror configuration with detected bone pairs.
    static MirrorConfig autoDetect(const Skeleton& skeleton);

    /// @brief Creates a mirror config from explicit bone name pairs.
    /// @param skeleton The skeleton.
    /// @param namePairs Pairs of bone names (left, right).
    /// @return Mirror configuration.
    static MirrorConfig fromNames(const Skeleton& skeleton,
                                  const std::vector<std::pair<std::string, std::string>>& namePairs);
};

} // namespace Vestige
