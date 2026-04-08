/// @file cloth_system.cpp
/// @brief ClothSystem implementation.
#include "systems/cloth_system.h"
#include "core/engine.h"
#include "core/logger.h"
#include "physics/cloth_component.h"
#include "scene/component.h"

namespace Vestige
{

bool ClothSystem::initialize(Engine& /*engine*/)
{
    // Cloth is entity-component based — no explicit initialization needed
    Logger::info("[ClothSystem] Initialized");
    return true;
}

void ClothSystem::shutdown()
{
    Logger::info("[ClothSystem] Shut down");
}

void ClothSystem::update(float /*deltaTime*/)
{
    // Cloth updates through entity component system (SceneManager)
}

std::vector<uint32_t> ClothSystem::getOwnedComponentTypes() const
{
    return { ComponentTypeId::get<ClothComponent>() };
}

} // namespace Vestige
