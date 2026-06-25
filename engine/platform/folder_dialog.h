// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file folder_dialog.h
/// @brief Native OS folder picker — the last-resort asset-location fallback.
#pragma once

#include <string>

namespace Vestige
{

/// @brief Show a native OS "choose a folder" dialog.
/// @return The chosen absolute directory, or "" if the user cancelled **or** no
///         dialog could be shown (headless / no display server / no dialog tool).
///         Never blocks indefinitely. Implemented via tinyfiledialogs; on Linux
///         it returns "" immediately when no display server is present rather
///         than risk the underlying tool hanging.
std::string pickAssetFolder();

} // namespace Vestige
