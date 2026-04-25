// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file path_sandbox.h
/// @brief Path-traversal guards reusable across the engine.
///
/// Two flavours:
///
///   - `resolveUriIntoBase(base, uri)` — for *relative* URIs (e.g. an asset
///     reference inside a glTF / scene-JSON that should resolve under a
///     scene-defined base directory). Returns the canonical path if it
///     stays inside `base` after resolution, empty string otherwise.
///
///   - `validateInsideRoots(abs, roots)` — for *already-absolute* paths
///     coming from trusted callers (editor file pickers, hard-coded
///     install-asset references). Returns the canonical path if it lies
///     inside any of `roots`, empty string otherwise.
///
/// Lifted from `gltf_loader.cpp::resolveUri` (originally Phase 5 / AUDIT
/// M16) to give every asset-loading entry-point a single canonical
/// implementation per Phase 10.9 Slice 5 D1. Adding a new asset loader
/// should never duplicate this dance — call `resolveUriIntoBase` /
/// `validateInsideRoots` directly.
///
/// AUDIT note (M16): the prefix-match step *must* compare against
/// `base + preferred_separator`, otherwise sibling directories that
/// share leading characters with the base (e.g. `/assets/foo` vs
/// `/assets/foo_evil/x.png`) silently slip past the check.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Vestige::PathSandbox
{

/// @brief Resolves @a uri against @a base and ensures the result stays
///        inside @a base after canonicalisation.
///
/// @param base An existing or weakly-canonicalisable directory.
/// @param uri  Relative or absolute URI as a string. Empty input returns
///             empty output without warning (caller decides whether to
///             default-substitute, per Slice 5 D5).
/// @return     Canonical absolute path on success; empty string on
///             escape / canonicalisation failure / empty input.
std::string resolveUriIntoBase(const std::filesystem::path& base,
                               const std::string& uri);

/// @brief Validates that @a absPath, after canonicalisation, lies inside
///        one of @a roots.
///
/// The "lies inside" test is the same as `resolveUriIntoBase`:
/// equality with a root *or* strict descendancy (`canon` starts with
/// `root + preferred_separator`).
///
/// @param absPath Path to check (absolute or relative — relative inputs
///                are canonicalised against the current working dir).
/// @param roots   Allowed roots. Empty `roots` means "no sandbox active";
///                in that case the function returns the canonical path
///                unchanged. This keeps the helper drop-in for callers
///                during a staged rollout.
/// @return        Canonical absolute path on success; empty string on
///                escape / canonicalisation failure.
std::string validateInsideRoots(const std::filesystem::path& absPath,
                                const std::vector<std::filesystem::path>& roots);

}  // namespace Vestige::PathSandbox
