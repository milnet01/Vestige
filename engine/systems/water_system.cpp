/// @file water_system.cpp
/// @brief WaterSystem implementation.
#include "systems/water_system.h"
#include "core/engine.h"
#include "core/logger.h"
#include "scene/water_surface.h"
#include "scene/component.h"

namespace Vestige
{

bool WaterSystem::initialize(Engine& engine)
{
    const std::string& assetPath = engine.getAssetPath();
    if (!m_waterRenderer.init(assetPath))
    {
        Logger::warning("[WaterSystem] Water renderer initialization failed "
                        "— water surfaces will be unavailable");
    }

    // Water FBOs at 25% resolution
    int w = engine.getWindow().getWidth();
    int h = engine.getWindow().getHeight();
    m_waterFbo.init(w / 4, h / 4, w / 4, h / 4);

    Logger::info("[WaterSystem] Initialized");
    return true;
}

void WaterSystem::shutdown()
{
    m_waterFbo.shutdown();
    m_waterRenderer.shutdown();
    Logger::info("[WaterSystem] Shut down");
}

void WaterSystem::update(float /*deltaTime*/)
{
    // Water rendering is driven by the render loop in engine.cpp
}

std::vector<uint32_t> WaterSystem::getOwnedComponentTypes() const
{
    return { ComponentTypeId::get<WaterSurfaceComponent>() };
}

} // namespace Vestige
