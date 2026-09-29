// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file update_release.h
/// @brief GitHub release parsing, download choice and "what changed" notes
///        for the self-updater (3D_E-0729).
#pragma once

#include "update/update_installer.h"
#include "update/update_version.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Vestige::Update
{

/// @brief One file attached to a release.
struct ReleaseAsset
{
    std::string name;
    std::string url;  ///< browser_download_url
    std::uint64_t size = 0;
};

/// @brief The fields of a GitHub release the updater reads.
struct ReleaseInfo
{
    std::string tag;  ///< tag_name, e.g. "v0.1.76"
    Version version;
    std::string pageUrl;  ///< html_url
    std::vector<ReleaseAsset> assets;
};

/// @brief Parses the body of GET /repos/{owner}/{repo}/releases/latest.
///        Empty when the JSON is malformed or the tag is not a version.
std::optional<ReleaseInfo> parseLatest(std::string_view json);

/// @brief The download for an install kind plus its signature file.
struct SelectedAsset
{
    ReleaseAsset asset;
    ReleaseAsset signature;  ///< "<asset name>.sig"
};

/// @brief The file name suffix a release asset carries for @a kind; empty for None.
std::string_view assetSuffix(InstallKind kind);

/// @brief Picks the asset for @a kind: exactly one name ending in its suffix,
///        exactly one "<name>.sig" beside it, and both served over https.
///        Empty otherwise, and then nothing is offered.
std::optional<SelectedAsset> selectAsset(const ReleaseInfo& release, InstallKind kind);

/// @brief The lines of @a offered that @a installed does not contain, in
///        @a offered's order.
///
/// Both are CHANGELOG.md texts. Lines are matched as a multiset, so a line
/// added again is shown as many times as it was added. This is what the
/// update dialog shows as "what changed" (spec §4.3, INV-6).
std::vector<std::string> notesSince(std::string_view installed, std::string_view offered);

}  // namespace Vestige::Update
