// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file engine_paths.cpp
/// @brief Implementation of path-composition helpers. See header
///        for contract.

#include "core/engine_paths.h"

namespace Vestige
{

std::string captionMapPath(const std::string& assetPath)
{
    // Extracted verbatim from engine.cpp's pre-refactor call site
    // so the spec-driven test in tests/test_engine_paths.cpp has a
    // single choke point to pin. The join logic is under review in
    // Phase 10.9 Slice 1 F1.
    return assetPath + "assets/captions.json";
}

} // namespace Vestige
