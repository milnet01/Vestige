// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file pressure_plate_component.h
/// @brief Physics puzzle trigger component -- activates when objects are placed on it.
#pragma once

#include "scene/component.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Vestige
{

class PhysicsWorld;

/// @brief A trigger volume that detects when physics objects overlap it.
///
/// Use for pressure plates, trap triggers, puzzle activation zones.
/// The component uses periodic sphere overlap queries via Jolt's narrow-phase
/// to detect dynamic bodies within a configurable radius.
class PressurePlateComponent : public Component
{
public:
    PressurePlateComponent() = default;
    ~PressurePlateComponent() override = default;

    void update(float deltaTime) override;
    std::unique_ptr<Component> clone() const override;

    /// @brief Set the physics world reference for overlap queries.
    void setPhysicsWorld(PhysicsWorld* world) { m_physicsWorld = world; }

    /// @brief Check if the plate is currently activated.
    bool isActivated() const { return m_activated; }

    /// @brief Get the number of overlapping bodies.
    size_t getOverlapCount() const { return m_overlappingBodies.size(); }

    /// @brief Callback when plate activates (first body enters).
    std::function<void()> onActivate;

    /// @brief Callback when plate deactivates (all bodies leave).
    std::function<void()> onDeactivate;

    // Configuration
    float detectionRadius = 1.0f;    ///< Radius of the detection sphere (meters).
    float detectionHeight = 0.5f;    ///< Height offset of detection center above entity.
    float queryInterval = 0.1f;      ///< Seconds between overlap queries.
    bool inverted = false;           ///< If true, activated when NO bodies overlap.

private:
    PhysicsWorld* m_physicsWorld = nullptr;
    bool m_activated = false;
    float m_timeSinceLastQuery = 0.0f;
    std::vector<uint32_t> m_overlappingBodies;  ///< Body IDs currently overlapping.
};

} // namespace Vestige
