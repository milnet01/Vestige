// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cube_loader.cpp
/// @brief Parser for .cube LUT files.
#include "utils/cube_loader.h"
#include "core/logger.h"
#include "utils/json_size_cap.h"
#include "utils/path_sandbox.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <sstream>

namespace Vestige
{

namespace
{
/// Function-local static so initialisation order across translation
/// units is well-defined (mirrors LipSyncPlayer's pattern).
std::vector<std::filesystem::path>& cubeSandboxRoots()
{
    static std::vector<std::filesystem::path> roots;
    return roots;
}

/// 128 MB cap. A 128³ LUT (the largest LUT_3D_SIZE we accept) is
/// ~2.1 M floating-point entries; even at ~30 chars per data line that's
/// only ~63 MB of text. The cap leaves room for verbose comments while
/// rejecting multi-GB OOM-style inputs.
constexpr std::uintmax_t kCubeMaxBytes = 128ULL * 1024ULL * 1024ULL;
}  // namespace

void CubeLoader::setSandboxRoots(std::vector<std::filesystem::path> roots)
{
    cubeSandboxRoots() = std::move(roots);
}

const std::vector<std::filesystem::path>& CubeLoader::getSandboxRoots()
{
    return cubeSandboxRoots();
}

CubeData CubeLoader::load(const std::string& filePath)
{
    CubeData result;

    // D3: path sandbox. Empty roots = sandbox disabled.
    const auto& roots = cubeSandboxRoots();
    if (!roots.empty())
    {
        auto canon = PathSandbox::validateInsideRoots(
            std::filesystem::path(filePath), roots);
        if (canon.empty())
        {
            Logger::warning("CubeLoader: path rejected (escapes sandbox): " + filePath);
            return result;
        }
    }

    // D3: read the entire file under a 128 MB cap. The text is then
    // parsed line-by-line via std::istringstream — same parse logic as
    // before, just sourced from a memory buffer instead of a stream
    // tied to the disk file.
    auto contents = JsonSizeCap::loadTextFileWithSizeCap(
        filePath, "CubeLoader", kCubeMaxBytes);
    if (!contents.has_value())
    {
        // loadTextFileWithSizeCap already logged the specific reason
        // (missing / over-cap / read failure).
        return result;
    }

    std::istringstream file(*contents);

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

        // Parse metadata. rfind(x, 0) == 0 is the C++17-compatible
        // equivalent of starts_with() — short-circuits at position 0
        // instead of scanning the whole string (cppcheck: stlIfStrFind).
        if (line.rfind("TITLE", 0) == 0)
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

        if (line.rfind("LUT_3D_SIZE", 0) == 0)
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
        if (line.rfind("DOMAIN_MIN", 0) == 0 || line.rfind("DOMAIN_MAX", 0) == 0
            || line.rfind("LUT_1D_SIZE", 0) == 0)
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
