// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file engine_paths.h
/// @brief Path-composition helpers for engine asset roots.
///
/// These helpers build full filesystem paths for well-known engine
/// assets (caption maps, future: scene files, shader roots, etc.)
/// from the configured asset path. Isolating the join logic keeps
/// it unit-testable without standing up an `Engine` instance.
#pragma once

#include <string>

namespace Vestige
{

/// @brief Build the path to the declarative caption-map JSON file.
///
/// Per `docs/phases/phase_10_7_design.md` §4.2 the caption map lives at
/// `<assetPath>/captions.json`. A project that ships no captions
/// simply has no file at this path (the loader treats "absent" as
/// "empty map", which is intentional).
///
/// @param assetPath The root asset directory (e.g. `"assets"` or
///                  `"/opt/vestige-game/assets"`). Trailing slashes
///                  are tolerated.
/// @return Full path to the caption-map file.
std::string captionMapPath(const std::string& assetPath);

} // namespace Vestige
