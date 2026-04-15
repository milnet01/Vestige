// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file vegetation_system.h
/// @brief Domain system for foliage, trees, and vegetation rendering.
#pragma once

#include "core/i_system.h"
#include "environment/foliage_manager.h"
#include "renderer/foliage_renderer.h"
#include "renderer/tree_renderer.h"

#include <string>

namespace Vestige
{

/// @brief Manages vegetation placement, LOD, and rendering.
///
/// Owns FoliageManager, FoliageRenderer, and TreeRenderer. Vegetation
/// is environment-based (not entity components), so this system does
/// not declare owned component types for auto-activation.
class VegetationSystem : public ISystem
{
public:
    VegetationSystem() = default;

    // -- ISystem interface --
    const std::string& getSystemName() const override { return m_name; }
    bool initialize(Engine& engine) override;
    void shutdown() override;
    void update(float deltaTime) override;

    // -- Accessors --
    FoliageManager& getFoliageManager() { return m_foliageManager; }
    const FoliageManager& getFoliageManager() const { return m_foliageManager; }
    FoliageRenderer& getFoliageRenderer() { return m_foliageRenderer; }
    const FoliageRenderer& getFoliageRenderer() const { return m_foliageRenderer; }
    TreeRenderer& getTreeRenderer() { return m_treeRenderer; }
    const TreeRenderer& getTreeRenderer() const { return m_treeRenderer; }

private:
    static inline const std::string m_name = "Vegetation";
    FoliageManager m_foliageManager;
    FoliageRenderer m_foliageRenderer;
    TreeRenderer m_treeRenderer;
};

} // namespace Vestige
