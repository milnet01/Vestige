/// @file vegetation_system.cpp
/// @brief VegetationSystem implementation.
#include "systems/vegetation_system.h"
#include "core/engine.h"
#include "core/logger.h"

namespace Vestige
{

bool VegetationSystem::initialize(Engine& engine)
{
    const std::string& assetPath = engine.getAssetPath();

    if (!m_foliageRenderer.init(assetPath))
    {
        Logger::warning("[VegetationSystem] Foliage renderer initialization failed "
                        "— foliage will be unavailable");
    }

    if (!m_treeRenderer.init(assetPath))
    {
        Logger::warning("[VegetationSystem] Tree renderer initialization failed "
                        "— trees will be unavailable");
    }

    Logger::info("[VegetationSystem] Initialized");
    return true;
}

void VegetationSystem::shutdown()
{
    m_treeRenderer.shutdown();
    m_foliageRenderer.shutdown();
    Logger::info("[VegetationSystem] Shut down");
}

void VegetationSystem::update(float /*deltaTime*/)
{
    // Vegetation rendering is driven by the render loop in engine.cpp
}

} // namespace Vestige
