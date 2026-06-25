// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file path_sandbox.cpp
#include "utils/path_sandbox.h"

#include <cctype>

namespace fs = std::filesystem;

namespace Vestige::PathSandbox
{

namespace
{

/// @brief Cross-platform "this URI is (or tries to be) absolute / rooted".
///
/// `fs::path::is_absolute()` is platform-specific: a leading "/" is absolute on
/// POSIX but NOT on Windows (which wants a drive, e.g. "C:\"). Without this,
/// `base / "/etc/passwd"` *replaces* base on POSIX (→ rejected by the containment
/// check) but *appends* on Windows (→ resolves inside base → sandbox escape).
/// Reject any URI that is native-absolute, POSIX-rooted ("/"), UNC ("\\"), or
/// Windows drive-prefixed ("C:...") so the sandbox behaves identically everywhere.
bool isAbsoluteOrRooted(const std::string& uri)
{
    if (uri.empty())
        return false;
    if (uri.front() == '/' || uri.front() == '\\')
        return true;  // POSIX root or UNC / leading-separator escape
    if (fs::path(uri).is_absolute())
        return true;  // native-absolute (e.g. "C:\foo" on Windows)
    // Windows drive prefix ("C:foo" / "C:\foo") — not is_absolute() off Windows.
    if (uri.size() >= 2
        && std::isalpha(static_cast<unsigned char>(uri[0]))
        && uri[1] == ':')
        return true;
    return false;
}

/// @brief Returns `true` iff @a canon equals @a base or is a strict
///        descendant of @a base. AUDIT M16 separator-suffix rule.
bool insideOrEqual(const std::string& canon, const std::string& base)
{
    if (canon == base)
        return true;
    std::string baseWithSep = base;
    if (!baseWithSep.empty()
        && baseWithSep.back() != static_cast<char>(fs::path::preferred_separator))
    {
        baseWithSep.push_back(static_cast<char>(fs::path::preferred_separator));
    }
    return canon.compare(0, baseWithSep.size(), baseWithSep) == 0;
}

}  // namespace

std::string resolveUriIntoBase(const fs::path& base, const std::string& uri)
{
    if (uri.empty())
        return {};

    // Reject absolute / rooted URIs cross-platform before joining (see
    // isAbsoluteOrRooted) — otherwise "/etc/passwd" escapes the sandbox on
    // Windows, where it is treated as a relative sub-path rather than absolute.
    if (isAbsoluteOrRooted(uri))
        return {};

    fs::path resolved = base / uri;
    std::error_code ec;
    auto canonical = fs::weakly_canonical(resolved, ec);
    if (ec)
        return {};

    auto canonicalBase = fs::weakly_canonical(base, ec);
    if (ec)
        return {};

    std::string canonStr = canonical.string();
    std::string baseStr = canonicalBase.string();
    return insideOrEqual(canonStr, baseStr) ? canonStr : std::string{};
}

std::string validateInsideRoots(const fs::path& absPath,
                                const std::vector<fs::path>& roots)
{
    std::error_code ec;
    auto canon = fs::weakly_canonical(absPath, ec);
    if (ec)
        return {};

    std::string canonStr = canon.string();

    if (roots.empty())
        return canonStr;

    for (const auto& root : roots)
    {
        auto canonRoot = fs::weakly_canonical(root, ec);
        if (ec)
            continue;
        if (insideOrEqual(canonStr, canonRoot.string()))
            return canonStr;
    }
    return {};
}

}  // namespace Vestige::PathSandbox
