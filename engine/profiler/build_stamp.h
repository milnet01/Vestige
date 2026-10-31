// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file build_stamp.h
/// @brief What this binary was built from; cmake/write_build_stamp.cmake writes
///        the definitions on every build (3D_E-0786).
#pragma once

namespace Vestige
{

/// @brief The full commit id, or "unknown" where the source has no git history.
const char* buildStampCommit();

/// @brief True when the build had uncommitted changes, or the answer is unknown.
bool buildStampDirty();

/// @brief The build type in lower case: "release", "debug", ...
const char* buildStampType();

} // namespace Vestige
