// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file update_version.h
/// @brief Release version parsing and ordering for the self-updater (3D_E-0729).
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace Vestige::Update
{

/// @brief A release version as this project tags it: `X.Y.Z` or `X.Y.Z-pre`.
struct Version
{
    std::array<int, 3> core{0, 0, 0};
    std::string pre;  ///< Empty for a final release, e.g. "rc.1" for an RC.
};

/// @brief Parses "0.1.76", "v0.1.76" or "0.1.76-rc.1". Anything else is empty.
std::optional<Version> parseVersion(std::string_view text);

/// @brief True when @a offered is a later release than @a installed.
///
/// SemVer ordering for the two shapes this project tags: the core compares
/// numerically, and at an equal core a pre-release is lower than the release.
bool isNewer(const Version& offered, const Version& installed);

/// @brief "0.1.76" or "0.1.76-rc.1".
std::string toString(const Version& version);

}  // namespace Vestige::Update
