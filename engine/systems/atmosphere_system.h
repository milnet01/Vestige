/// @file atmosphere_system.h
/// @brief Domain system for weather, wind, and atmospheric effects.
#pragma once

#include "core/i_system.h"
#include "environment/environment_forces.h"

#include <string>

namespace Vestige
{

/// @brief Manages weather state, wind field, and atmospheric effects.
///
/// Owns the EnvironmentForces subsystem that provides position-dependent
/// wind queries for cloth, foliage, water, and particles. Always active
/// (force-active) since wind state must advance every frame.
class AtmosphereSystem : public ISystem
{
public:
    AtmosphereSystem() = default;

    // -- ISystem interface --
    const std::string& getSystemName() const override { return m_name; }
    bool initialize(Engine& engine) override;
    void shutdown() override;
    void update(float deltaTime) override;
    bool isForceActive() const override { return true; }

    // -- Accessors --
    EnvironmentForces& getEnvironmentForces() { return m_environmentForces; }
    const EnvironmentForces& getEnvironmentForces() const { return m_environmentForces; }

private:
    static inline const std::string m_name = "Atmosphere";
    EnvironmentForces m_environmentForces;
};

} // namespace Vestige
