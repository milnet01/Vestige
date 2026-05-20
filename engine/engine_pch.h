// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file engine_pch.h
/// @brief Precompiled-header payload for the engine library (C++ only).
///
/// Collects the heavy headers pulled in by nearly every engine translation
/// unit so they are parsed once instead of ~300 times. Wired up in
/// CMakeLists.txt via target_precompile_headers (scoped to CXX so the C glad
/// loader is unaffected); not meant to be #included directly.
#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
