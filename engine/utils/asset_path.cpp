// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file asset_path.cpp
#include "utils/asset_path.h"

namespace fs = std::filesystem;

namespace Vestige::AssetPath
{

namespace
{

/// The folder name scenes saved before the rule put in front of every path,
/// when assets were opened relative to the repository root.
constexpr const char* kLegacyPrefix = "assets";

} // namespace

std::string toKey(const fs::path& root, const std::string& path)
{
    if (root.empty() || path.empty())
    {
        return path;
    }

    const fs::path normal = fs::path(path).lexically_normal();
    if (normal.is_absolute())
    {
        const fs::path below = normal.lexically_relative(root.lexically_normal());
        if (below.empty() || below == "." || *below.begin() == "..")
        {
            return path;  // Outside the root: kept as written (spec § 4.4).
        }
        return below.generic_string();
    }

    // A traversal is left as it is, for the sandbox to refuse.
    if (normal.empty() || *normal.begin() == "..")
    {
        return path;
    }

    std::error_code ec;
    if (*normal.begin() == kLegacyPrefix && !fs::exists(root / kLegacyPrefix, ec))
    {
        const fs::path stripped = normal.lexically_relative(kLegacyPrefix);
        if (!stripped.empty() && stripped != ".")
        {
            return stripped.generic_string();
        }
    }
    return normal.generic_string();
}

fs::path resolve(const fs::path& root, const std::string& path)
{
    fs::path given(path);
    if (given.is_absolute() || root.empty() || path.empty())
    {
        return given;
    }
    return (root / toKey(root, path)).lexically_normal();
}

} // namespace Vestige::AssetPath
