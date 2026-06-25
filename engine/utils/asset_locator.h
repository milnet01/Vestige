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
#include <optional>
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

// --- Part B: folder-picker fallback + remembered asset root -----------------

/// @brief Outcome of @ref chooseAssetRoot.
struct AssetRootChoice
{
    std::string path;     ///< Resolved asset root, or "" if none could be found.
    bool persist = false; ///< true ⇒ a fresh picker choice the caller should save.
};

/// @brief Decide the asset root from the auto-resolved + remembered values,
///        falling back to a folder-picker. **Pure** — the validity predicate and
///        the picker are injected so it is unit-testable without a filesystem or
///        a real dialog. String-only (no `std::filesystem::path`); the caller
///        owns any string↔path adaptation (e.g. wrapping @ref isAssetDir).
///
/// Order: a non-empty @p autoResolved wins (`persist=false`); else a @p remembered
/// path that @p isValid accepts (`persist=false`); else loop @p pickFolder until
/// it returns a path @p isValid accepts (`persist=true`) or "" — cancel/unavailable
/// (`pickFolder` returns "") yields `{"", false}`. A path is persisted only when it
/// passed @p isValid, so a wrong pick is never saved.
AssetRootChoice chooseAssetRoot(
    const std::string& autoResolved,
    const std::string& remembered,
    const std::function<bool(const std::string&)>& isValid,
    const std::function<std::string()>& pickFolder);

/// @brief Parse a remembered-asset-root config blob. **Pure / testable.**
///        Returns the value of the first `assets.path=<value>` line (split on the
///        first `=`, trailing CR/LF stripped, no unescaping), skipping lines with
///        no `=` or any other key. Empty / no `assets.path` line ⇒ `nullopt`.
std::optional<std::string> parseAssetPathConfig(const std::string& contents);

/// @brief Read the remembered asset root from the user-config file
///        (`ConfigPath::getConfigFile("asset_root")`). `nullopt` if absent /
///        unreadable / has no `assets.path` line.
std::optional<std::string> readRememberedAssetPath();

/// @brief Persist @p path as the remembered asset root via `AtomicWrite`.
/// @return true on success (incl. `DirFsyncFailed` — the file is written);
///         false on a real write failure (the caller logs; nothing persisted).
bool writeRememberedAssetPath(const std::string& path);

} // namespace Vestige
