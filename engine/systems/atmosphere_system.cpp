/// @file atmosphere_system.cpp
/// @brief AtmosphereSystem implementation.
#include "systems/atmosphere_system.h"
#include "core/engine.h"
#include "core/logger.h"

namespace Vestige
{

bool AtmosphereSystem::initialize(Engine& /*engine*/)
{
    // EnvironmentForces is fully initialized by its constructor (no GL resources)
    Logger::info("[AtmosphereSystem] Initialized");
    return true;
}

void AtmosphereSystem::shutdown()
{
    // No GL resources to release
    Logger::info("[AtmosphereSystem] Shut down");
}

void AtmosphereSystem::update(float deltaTime)
{
    m_environmentForces.update(deltaTime);
}

} // namespace Vestige
