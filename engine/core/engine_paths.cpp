// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file engine_paths.cpp
/// @brief Implementation of path-composition helpers. See header
///        for contract.

#include "core/engine_paths.h"

namespace Vestige
{

namespace
{

/// Strip a single trailing `/` so `"assets/" + "/captions.json"`
/// doesn't produce `"assets//captions.json"` (an OS-tolerant quirk
/// that nonetheless breaks path-equality in any string-keyed
/// cache).
///
/// Intentionally POSIX-separator-only: the filename fragments we
/// append here (`/captions.json` and any future siblings) use `/`
/// exclusively, so a Windows asset root ending in `\\` would
/// produce `"C:\\Game\\assets\\/captions.json"` — ugly but
/// Windows-tolerated. If we ever append platform-native fragments,
/// revisit.
std::string stripTrailingSlash(const std::string& path)
{
    if (!path.empty() && path.back() == '/')
    {
        return path.substr(0, path.size() - 1);
    }
    return path;
}

} // namespace

std::string captionMapPath(const std::string& assetPath)
{
    const std::string root = stripTrailingSlash(assetPath);
    if (root.empty())
    {
        return "captions.json";
    }
    return root + "/captions.json";
}

} // namespace Vestige
