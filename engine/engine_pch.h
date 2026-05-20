// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file engine_pch.h
/// @brief Precompiled-header payload for the engine library (C++ only).
///
/// GLM is included by nearly every engine translation unit and is the single
/// heaviest common header, so precompiling it once (instead of ~300 times)
/// is the bulk of the PCH win. Wired up in CMakeLists.txt via
/// target_precompile_headers (scoped to CXX so the C glad loader is
/// unaffected); not meant to be #included directly.
///
/// Deliberately does NOT precompile <string>/<vector>/<memory> etc.:
/// precompiling <string> trips a GCC 13 -Warray-bounds false positive in
/// char_traits.h under -O3 + _FORTIFY_SOURCE=3 (Release), which -Werror turns
/// fatal. GLM itself compiles clean at -O3, and the STL headers are cheap to
/// parse per-TU, so the net loss is negligible.
#pragma once

#include <glm/glm.hpp>
