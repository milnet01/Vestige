/// @file character_system.cpp
/// @brief CharacterSystem implementation.
#include "systems/character_system.h"
#include "core/engine.h"
#include "core/logger.h"
#include "renderer/camera.h"

namespace Vestige
{

bool CharacterSystem::initialize(Engine& engine)
{
    if (!engine.getPhysicsWorld().isInitialized())
    {
        Logger::info("[CharacterSystem] Physics not available — controller disabled");
        return true;
    }

    PhysicsControllerConfig physCtrlConfig;
    physCtrlConfig.eyeHeight = 1.7f;    // matches ControllerConfig defaults
    physCtrlConfig.maxSlopeAngle = 50.0f;

    glm::vec3 startFeet = engine.getCamera().getPosition();
    startFeet.y -= physCtrlConfig.eyeHeight;  // Camera position -> feet position

    if (!m_physicsCharController.initialize(engine.getPhysicsWorld(), startFeet, physCtrlConfig))
    {
        Logger::warning("[CharacterSystem] Physics character controller initialization failed");
    }

    Logger::info("[CharacterSystem] Initialized");
    return true;
}

void CharacterSystem::shutdown()
{
    m_physicsCharController.shutdown();
    Logger::info("[CharacterSystem] Shut down");
}

void CharacterSystem::update(float /*deltaTime*/)
{
    // Controller update stays in engine.cpp main loop
    // (tightly coupled with Camera and FirstPersonController)
}

} // namespace Vestige
