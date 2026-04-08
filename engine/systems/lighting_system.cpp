/// @file lighting_system.cpp
/// @brief LightingSystem implementation.
#include "systems/lighting_system.h"
#include "core/engine.h"
#include "core/logger.h"

namespace Vestige
{

bool LightingSystem::initialize(Engine& /*engine*/)
{
    // Lighting is embedded in Renderer — no separate initialization
    Logger::info("[LightingSystem] Initialized");
    return true;
}

void LightingSystem::shutdown()
{
    Logger::info("[LightingSystem] Shut down");
}

void LightingSystem::update(float /*deltaTime*/)
{
    // Lighting is managed by Renderer internally
}

} // namespace Vestige
