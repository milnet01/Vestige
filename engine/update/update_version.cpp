// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file update_version.cpp
#include "update/update_version.h"

#include <cctype>

namespace Vestige::Update
{

namespace
{

/// Parses a run of ASCII digits with no leading zero (except "0"), bounded so
/// it cannot overflow an int.
std::optional<int> parseNumber(std::string_view s)
{
    if (s.empty() || s.size() > 6 || (s.size() > 1 && s[0] == '0'))
    {
        return std::nullopt;
    }
    int value = 0;
    for (char c : s)
    {
        if (c < '0' || c > '9')
        {
            return std::nullopt;
        }
        value = value * 10 + (c - '0');
    }
    return value;
}

bool isPreReleaseChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.' || c == '-';
}

}  // namespace

std::optional<Version> parseVersion(std::string_view text)
{
    if (!text.empty() && text[0] == 'v')
    {
        text.remove_prefix(1);
    }

    Version version;
    const size_t dash = text.find('-');
    std::string_view core = text.substr(0, dash);
    if (dash != std::string_view::npos)
    {
        const std::string_view pre = text.substr(dash + 1);
        if (pre.empty())
        {
            return std::nullopt;
        }
        for (char c : pre)
        {
            if (!isPreReleaseChar(c))
            {
                return std::nullopt;
            }
        }
        version.pre = std::string(pre);
    }

    for (size_t i = 0; i < 3; ++i)
    {
        const size_t dot = core.find('.');
        const bool last = (i == 2);
        if (last != (dot == std::string_view::npos))
        {
            return std::nullopt;  // too few or too many parts
        }
        auto number = parseNumber(core.substr(0, dot));
        if (!number)
        {
            return std::nullopt;
        }
        version.core[i] = *number;
        core = last ? std::string_view{} : core.substr(dot + 1);
    }
    return version;
}

bool isNewer(const Version& offered, const Version& installed)
{
    if (offered.core != installed.core)
    {
        return offered.core > installed.core;
    }
    // Same core: a release outranks its own pre-releases. Two pre-releases of
    // one core are never offered (only final releases are), so compare them
    // as text rather than implementing SemVer's full identifier ordering.
    if (offered.pre.empty() != installed.pre.empty())
    {
        return offered.pre.empty();
    }
    return offered.pre > installed.pre;
}

std::string toString(const Version& version)
{
    std::string out = std::to_string(version.core[0]) + "." + std::to_string(version.core[1])
                    + "." + std::to_string(version.core[2]);
    if (!version.pre.empty())
    {
        out += "-" + version.pre;
    }
    return out;
}

}  // namespace Vestige::Update
