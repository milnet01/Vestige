// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file update_release.cpp
#include "update/update_release.h"

#include <nlohmann/json.hpp>

#include <unordered_map>

namespace Vestige::Update
{

namespace
{

bool endsWith(std::string_view s, std::string_view suffix)
{
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

bool isHttps(std::string_view url)
{
    return url.rfind("https://", 0) == 0;
}

/// Splits on '\n', dropping one trailing '\r' from each line so a file with
/// Windows line endings matches the same file with Unix ones.
std::vector<std::string_view> splitLines(std::string_view text)
{
    std::vector<std::string_view> lines;
    size_t start = 0;
    while (start < text.size())
    {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
        {
            end = text.size();
        }
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r')
        {
            line.remove_suffix(1);
        }
        lines.push_back(line);
        start = end + 1;
    }
    return lines;
}

}  // namespace

std::optional<ReleaseInfo> parseLatest(std::string_view json)
{
    const auto doc = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (!doc.is_object() || !doc.contains("tag_name") || !doc["tag_name"].is_string())
    {
        return std::nullopt;
    }
    ReleaseInfo release;
    release.tag = doc["tag_name"].get<std::string>();
    auto version = parseVersion(release.tag);
    if (!version)
    {
        return std::nullopt;
    }
    release.version = *version;
    if (doc.contains("html_url") && doc["html_url"].is_string())
    {
        release.pageUrl = doc["html_url"].get<std::string>();
    }
    if (doc.contains("assets") && doc["assets"].is_array())
    {
        for (const auto& a : doc["assets"])
        {
            if (!a.is_object() || !a.contains("name") || !a["name"].is_string()
                || !a.contains("browser_download_url") || !a["browser_download_url"].is_string())
            {
                continue;
            }
            ReleaseAsset asset;
            asset.name = a["name"].get<std::string>();
            asset.url = a["browser_download_url"].get<std::string>();
            if (a.contains("size") && a["size"].is_number_unsigned())
            {
                asset.size = a["size"].get<std::uint64_t>();
            }
            release.assets.push_back(std::move(asset));
        }
    }
    return release;
}

std::string_view assetSuffix(InstallKind kind)
{
    switch (kind)
    {
        case InstallKind::AppImage:
            return "-x86_64.AppImage";
        case InstallKind::WindowsZip:
            return "-windows-x86_64.zip";
        case InstallKind::Tarball:
            return "-linux-x86_64.tar.gz";
        case InstallKind::None:
            break;
    }
    return {};
}

std::optional<SelectedAsset> selectAsset(const ReleaseInfo& release, InstallKind kind)
{
    const std::string_view suffix = assetSuffix(kind);
    if (suffix.empty())
    {
        return std::nullopt;
    }
    const ReleaseAsset* found = nullptr;
    for (const auto& asset : release.assets)
    {
        if (endsWith(asset.name, suffix))
        {
            if (found)
            {
                return std::nullopt;  // ambiguous
            }
            found = &asset;
        }
    }
    if (!found)
    {
        return std::nullopt;
    }
    const std::string sigName = found->name + ".sig";
    const ReleaseAsset* sig = nullptr;
    for (const auto& asset : release.assets)
    {
        if (asset.name == sigName)
        {
            if (sig)
            {
                return std::nullopt;
            }
            sig = &asset;
        }
    }
    if (!sig || !isHttps(found->url) || !isHttps(sig->url))
    {
        return std::nullopt;
    }
    return SelectedAsset{*found, *sig};
}

std::vector<std::string> notesSince(std::string_view installed, std::string_view offered)
{
    std::unordered_map<std::string_view, int> remaining;
    for (std::string_view line : splitLines(installed))
    {
        ++remaining[line];
    }
    std::vector<std::string> added;
    for (std::string_view line : splitLines(offered))
    {
        auto it = remaining.find(line);
        if (it != remaining.end() && it->second > 0)
        {
            --it->second;
            continue;
        }
        added.emplace_back(line);
    }
    return added;
}

}  // namespace Vestige::Update
