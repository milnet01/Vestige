// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file config_path.h
/// @brief Cross-platform user-config directory resolution.
///
/// Returns the per-user directory where Vestige persistent state
/// (settings, recent-files, autosaves) lives. Platform policy:
///
///  - Linux / *BSD: `$XDG_CONFIG_HOME/vestige/`, fallback to
///    `$HOME/.config/vestige/`, final fallback `/tmp/vestige/`.
///    Matches the XDG Base Directory Specification.
///  - Windows: `%LOCALAPPDATA%\Vestige\`. Matches Unreal / Unity
///    convention; resolved via `SHGetKnownFolderPath(FOLDERID_LocalAppData)`.
///  - macOS (future): `~/Library/Application Support/Vestige/`.
///    Not implemented — stub returns the Linux fallback for now.
///
/// Factored out of `editor/recent_files.cpp` in slice 13.1 so that
/// Settings (slice 13.1), save-games, and any other future
/// persistence callsite share a single resolver. The old
/// `RecentFiles::getConfigDir` forwards to this helper for
/// backward compatibility.
///
/// References:
///  - XDG Base Directory Specification:
///    https://specifications.freedesktop.org/basedir/latest/
///  - Microsoft Known Folder IDs (FOLDERID_LocalAppData):
///    https://learn.microsoft.com/en-us/windows/win32/shell/knownfolderid
#pragma once

#include <filesystem>

namespace Vestige
{
namespace ConfigPath
{

/// @brief Returns the Vestige per-user config directory.
///
/// Does not create the directory — callers that need it created
/// should call `std::filesystem::create_directories` on the return.
/// Pure function: depends only on environment variables / OS APIs,
/// has no side effects.
std::filesystem::path getConfigDir();

/// @brief Returns `getConfigDir() / filename`.
///
/// Convenience for "give me the absolute path where foo.json lives"
/// without having to import `<filesystem>` path arithmetic at every
/// callsite.
std::filesystem::path getConfigFile(const std::string& filename);

} // namespace ConfigPath
} // namespace Vestige
