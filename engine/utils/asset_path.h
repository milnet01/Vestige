// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file asset_path.h
/// @brief The one rule for what a relative asset path means: it names a file
///        under the asset root, never under the working directory.
///
/// Contract: docs/specs/3D_E-0874-asset-relative-paths.md.
///
///   - `toKey(root, path)` — the root-relative, '/'-separated form used as a
///     cache key and written into saved scenes.
///   - `resolve(root, path)` — the file a key or a caller's path names.
///
/// Both are lexical: neither follows a symbolic link, so a file under a linked
/// folder keeps its path below the root. The sandbox check
/// (`PathSandbox::validateInsideRoots`) still runs on every load and is what
/// follows links.
#pragma once

#include <filesystem>
#include <string>

namespace Vestige::AssetPath
{

/// @brief Root-relative, '/'-separated form of @a path, for caching and saving.
///
/// - root empty, any path                 -> @a path unchanged
/// - absolute and lexically inside @a root -> the part below root
/// - relative                             -> normalised; a leading `assets/`
///   is dropped when `root/assets` does not exist (scenes saved before the
///   rule stored `assets/...`)
/// - absolute outside @a root             -> @a path unchanged
std::string toKey(const std::filesystem::path& root, const std::string& path);

/// @brief The file a key or a caller's path names.
///
/// - absolute             -> unchanged
/// - relative, root empty -> unchanged (the working directory, as before)
/// - relative             -> root / toKey(root, path), lexically normalised
std::filesystem::path resolve(const std::filesystem::path& root, const std::string& path);

} // namespace Vestige::AssetPath
