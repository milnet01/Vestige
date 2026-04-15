// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file particle_system.cpp
/// @brief ParticleVfxSystem implementation.
#include "systems/particle_system.h"
#include "core/engine.h"
#include "core/logger.h"
#include "scene/component.h"
#include "scene/particle_emitter.h"
#include "scene/gpu_particle_emitter.h"

namespace Vestige
{

bool ParticleVfxSystem::initialize(Engine& engine)
{
    if (!m_particleRenderer.init(engine.getAssetPath()))
    {
        Logger::warning("[ParticleVfxSystem] Particle renderer initialization failed "
                        "— particles will be unavailable");
    }
    Logger::info("[ParticleVfxSystem] Initialized");
    return true;
}

void ParticleVfxSystem::shutdown()
{
    m_particleRenderer.shutdown();
    Logger::info("[ParticleVfxSystem] Shut down");
}

void ParticleVfxSystem::update(float /*deltaTime*/)
{
    // Particles update through entity component system (SceneManager)
}

std::vector<uint32_t> ParticleVfxSystem::getOwnedComponentTypes() const
{
    return {
        ComponentTypeId::get<ParticleEmitterComponent>(),
        ComponentTypeId::get<GPUParticleEmitter>()
    };
}

} // namespace Vestige
