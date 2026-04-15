// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file lighting_system.h
/// @brief Domain system for lighting, shadows, and global illumination.
#pragma once

#include "core/i_system.h"

#include <string>

namespace Vestige
{

/// @brief Manages lighting, shadows, probes, and global illumination.
///
/// Lighting is currently embedded in the Renderer class. This system
/// provides the domain system entry point for auto-activation, performance
/// metrics, and future lighting control APIs. Always force-active since
/// scenes always need lighting.
class LightingSystem : public ISystem
{
public:
    LightingSystem() = default;

    // -- ISystem interface --
    const std::string& getSystemName() const override { return m_name; }
    bool initialize(Engine& engine) override;
    void shutdown() override;
    void update(float deltaTime) override;
    bool isForceActive() const override { return true; }

private:
    static inline const std::string m_name = "Lighting";
};

} // namespace Vestige
