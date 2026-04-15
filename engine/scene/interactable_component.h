// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file interactable_component.h
/// @brief Component marking entities as interactable (grabbable, pushable, toggleable).
#pragma once

#include "scene/component.h"

#include <memory>
#include <string>

namespace Vestige
{

/// @brief Type of interaction supported.
enum class InteractionType : uint8_t
{
    GRAB,     ///< Can be picked up, carried, and thrown
    PUSH,     ///< Can be pushed but not picked up
    TOGGLE    ///< Activates/deactivates on use (doors, switches)
};

/// @brief Marks an entity as interactable by the GrabSystem.
class InteractableComponent : public Component
{
public:
    InteractableComponent() = default;
    ~InteractableComponent() override = default;

    void update(float deltaTime) override;
    std::unique_ptr<Component> clone() const override;

    /// @brief Type of interaction.
    InteractionType type = InteractionType::GRAB;

    /// @brief Maximum mass the player can pick up (kg).
    float maxGrabMass = 50.0f;

    /// @brief Impulse multiplier when throwing.
    float throwForce = 10.0f;

    /// @brief Maximum reach distance for interaction (meters).
    float grabDistance = 3.0f;

    /// @brief Distance from camera when object is held (meters).
    float holdDistance = 1.5f;

    /// @brief Spring frequency for the hold constraint (Hz). Higher = stiffer.
    float holdSpringFrequency = 8.0f;

    /// @brief Spring damping for the hold constraint. 1 = critical damping.
    float holdSpringDamping = 1.0f;

    /// @brief Whether this entity is currently highlighted (looked at).
    bool highlighted = false;

    /// @brief Optional interaction prompt text.
    std::string promptText = "Grab";
};

} // namespace Vestige
