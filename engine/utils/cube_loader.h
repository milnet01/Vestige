// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cube_loader.h
/// @brief Parser for .cube LUT files (industry standard color grading format).
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief Result of parsing a .cube LUT file.
struct CubeData
{
    int size = 0;                           ///< LUT dimension (e.g., 32)
    std::vector<unsigned char> rgbaData;    ///< RGBA8 pixel data for GL_TEXTURE_3D
    std::string title;                      ///< Optional title from the file
};

/// @brief Parses .cube format LUT files into RGBA8 data for 3D texture upload.
class CubeLoader
{
public:
    /// @brief Loads and parses a .cube LUT file.
    /// @param filePath Path to the .cube file.
    /// @return Parsed data, or empty CubeData (size==0) on failure.
    ///
    /// Phase 10.9 Slice 5 D3: a 128 MB file-size cap runs before parse
    /// (a 128³ LUT in floating-point text is ~63 MB; the cap leaves
    /// headroom without admitting multi-GB OOM-style attacks). If
    /// `setSandboxRoots` has installed a non-empty roots list, the path
    /// is canonicalised and verified inside one of those roots before
    /// being opened — empty roots (the default) leave the sandbox
    /// disabled, preserving backwards compatibility.
    static CubeData load(const std::string& filePath);

    /// @brief Configure the path sandbox for `load()` calls.
    ///
    /// Process-wide static (CubeLoader exposes no instance state). Empty
    /// roots disable the sandbox.
    static void setSandboxRoots(std::vector<std::filesystem::path> roots);

    /// @brief Read-back accessor for the configured sandbox roots.
    static const std::vector<std::filesystem::path>& getSandboxRoots();
};

} // namespace Vestige
