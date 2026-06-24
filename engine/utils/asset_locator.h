// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file asset_locator.h
/// @brief Locate the engine's `assets/` directory robustly across package
///        formats (AppImage / tarball / zip) and dev runs.
///
/// The engine reads its shaders/fonts/scenes from an asset root. Historically
/// that root was the literal relative path `"assets"`, i.e. resolved against the
/// current working directory — so a packaged build only ran if launched from its
/// own folder. These helpers add binary-relative resolution so the app finds the
/// assets that ship beside it no matter where it is launched from.
///
/// Resolution order (see @ref resolveAssetPath):
///   1. an explicit `--assets <path>` CLI override (honoured as-is),
///   2. the `VESTIGE_ASSETS` environment variable,
///   3. `<exe-dir>/assets`,
///   4. `<exe-dir>/../share/vestige/assets` (installed / AppDir layout),
///   5. `<cwd>/assets` (back-compat with the old behaviour).
///
/// The search-order + selection logic is split into pure functions so it is
/// unit-testable without a real filesystem (project Rule 7 / six-month test).
#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief Absolute directory containing the running executable.
/// @return The directory, or an empty path if it could not be determined
///         (Linux: `/proc/self/exe`; Windows: `GetModuleFileNameW`).
std::filesystem::path executableDir();

/// @brief Ordered auto-search candidate asset directories (steps 3–5 above).
///        Pure — takes the executable dir and the working dir explicitly so the
///        ordering can be unit-tested. An empty @p exeDir omits the
///        binary-relative candidates.
std::vector<std::filesystem::path> assetSearchCandidates(
    const std::filesystem::path& exeDir,
    const std::filesystem::path& cwd);

/// @brief First candidate @p isValid accepts, or an empty path if none do.
///        Pure — @p isValid is injected so tests can stub the filesystem.
std::filesystem::path firstValidAssetDir(
    const std::vector<std::filesystem::path>& candidates,
    const std::function<bool(const std::filesystem::path&)>& isValid);

/// @brief True if @p dir looks like a real asset root — i.e. it contains the
///        sentinel `shaders/scene.vert.glsl` the renderer always needs.
bool isAssetDir(const std::filesystem::path& dir);

/// @brief Resolve the asset root against the real environment + filesystem.
/// @param cliOverride The `--assets` value, or "" if not given.
/// @return The resolved asset path, or "" if none was found (the caller then
///         falls back to a folder-picker / fatal error).
std::string resolveAssetPath(const std::string& cliOverride);

} // namespace Vestige
