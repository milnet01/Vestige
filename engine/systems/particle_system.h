// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file particle_system.h
/// @brief Domain system for CPU/GPU particle rendering and VFX.
#pragma once

#include "core/i_system.h"
#include "renderer/particle_renderer.h"

#include <string>

namespace Vestige
{

/// @brief Manages particle rendering and VFX.
///
/// Owns the ParticleRenderer. Particle emitters are entity components
/// (ParticleEmitter, GpuParticleEmitter) that update through the scene
/// manager; this system handles renderer lifecycle and auto-activation.
class ParticleVfxSystem : public ISystem
{
public:
    ParticleVfxSystem() = default;

    // -- ISystem interface --
    const std::string& getSystemName() const override { return m_name; }
    bool initialize(Engine& engine) override;
    void shutdown() override;
    void update(float deltaTime) override;
    std::vector<uint32_t> getOwnedComponentTypes() const override;

    // -- Accessors --
    ParticleRenderer& getParticleRenderer() { return m_particleRenderer; }
    const ParticleRenderer& getParticleRenderer() const { return m_particleRenderer; }

private:
    static inline const std::string m_name = "ParticleVFX";
    ParticleRenderer m_particleRenderer;
};

} // namespace Vestige
