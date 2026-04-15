// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file terrain_system.cpp
/// @brief TerrainSystem implementation.
#include "systems/terrain_system.h"
#include "core/engine.h"
#include "core/logger.h"

#include <algorithm>
#include <cmath>

namespace Vestige
{

bool TerrainSystem::initialize(Engine& engine)
{
    const std::string& assetPath = engine.getAssetPath();

    if (!m_terrainRenderer.init(assetPath))
    {
        Logger::warning("[TerrainSystem] Terrain renderer initialization failed "
                        "— terrain will be unavailable");
    }

    // Initialize terrain with default config
    TerrainConfig terrainConfig;
    terrainConfig.width = 257;
    terrainConfig.depth = 257;
    terrainConfig.spacingX = 1.0f;
    terrainConfig.spacingZ = 1.0f;
    terrainConfig.heightScale = 50.0f;
    terrainConfig.origin = glm::vec3(-128.0f, 0.0f, -128.0f);
    terrainConfig.gridResolution = 33;
    terrainConfig.maxLodLevels = 6;
    terrainConfig.baseLodDistance = 20.0f;

    if (m_terrain.initialize(terrainConfig))
    {
        int w = terrainConfig.width;
        int d = terrainConfig.depth;
        float originX = terrainConfig.origin.x;
        float originZ = terrainConfig.origin.z;
        for (int z = 0; z < d; ++z)
        {
            for (int x = 0; x < w; ++x)
            {
                float nx = static_cast<float>(x) / static_cast<float>(w - 1);
                float nz = static_cast<float>(z) / static_cast<float>(d - 1);
                float wx = originX + static_cast<float>(x) * terrainConfig.spacingX;
                float wz = originZ + static_cast<float>(z) * terrainConfig.spacingZ;

                float h = 0.0f;
                h += 0.15f * std::sin(nx * 6.28f * 2.0f) * std::cos(nz * 6.28f * 1.5f);
                h += 0.08f * std::sin(nx * 6.28f * 5.0f + 1.0f) * std::sin(nz * 6.28f * 4.0f);
                h += 0.04f * std::sin(nx * 6.28f * 11.0f) * std::cos(nz * 6.28f * 9.0f + 2.0f);
                h = std::max(h, 0.0f);

                float distFromOrigin = std::sqrt(wx * wx + wz * wz);
                float flatRadius = 18.0f;
                float blendRadius = 35.0f;
                if (distFromOrigin < blendRadius)
                {
                    float t = std::max(0.0f, (distFromOrigin - flatRadius)
                                            / (blendRadius - flatRadius));
                    h *= t * t;
                }

                m_terrain.setRawHeight(x, z, h);
            }
        }

        m_terrain.updateHeightmapRegion(0, 0, w, d);
        m_terrain.updateNormalMapRegion(0, 0, w, d);
        m_terrain.buildQuadtree();
    }
    else
    {
        Logger::warning("[TerrainSystem] Terrain initialization failed");
    }

    Logger::info("[TerrainSystem] Initialized");
    return true;
}

void TerrainSystem::shutdown()
{
    m_terrain.shutdown();
    m_terrainRenderer.shutdown();
    Logger::info("[TerrainSystem] Shut down");
}

void TerrainSystem::update(float /*deltaTime*/)
{
    // Terrain rendering is driven by the render loop in engine.cpp
}

} // namespace Vestige
