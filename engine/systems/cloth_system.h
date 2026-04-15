// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_system.h
/// @brief Domain system for cloth and soft body simulation.
#pragma once

#include "core/i_system.h"

#include <string>

namespace Vestige
{

/// @brief Manages cloth and soft body simulation.
///
/// Cloth simulation is entity-component based (ClothComponent instances
/// update through SceneManager). This system provides domain grouping,
/// auto-activation tracking, and performance budget enforcement.
/// Future: GPU compute cloth pipeline.
class ClothSystem : public ISystem
{
public:
    ClothSystem() = default;

    // -- ISystem interface --
    const std::string& getSystemName() const override { return m_name; }
    bool initialize(Engine& engine) override;
    void shutdown() override;
    void update(float deltaTime) override;
    std::vector<uint32_t> getOwnedComponentTypes() const override;

private:
    static inline const std::string m_name = "Cloth";
};

} // namespace Vestige
