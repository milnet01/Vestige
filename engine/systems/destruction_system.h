// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file destruction_system.h
/// @brief Domain system for destruction, fracture, and physics objects.
#pragma once

#include "core/i_system.h"

#include <string>

namespace Vestige
{

/// @brief Manages destructible objects, fracture, dismemberment, and rigid bodies.
///
/// Physics entities are component-based (BreakableComponent, RigidBody).
/// PhysicsWorld stays in Engine as shared infrastructure. This system
/// provides domain grouping, auto-activation, and performance metrics.
class DestructionSystem : public ISystem
{
public:
    DestructionSystem() = default;

    // -- ISystem interface --
    const std::string& getSystemName() const override { return m_name; }
    bool initialize(Engine& engine) override;
    void shutdown() override;
    void update(float deltaTime) override;
    std::vector<uint32_t> getOwnedComponentTypes() const override;

private:
    static inline const std::string m_name = "Destruction";
};

} // namespace Vestige
