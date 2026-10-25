// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file stills_file.h
/// @brief Reads the camera spots for --stills: one picture per spot.
#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Vestige
{

/// @brief One camera spot read from a stills file.
struct StillSpot
{
    std::string name;            ///< Letters, digits, '_' and '-' only (used in filenames)
    glm::vec3 position{0.0f};    ///< World position; y is metres above the ground when aboveGround
    bool aboveGround = false;    ///< y was written "~h": h metres above the terrain at (x, z)
    float yaw = -90.0f;          ///< Degrees, Camera convention (−90 faces −Z)
    float pitch = 0.0f;          ///< Degrees
};

/// @brief What parseStills() found: the spots, or the first error.
struct StillsParseResult
{
    std::vector<StillSpot> spots;
    std::string error;           ///< Empty on success; else "line N: what is wrong"
};

/// @brief Parses a stills file's text.
///
/// One spot per line: `name x y z yaw pitch`, separated by spaces or tabs.
/// Writing y as `~1.7` puts the eye 1.7 m above the ground at (x, z).
/// Blank lines and lines starting with `#` are skipped. A file with no
/// spots is an error, so a typo'd path to an empty file cannot pass.
StillsParseResult parseStills(const std::string& text);

/// @brief Reads @p path and parses it with parseStills().
StillsParseResult loadStillsFile(const std::string& path);

} // namespace Vestige
