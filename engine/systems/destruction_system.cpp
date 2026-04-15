// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file destruction_system.cpp
/// @brief DestructionSystem implementation.
#include "systems/destruction_system.h"
#include "core/engine.h"
#include "core/logger.h"
#include "physics/breakable_component.h"
#include "physics/rigid_body.h"
#include "scene/component.h"

namespace Vestige
{

bool DestructionSystem::initialize(Engine& /*engine*/)
{
    // Physics managed by PhysicsWorld (shared infrastructure in Engine)
    Logger::info("[DestructionSystem] Initialized");
    return true;
}

void DestructionSystem::shutdown()
{
    Logger::info("[DestructionSystem] Shut down");
}

void DestructionSystem::update(float /*deltaTime*/)
{
    // Physics updates through PhysicsWorld in engine main loop
}

std::vector<uint32_t> DestructionSystem::getOwnedComponentTypes() const
{
    return {
        ComponentTypeId::get<BreakableComponent>(),
        ComponentTypeId::get<RigidBody>()
    };
}

} // namespace Vestige
