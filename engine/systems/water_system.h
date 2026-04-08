/// @file water_system.h
/// @brief Domain system for water surfaces, rendering, and fluid effects.
#pragma once

#include "core/i_system.h"
#include "renderer/water_renderer.h"
#include "renderer/water_fbo.h"

#include <string>

namespace Vestige
{

/// @brief Manages water surface rendering and fluid effects.
///
/// Owns WaterRenderer and WaterFbo. Water surfaces are entity components
/// (WaterSurface) that provide per-surface configuration; this system
/// handles the shared rendering infrastructure.
class WaterSystem : public ISystem
{
public:
    WaterSystem() = default;

    // -- ISystem interface --
    const std::string& getSystemName() const override { return m_name; }
    bool initialize(Engine& engine) override;
    void shutdown() override;
    void update(float deltaTime) override;
    std::vector<uint32_t> getOwnedComponentTypes() const override;

    // -- Accessors --
    WaterRenderer& getWaterRenderer() { return m_waterRenderer; }
    const WaterRenderer& getWaterRenderer() const { return m_waterRenderer; }
    WaterFbo& getWaterFbo() { return m_waterFbo; }
    const WaterFbo& getWaterFbo() const { return m_waterFbo; }

private:
    static inline const std::string m_name = "Water";
    WaterRenderer m_waterRenderer;
    WaterFbo m_waterFbo;
};

} // namespace Vestige
