// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file lut_loader.cpp
/// @brief Binary LUT loader implementation.
#include "formula/lut_loader.h"
#include "core/logger.h"

#include <algorithm>
#include <cmath>
#include <climits>
#include <cstring>
#include <fstream>
#include <limits>

namespace Vestige
{

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

bool LutLoader::loadFromFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        Logger::warning("LutLoader: cannot open file: " + path);
        return false;
    }

    // Read header
    struct
    {
        uint32_t magic;
        uint32_t version;
        uint32_t dimensions;
        uint32_t axisSizes[3];
        uint32_t valueType;
        uint32_t flags;
    } header{};

    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file.good())
    {
        Logger::warning("LutLoader: failed to read header from: " + path);
        return false;
    }

    if (header.magic != VLUT_MAGIC)
    {
        Logger::warning("LutLoader: invalid magic in: " + path);
        return false;
    }

    if (header.version != VLUT_VERSION)
    {
        Logger::warning("LutLoader: unsupported version " + std::to_string(header.version)
                        + " in: " + path);
        return false;
    }

    if (header.dimensions < 1 || header.dimensions > 3)
    {
        Logger::warning("LutLoader: invalid dimensions " + std::to_string(header.dimensions)
                        + " in: " + path);
        return false;
    }

    // Read axis definitions
    m_axes.clear();
    m_axes.resize(header.dimensions);

    for (uint32_t i = 0; i < header.dimensions; ++i)
    {
        struct
        {
            uint32_t nameHash;
            float minValue;
            float maxValue;
            uint32_t padding;
        } axisEntry{};

        file.read(reinterpret_cast<char*>(&axisEntry), sizeof(axisEntry));
        if (!file.good())
        {
            Logger::warning("LutLoader: failed to read axis " + std::to_string(i)
                            + " from: " + path);
            m_axes.clear();
            return false;
        }

        m_axes[i].nameHash = axisEntry.nameHash;
        m_axes[i].minValue = axisEntry.minValue;
        m_axes[i].maxValue = axisEntry.maxValue;
        m_axes[i].size = header.axisSizes[i];
    }

    // Read data
    size_t totalSamples = 1;
    for (const auto& axis : m_axes)
    {
        if (axis.size == 0 || totalSamples > SIZE_MAX / axis.size)
        {
            Logger::error("LutLoader: integer overflow in totalSamples for: " + path);
            m_axes.clear();
            return false;
        }
        totalSamples *= axis.size;
    }

    // Check that totalSamples * sizeof(float) fits in std::streamsize
    constexpr size_t maxStreamSamples =
        static_cast<size_t>(std::numeric_limits<std::streamsize>::max()) / sizeof(float);
    if (totalSamples > maxStreamSamples)
    {
        Logger::error("LutLoader: data size exceeds streamsize limit for: " + path);
        m_axes.clear();
        return false;
    }

    m_data.resize(totalSamples);
    file.read(reinterpret_cast<char*>(m_data.data()),
              static_cast<std::streamsize>(totalSamples * sizeof(float)));

    if (!file.good())
    {
        Logger::warning("LutLoader: failed to read data from: " + path);
        m_data.clear();
        m_axes.clear();
        return false;
    }

    return true;
}

bool LutLoader::loadFromResult(const LutGenerateResult& result)
{
    if (!result.success || result.data.empty() || result.axes.empty())
        return false;

    m_data = result.data;
    m_axes.clear();
    m_axes.reserve(result.axes.size());

    for (const auto& axisDef : result.axes)
    {
        LutAxis axis;
        axis.nameHash = LutGenerator::fnv1aHash(axisDef.variableName);
        axis.minValue = axisDef.minValue;
        axis.maxValue = axisDef.maxValue;
        axis.size = axisDef.resolution;
        m_axes.push_back(axis);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Sampling
// ---------------------------------------------------------------------------

float LutLoader::toIndex(float val, const LutAxis& axis) const
{
    float range = axis.maxValue - axis.minValue;
    if (range < 1e-10f)
        return 0.0f;

    float t = (val - axis.minValue) / range;
    t = std::clamp(t, 0.0f, 1.0f);
    return t * static_cast<float>(axis.size - 1);
}

float LutLoader::fetch(int x, int y, int z) const
{
    int w = m_axes.size() > 0 ? static_cast<int>(m_axes[0].size) : 1;
    int h = m_axes.size() > 1 ? static_cast<int>(m_axes[1].size) : 1;

    x = std::clamp(x, 0, w - 1);
    y = std::clamp(y, 0, h - 1);
    if (m_axes.size() > 2)
    {
        int d = static_cast<int>(m_axes[2].size);
        z = std::clamp(z, 0, d - 1);
    }
    else
    {
        z = 0;
    }

    size_t index = static_cast<size_t>(z) * static_cast<size_t>(h) * static_cast<size_t>(w)
                 + static_cast<size_t>(y) * static_cast<size_t>(w)
                 + static_cast<size_t>(x);

    if (index >= m_data.size())
        return 0.0f;

    return m_data[index];
}

float LutLoader::sample1D(float x) const
{
    if (m_axes.empty() || m_data.empty())
        return 0.0f;

    float fi = toIndex(x, m_axes[0]);
    int i0 = static_cast<int>(fi);
    float fx = fi - static_cast<float>(i0);

    return lerp(fetch(i0), fetch(i0 + 1), fx);
}

float LutLoader::sample2D(float x, float y) const
{
    if (m_axes.size() < 2 || m_data.empty())
        return 0.0f;

    float fi = toIndex(x, m_axes[0]);
    float fj = toIndex(y, m_axes[1]);

    int i0 = static_cast<int>(fi);
    int j0 = static_cast<int>(fj);
    float fx = fi - static_cast<float>(i0);
    float fy = fj - static_cast<float>(j0);

    // Bilinear interpolation
    float v00 = fetch(i0,     j0);
    float v10 = fetch(i0 + 1, j0);
    float v01 = fetch(i0,     j0 + 1);
    float v11 = fetch(i0 + 1, j0 + 1);

    return lerp(lerp(v00, v10, fx), lerp(v01, v11, fx), fy);
}

float LutLoader::sample3D(float x, float y, float z) const
{
    if (m_axes.size() < 3 || m_data.empty())
        return 0.0f;

    float fi = toIndex(x, m_axes[0]);
    float fj = toIndex(y, m_axes[1]);
    float fk = toIndex(z, m_axes[2]);

    int i0 = static_cast<int>(fi);
    int j0 = static_cast<int>(fj);
    int k0 = static_cast<int>(fk);
    float fx = fi - static_cast<float>(i0);
    float fy = fj - static_cast<float>(j0);
    float fz = fk - static_cast<float>(k0);

    // Trilinear interpolation
    float v000 = fetch(i0,     j0,     k0);
    float v100 = fetch(i0 + 1, j0,     k0);
    float v010 = fetch(i0,     j0 + 1, k0);
    float v110 = fetch(i0 + 1, j0 + 1, k0);
    float v001 = fetch(i0,     j0,     k0 + 1);
    float v101 = fetch(i0 + 1, j0,     k0 + 1);
    float v011 = fetch(i0,     j0 + 1, k0 + 1);
    float v111 = fetch(i0 + 1, j0 + 1, k0 + 1);

    float c00 = lerp(v000, v100, fx);
    float c10 = lerp(v010, v110, fx);
    float c01 = lerp(v001, v101, fx);
    float c11 = lerp(v011, v111, fx);

    float c0 = lerp(c00, c10, fy);
    float c1 = lerp(c01, c11, fy);

    return lerp(c0, c1, fz);
}

} // namespace Vestige
