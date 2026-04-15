// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cube_loader.cpp
/// @brief Parser for .cube LUT files.
#include "utils/cube_loader.h"
#include "core/logger.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace Vestige
{

CubeData CubeLoader::load(const std::string& filePath)
{
    CubeData result;

    std::ifstream file(filePath);
    if (!file.is_open())
    {
        Logger::error("CubeLoader: failed to open " + filePath);
        return result;
    }

    int lutSize = 0;
    std::vector<glm::vec3> entries;

    std::string line;
    while (std::getline(file, line))
    {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        // Parse metadata
        if (line.find("TITLE") == 0)
        {
            // TITLE "name" or TITLE name
            size_t start = line.find('"');
            if (start != std::string::npos)
            {
                size_t end = line.find('"', start + 1);
                if (end != std::string::npos)
                {
                    result.title = line.substr(start + 1, end - start - 1);
                }
            }
            else
            {
                result.title = line.substr(6);
            }
            continue;
        }

        if (line.find("LUT_3D_SIZE") == 0)
        {
            std::istringstream iss(line.substr(12));
            iss >> lutSize;
            if (lutSize < 2 || lutSize > 128)
            {
                Logger::error("CubeLoader: invalid LUT size " + std::to_string(lutSize));
                return CubeData{};
            }
            entries.reserve(static_cast<size_t>(lutSize * lutSize * lutSize));
            continue;
        }

        // Skip other metadata lines
        if (line.find("DOMAIN_MIN") == 0 || line.find("DOMAIN_MAX") == 0
            || line.find("LUT_1D_SIZE") == 0)
        {
            continue;
        }

        // Parse data line: three floats
        if (lutSize == 0)
        {
            continue;  // Haven't seen LUT_3D_SIZE yet
        }

        std::istringstream iss(line);
        float r, g, b;
        if (!(iss >> r >> g >> b))
        {
            continue;  // Not a data line
        }

        r = std::clamp(r, 0.0f, 1.0f);
        g = std::clamp(g, 0.0f, 1.0f);
        b = std::clamp(b, 0.0f, 1.0f);
        entries.emplace_back(r, g, b);
    }

    if (lutSize == 0)
    {
        Logger::error("CubeLoader: no LUT_3D_SIZE found in " + filePath);
        return CubeData{};
    }

    int expectedEntries = lutSize * lutSize * lutSize;
    if (static_cast<int>(entries.size()) != expectedEntries)
    {
        Logger::error("CubeLoader: expected " + std::to_string(expectedEntries)
            + " entries but got " + std::to_string(entries.size()));
        return CubeData{};
    }

    // Convert float entries to RGBA8
    result.size = lutSize;
    result.rgbaData.resize(static_cast<size_t>(expectedEntries) * 4);

    for (int i = 0; i < expectedEntries; i++)
    {
        size_t idx = static_cast<size_t>(i) * 4;
        result.rgbaData[idx + 0] = static_cast<unsigned char>(entries[static_cast<size_t>(i)].r * 255.0f + 0.5f);
        result.rgbaData[idx + 1] = static_cast<unsigned char>(entries[static_cast<size_t>(i)].g * 255.0f + 0.5f);
        result.rgbaData[idx + 2] = static_cast<unsigned char>(entries[static_cast<size_t>(i)].b * 255.0f + 0.5f);
        result.rgbaData[idx + 3] = 255;
    }

    Logger::info("CubeLoader: loaded " + filePath + " (" + std::to_string(lutSize) + "^3)");
    return result;
}

} // namespace Vestige
