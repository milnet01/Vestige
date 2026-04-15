// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cube_loader.h
/// @brief Parser for .cube LUT files (industry standard color grading format).
#pragma once

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
    static CubeData load(const std::string& filePath);
};

} // namespace Vestige
