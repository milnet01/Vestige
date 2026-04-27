// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file terrain_system.h
/// @brief Domain system for terrain editing, rendering, and collision.
#pragma once

#include "core/i_system.h"
#include "environment/terrain.h"
#include "renderer/terrain_renderer.h"

#include <string>

namespace Vestige
{

/// @brief Manages heightfield terrain, LOD, splatmap texturing, and rendering.
///
/// Owns the Terrain data and TerrainRenderer. Terrain is global (not an
/// entity component), so this system does not declare owned component types.
class TerrainSystem : public ISystem
{
public:
    TerrainSystem() = default;

    // -- ISystem interface --
    const std::string& getSystemName() const override { return m_name; }
    bool initialize(Engine& engine) override;
    void shutdown() override;
    void update(float deltaTime) override;

    /// @brief Phase 10.9 Slice 8 W5 — TerrainSystem owns one global
    ///        heightfield, splatmap, and renderer rather than per-entity
    ///        components. With no owned component types, the
    ///        scene-has-no-owned-components heuristic would deactivate
    ///        a system that the scene very much needs (terrain LOD
    ///        update, GPU upload, brush sculpt commit). Force active.
    bool isForceActive() const override { return true; }

    // -- Accessors --
    Terrain& getTerrain() { return m_terrain; }
    const Terrain& getTerrain() const { return m_terrain; }
    TerrainRenderer& getTerrainRenderer() { return m_terrainRenderer; }
    const TerrainRenderer& getTerrainRenderer() const { return m_terrainRenderer; }

private:
    static inline const std::string m_name = "Terrain";
    Terrain m_terrain;
    TerrainRenderer m_terrainRenderer;
};

} // namespace Vestige
