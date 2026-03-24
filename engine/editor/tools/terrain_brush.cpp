/// @file terrain_brush.cpp
/// @brief Brush tool for terrain sculpting and texture painting.

#include "editor/tools/terrain_brush.h"
#include "editor/command_history.h"
#include "editor/commands/terrain_sculpt_command.h"

#include <algorithm>
#include <cmath>

namespace Vestige
{

bool TerrainBrush::processInput(const Ray& mouseRay, bool mouseDown, float deltaTime,
                                Terrain& terrain, CommandHistory& history)
{
    if (!m_enabled || !terrain.isInitialized())
    {
        return false;
    }

    // Raycast against terrain for brush position
    glm::vec3 hitPoint;
    m_hasHit = terrain.raycast(mouseRay, 500.0f, hitPoint);
    if (m_hasHit)
    {
        m_hitPoint = hitPoint;
        m_hitNormal = terrain.getNormal(hitPoint.x, hitPoint.z);
    }

    if (mouseDown && m_hasHit)
    {
        if (!m_painting)
        {
            beginStroke(terrain);
        }
        applyBrush(m_hitPoint, deltaTime, terrain);
    }
    else if (m_painting)
    {
        endStroke(terrain, history);
    }

    return m_hasHit;
}

bool TerrainBrush::getHitPoint(glm::vec3& outPoint, glm::vec3& outNormal) const
{
    if (!m_hasHit) return false;
    outPoint = m_hitPoint;
    outNormal = m_hitNormal;
    return true;
}

void TerrainBrush::beginStroke(Terrain& terrain)
{
    m_painting = true;
    m_hasDirtyRegion = false;

    // Snapshot the entire heightmap/splatmap for undo
    // (we'll trim to dirty region when creating the command)
    int w = terrain.getWidth();
    int d = terrain.getDepth();
    int total = w * d;

    if (mode == TerrainBrushMode::PAINT)
    {
        m_beforeSplat.resize(static_cast<size_t>(total));
        for (int z = 0; z < d; ++z)
        {
            for (int x = 0; x < w; ++x)
            {
                m_beforeSplat[static_cast<size_t>(z * w + x)] = terrain.getSplatWeight(x, z);
            }
        }
    }
    else
    {
        m_beforeHeights.resize(static_cast<size_t>(total));
        for (int z = 0; z < d; ++z)
        {
            for (int x = 0; x < w; ++x)
            {
                m_beforeHeights[static_cast<size_t>(z * w + x)] = terrain.getRawHeight(x, z);
            }
        }

        // For flatten mode, sample the reference height at the cursor
        if (mode == TerrainBrushMode::FLATTEN)
        {
            m_flattenHeight = terrain.getRawHeight(
                static_cast<int>((m_hitPoint.x - terrain.getConfig().origin.x)
                                 / terrain.getConfig().spacingX),
                static_cast<int>((m_hitPoint.z - terrain.getConfig().origin.z)
                                 / terrain.getConfig().spacingZ));
        }
    }
}

void TerrainBrush::endStroke(Terrain& terrain, CommandHistory& history)
{
    m_painting = false;

    if (!m_hasDirtyRegion) return;

    int w = terrain.getWidth();
    int d = terrain.getDepth();

    // Clamp dirty region
    m_dirtyMinX = std::max(0, m_dirtyMinX);
    m_dirtyMinZ = std::max(0, m_dirtyMinZ);
    m_dirtyMaxX = std::min(w - 1, m_dirtyMaxX);
    m_dirtyMaxZ = std::min(d - 1, m_dirtyMaxZ);

    int regionW = m_dirtyMaxX - m_dirtyMinX + 1;
    int regionH = m_dirtyMaxZ - m_dirtyMinZ + 1;

    if (regionW <= 0 || regionH <= 0) return;

    if (mode == TerrainBrushMode::PAINT)
    {
        // Extract before/after splatmap for just the dirty region
        std::vector<glm::vec4> beforeRegion(static_cast<size_t>(regionW * regionH));
        std::vector<glm::vec4> afterRegion(static_cast<size_t>(regionW * regionH));

        for (int rz = 0; rz < regionH; ++rz)
        {
            for (int rx = 0; rx < regionW; ++rx)
            {
                int srcIdx = (m_dirtyMinZ + rz) * w + (m_dirtyMinX + rx);
                size_t dstIdx = static_cast<size_t>(rz * regionW + rx);
                beforeRegion[dstIdx] = m_beforeSplat[static_cast<size_t>(srcIdx)];
                afterRegion[dstIdx] = terrain.getSplatWeight(m_dirtyMinX + rx, m_dirtyMinZ + rz);
            }
        }

        auto cmd = std::make_unique<TerrainPaintCommand>(
            terrain, m_dirtyMinX, m_dirtyMinZ, regionW, regionH,
            std::move(beforeRegion), std::move(afterRegion));
        history.execute(std::move(cmd));
    }
    else
    {
        // Extract before/after heights for just the dirty region
        std::vector<float> beforeRegion(static_cast<size_t>(regionW * regionH));
        std::vector<float> afterRegion(static_cast<size_t>(regionW * regionH));

        for (int rz = 0; rz < regionH; ++rz)
        {
            for (int rx = 0; rx < regionW; ++rx)
            {
                int srcIdx = (m_dirtyMinZ + rz) * w + (m_dirtyMinX + rx);
                size_t dstIdx = static_cast<size_t>(rz * regionW + rx);
                beforeRegion[dstIdx] = m_beforeHeights[static_cast<size_t>(srcIdx)];
                afterRegion[dstIdx] = terrain.getRawHeight(m_dirtyMinX + rx, m_dirtyMinZ + rz);
            }
        }

        auto cmd = std::make_unique<TerrainSculptCommand>(
            terrain, m_dirtyMinX, m_dirtyMinZ, regionW, regionH,
            std::move(beforeRegion), std::move(afterRegion));
        history.execute(std::move(cmd));
    }

    m_beforeHeights.clear();
    m_beforeSplat.clear();

    // Rebuild quadtree after sculpting to update min/max heights
    if (mode != TerrainBrushMode::PAINT)
    {
        terrain.buildQuadtree();
    }
}

void TerrainBrush::applyBrush(const glm::vec3& center, float dt, Terrain& terrain)
{
    const auto& cfg = terrain.getConfig();

    // Convert world brush center to texel space
    float tcx = (center.x - cfg.origin.x) / cfg.spacingX;
    float tcz = (center.z - cfg.origin.z) / cfg.spacingZ;

    // Brush radius in texels
    float texelRadius = radius / cfg.spacingX;

    int minX = static_cast<int>(std::floor(tcx - texelRadius));
    int maxX = static_cast<int>(std::ceil(tcx + texelRadius));
    int minZ = static_cast<int>(std::floor(tcz - texelRadius));
    int maxZ = static_cast<int>(std::ceil(tcz + texelRadius));

    minX = std::max(0, minX);
    maxX = std::min(cfg.width - 1, maxX);
    minZ = std::max(0, minZ);
    maxZ = std::min(cfg.depth - 1, maxZ);

    for (int z = minZ; z <= maxZ; ++z)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            // Distance from brush center in texels
            float dx = static_cast<float>(x) - tcx;
            float dz = static_cast<float>(z) - tcz;
            float dist = std::sqrt(dx * dx + dz * dz);

            if (dist > texelRadius) continue;

            float weight = computeFalloff(dist / texelRadius);

            switch (mode)
            {
                case TerrainBrushMode::RAISE:
                    applyRaise(x, z, weight, dt, terrain);
                    break;
                case TerrainBrushMode::LOWER:
                    applyLower(x, z, weight, dt, terrain);
                    break;
                case TerrainBrushMode::SMOOTH:
                    applySmooth(x, z, weight, dt, terrain);
                    break;
                case TerrainBrushMode::FLATTEN:
                    applyFlatten(x, z, weight, dt, terrain);
                    break;
                case TerrainBrushMode::PAINT:
                    applyPaint(x, z, weight, dt, terrain);
                    break;
            }

            expandDirtyRegion(x, z);
        }
    }

    // Partial GPU upload for the affected region
    int uploadW = maxX - minX + 1;
    int uploadH = maxZ - minZ + 1;
    if (uploadW > 0 && uploadH > 0)
    {
        if (mode == TerrainBrushMode::PAINT)
        {
            terrain.updateSplatmapRegion(minX, minZ, uploadW, uploadH);
        }
        else
        {
            terrain.updateHeightmapRegion(minX, minZ, uploadW, uploadH);
            terrain.updateNormalMapRegion(minX, minZ, uploadW, uploadH);
        }
    }
}

