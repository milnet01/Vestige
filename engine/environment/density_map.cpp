/// @file density_map.cpp
/// @brief DensityMap implementation — world-space density texture for foliage modulation.
#include "environment/density_map.h"
#include "core/logger.h"

#include <algorithm>
#include <cmath>

namespace Vestige
{

void DensityMap::initialize(float originX, float originZ,
                            float worldWidth, float worldDepth,
                            float texelsPerMeter)
{
    m_origin = glm::vec2(originX, originZ);
    m_worldExtent = glm::vec2(worldWidth, worldDepth);
    m_texelsPerMeter = std::max(0.1f, texelsPerMeter);

    m_width = std::max(1, static_cast<int>(std::ceil(worldWidth * m_texelsPerMeter)));
    m_height = std::max(1, static_cast<int>(std::ceil(worldDepth * m_texelsPerMeter)));

    m_data.assign(static_cast<size_t>(m_width) * static_cast<size_t>(m_height), 1.0f);
    m_initialized = true;

    Logger::info("DensityMap initialized: " + std::to_string(m_width) + "x"
                 + std::to_string(m_height) + " (" + std::to_string(worldWidth)
                 + "m x " + std::to_string(worldDepth) + "m)");
}

float DensityMap::sample(float worldX, float worldZ) const
{
    if (!m_initialized)
    {
        return 1.0f;
    }

    float tx = 0.0f;
    float tz = 0.0f;
    worldToTexel(worldX, worldZ, tx, tz);

    // Outside bounds: return full density (no restriction)
    if (tx < 0.0f || tz < 0.0f ||
        tx >= static_cast<float>(m_width) || tz >= static_cast<float>(m_height))
    {
        return 1.0f;
    }

    // Bilinear interpolation
    int x0 = static_cast<int>(std::floor(tx));
    int z0 = static_cast<int>(std::floor(tz));
    int x1 = std::min(x0 + 1, m_width - 1);
    int z1 = std::min(z0 + 1, m_height - 1);
    x0 = std::max(0, x0);
    z0 = std::max(0, z0);

    float fx = tx - static_cast<float>(x0);
    float fz = tz - static_cast<float>(z0);

    float v00 = m_data[static_cast<size_t>(z0) * static_cast<size_t>(m_width) + static_cast<size_t>(x0)];
    float v10 = m_data[static_cast<size_t>(z0) * static_cast<size_t>(m_width) + static_cast<size_t>(x1)];
    float v01 = m_data[static_cast<size_t>(z1) * static_cast<size_t>(m_width) + static_cast<size_t>(x0)];
    float v11 = m_data[static_cast<size_t>(z1) * static_cast<size_t>(m_width) + static_cast<size_t>(x1)];

    float top = v00 * (1.0f - fx) + v10 * fx;
    float bot = v01 * (1.0f - fx) + v11 * fx;
    return top * (1.0f - fz) + bot * fz;
}

void DensityMap::paint(const glm::vec3& center, float radius,
                       float value, float strength, float falloff)
{
    if (!m_initialized)
    {
        return;
    }

    value = std::clamp(value, 0.0f, 1.0f);
    strength = std::clamp(strength, 0.0f, 1.0f);

    // Find texel range that the brush covers
    float minWx = center.x - radius;
    float maxWx = center.x + radius;
    float minWz = center.z - radius;
    float maxWz = center.z + radius;

    float txMin = 0.0f, tzMin = 0.0f, txMax = 0.0f, tzMax = 0.0f;
    worldToTexel(minWx, minWz, txMin, tzMin);
    worldToTexel(maxWx, maxWz, txMax, tzMax);

    int x0 = std::max(0, static_cast<int>(std::floor(txMin)));
    int z0 = std::max(0, static_cast<int>(std::floor(tzMin)));
    int x1 = std::min(m_width - 1, static_cast<int>(std::ceil(txMax)));
    int z1 = std::min(m_height - 1, static_cast<int>(std::ceil(tzMax)));

    float radiusSq = radius * radius;
    float falloffStart = 1.0f - std::clamp(falloff, 0.0f, 1.0f);

    for (int z = z0; z <= z1; ++z)
    {
        for (int x = x0; x <= x1; ++x)
        {
            // Convert texel back to world position
            float wx = m_origin.x + (static_cast<float>(x) + 0.5f) / m_texelsPerMeter;
            float wz = m_origin.y + (static_cast<float>(z) + 0.5f) / m_texelsPerMeter;

            float dx = wx - center.x;
            float dz = wz - center.z;
            float distSq = dx * dx + dz * dz;

            if (distSq > radiusSq)
            {
                continue;
            }

            // Compute brush weight with falloff
            float dist = std::sqrt(distSq);
            float distRatio = dist / radius;
            float brushWeight = 1.0f;
            if (distRatio > falloffStart && falloff > 0.0f)
            {
                float t = (distRatio - falloffStart) / falloff;
                brushWeight = 1.0f - t * t;  // Quadratic falloff
            }

            float effectiveStrength = strength * brushWeight;

            size_t idx = static_cast<size_t>(z) * static_cast<size_t>(m_width) + static_cast<size_t>(x);
            m_data[idx] = std::clamp(
                m_data[idx] + (value - m_data[idx]) * effectiveStrength,
                0.0f, 1.0f);
        }
    }
}

void DensityMap::clearAlongPath(const SplinePath& path, float margin)
{
    if (!m_initialized || path.getWaypointCount() < 2)
    {
        return;
    }

    float totalLength = path.getLength();
    if (totalLength < 0.001f)
    {
        return;
    }

    float clearRadius = path.width + margin;

    // Sample the spline densely enough that no texel is missed
    // Use half-texel spacing for good coverage
    float stepSize = 0.5f / m_texelsPerMeter;
    int sampleCount = std::max(2, static_cast<int>(totalLength / stepSize) + 1);

    for (int i = 0; i < sampleCount; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(sampleCount - 1);
        glm::vec3 pos = path.evaluate(t);

        // Paint zero density in a circle around this sample point
        paint(pos, clearRadius, 0.0f, 1.0f, 0.0f);
    }
}

void DensityMap::fill(float value)
{
    if (!m_initialized)
    {
        return;
    }
    std::fill(m_data.begin(), m_data.end(), std::clamp(value, 0.0f, 1.0f));
}

float DensityMap::getTexel(int x, int z) const
{
    if (!m_initialized || x < 0 || x >= m_width || z < 0 || z >= m_height)
    {
        return 1.0f;
    }
    return m_data[static_cast<size_t>(z) * static_cast<size_t>(m_width) + static_cast<size_t>(x)];
}

void DensityMap::setTexel(int x, int z, float value)
{
    if (!m_initialized || x < 0 || x >= m_width || z < 0 || z >= m_height)
    {
        return;
    }
    m_data[static_cast<size_t>(z) * static_cast<size_t>(m_width) + static_cast<size_t>(x)] =
        std::clamp(value, 0.0f, 1.0f);
}

nlohmann::json DensityMap::serialize() const
{
    nlohmann::json j;
    j["originX"] = m_origin.x;
    j["originZ"] = m_origin.y;
    j["worldWidth"] = m_worldExtent.x;
    j["worldDepth"] = m_worldExtent.y;
    j["texelsPerMeter"] = m_texelsPerMeter;
    j["width"] = m_width;
    j["height"] = m_height;

    // Store data as array (compact — mostly 1.0f values can be RLE'd by JSON compressors)
    j["data"] = m_data;

    return j;
}

void DensityMap::deserialize(const nlohmann::json& j)
{
    float ox = j.value("originX", 0.0f);
    float oz = j.value("originZ", 0.0f);
    float ww = j.value("worldWidth", 100.0f);
    float wd = j.value("worldDepth", 100.0f);
    float tpm = j.value("texelsPerMeter", 1.0f);

    initialize(ox, oz, ww, wd, tpm);

    if (j.contains("data") && j["data"].is_array())
    {
        size_t expected = static_cast<size_t>(m_width) * static_cast<size_t>(m_height);
        const auto& arr = j["data"];
        size_t count = std::min(arr.size(), expected);
        for (size_t i = 0; i < count; ++i)
        {
            m_data[i] = std::clamp(arr[i].get<float>(), 0.0f, 1.0f);
        }
    }
}

void DensityMap::worldToTexel(float worldX, float worldZ, float& tx, float& tz) const
{
    tx = (worldX - m_origin.x) * m_texelsPerMeter;
    tz = (worldZ - m_origin.y) * m_texelsPerMeter;
}

} // namespace Vestige
