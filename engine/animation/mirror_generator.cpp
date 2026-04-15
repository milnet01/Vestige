// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file mirror_generator.cpp
/// @brief Animation mirroring utilities implementation.

#include "animation/mirror_generator.h"

#include <algorithm>
#include <cctype>

namespace Vestige
{

/// @brief Replaces "Left" with "Right" (or vice versa) in a bone name.
static std::string swapSide(const std::string& name)
{
    std::string result = name;

    // Try various naming conventions
    auto tryReplace = [&](const std::string& from, const std::string& to) -> bool
    {
        size_t pos = result.find(from);
        if (pos != std::string::npos)
        {
            result.replace(pos, from.length(), to);
            return true;
        }
        return false;
    };

    // PascalCase: "Left" <-> "Right"
    if (tryReplace("Left", "Right")) return result;
    if (tryReplace("Right", "Left")) return result;

    // lowercase: "left" <-> "right"
    if (tryReplace("left", "right")) return result;
    if (tryReplace("right", "left")) return result;

    // Prefix: "L_" <-> "R_"
    if (tryReplace("L_", "R_")) return result;
    if (tryReplace("R_", "L_")) return result;

    // Suffix: "_L" <-> "_R"
    if (tryReplace("_L", "_R")) return result;
    if (tryReplace("_R", "_L")) return result;

    // Single letter prefix: "l" <-> "r" (only if followed by uppercase)
    if (result.size() >= 2 && result[0] == 'l' && std::isupper(result[1]))
    {
        result[0] = 'r';
        return result;
    }
    if (result.size() >= 2 && result[0] == 'r' && std::isupper(result[1]))
    {
        result[0] = 'l';
        return result;
    }

    return ""; // No side indicator found
}

MirrorConfig MirrorGenerator::autoDetect(const Skeleton& skeleton)
{
    MirrorConfig config;

    int jointCount = skeleton.getJointCount();
    std::vector<bool> paired(static_cast<size_t>(jointCount), false);

    for (int i = 0; i < jointCount; ++i)
    {
        if (paired[static_cast<size_t>(i)])
            continue;

        const std::string& name = skeleton.m_joints[static_cast<size_t>(i)].name;
        std::string mirrorName = swapSide(name);

        if (mirrorName.empty() || mirrorName == name)
            continue;

        // Find the mirror bone
        int mirrorIdx = skeleton.findJoint(mirrorName);
        if (mirrorIdx >= 0 && mirrorIdx != i && !paired[static_cast<size_t>(mirrorIdx)])
        {
            config.bonePairs.push_back({i, mirrorIdx});
            paired[static_cast<size_t>(i)] = true;
            paired[static_cast<size_t>(mirrorIdx)] = true;
        }
    }

    return config;
}

MirrorConfig MirrorGenerator::fromNames(
    const Skeleton& skeleton,
    const std::vector<std::pair<std::string, std::string>>& namePairs)
{
    MirrorConfig config;

    for (const auto& pair : namePairs)
    {
        int left = skeleton.findJoint(pair.first);
        int right = skeleton.findJoint(pair.second);

        if (left >= 0 && right >= 0)
        {
            config.bonePairs.push_back({left, right});
        }
    }

    return config;
}

} // namespace Vestige