void TerrainBrush::applyRaise(int x, int z, float weight, float dt, Terrain& terrain)
{
    float h = terrain.getRawHeight(x, z);
    h += strength * weight * dt;
    h = std::min(h, 1.0f);
    terrain.setRawHeight(x, z, h);
}

void TerrainBrush::applyLower(int x, int z, float weight, float dt, Terrain& terrain)
{
    float h = terrain.getRawHeight(x, z);
    h -= strength * weight * dt;
    h = std::max(h, 0.0f);
    terrain.setRawHeight(x, z, h);
}

void TerrainBrush::applySmooth(int x, int z, float weight, float dt, Terrain& terrain)
{
    // Average of 4 cardinal neighbors + center
    float center = terrain.getRawHeight(x, z);
    float left = terrain.getRawHeight(x - 1, z);
    float right = terrain.getRawHeight(x + 1, z);
    float down = terrain.getRawHeight(x, z - 1);
    float up = terrain.getRawHeight(x, z + 1);

    float avg = (center + left + right + down + up) / 5.0f;
    float smoothed = center + (avg - center) * weight * strength * dt * 4.0f;
    smoothed = std::clamp(smoothed, 0.0f, 1.0f);
    terrain.setRawHeight(x, z, smoothed);
}

void TerrainBrush::applyFlatten(int x, int z, float weight, float dt, Terrain& terrain)
{
    float h = terrain.getRawHeight(x, z);
    float target = m_flattenHeight;
    float flattened = h + (target - h) * weight * strength * dt * 4.0f;
    flattened = std::clamp(flattened, 0.0f, 1.0f);
    terrain.setRawHeight(x, z, flattened);
}

void TerrainBrush::applyPaint(int x, int z, float weight, float dt, Terrain& terrain)
{
    glm::vec4 splat = terrain.getSplatWeight(x, z);

    // Increase selected channel weight
    float addAmount = strength * weight * dt * 2.0f;
    splat[paintChannel] += addAmount;

    // Normalize so all channels sum to 1.0
    float total = splat.r + splat.g + splat.b + splat.a;
    if (total > 0.0f)
    {
        splat /= total;
    }

    for (int c = 0; c < 4; ++c)
    {
        terrain.setSplatWeight(x, z, c, splat[c]);
    }
}

float TerrainBrush::computeFalloff(float normalizedDist) const
{
    // normalizedDist is 0 at center, 1 at edge
    if (normalizedDist >= 1.0f) return 0.0f;

    // falloff=0: sharp circle (constant weight)
    // falloff=1: full taper (linear falloff from center to edge)
    float t = normalizedDist;
    float edge = 1.0f - falloff;  // Distance where falloff begins

    if (t <= edge)
    {
        return 1.0f;
    }

    // Smooth hermite falloff from edge to 1.0
    float f = (t - edge) / (1.0f - edge);
    return 1.0f - f * f * (3.0f - 2.0f * f);  // smoothstep
}

void TerrainBrush::expandDirtyRegion(int x, int z)
{
    if (!m_hasDirtyRegion)
    {
        m_dirtyMinX = x;
        m_dirtyMaxX = x;
        m_dirtyMinZ = z;
        m_dirtyMaxZ = z;
        m_hasDirtyRegion = true;
    }
    else
    {
        m_dirtyMinX = std::min(m_dirtyMinX, x);
        m_dirtyMaxX = std::max(m_dirtyMaxX, x);
        m_dirtyMinZ = std::min(m_dirtyMinZ, z);
        m_dirtyMaxZ = std::max(m_dirtyMaxZ, z);
    }
}

} // namespace Vestige
