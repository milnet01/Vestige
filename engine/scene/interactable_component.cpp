// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file interactable_component.cpp
/// @brief InteractableComponent implementation.

#include "scene/interactable_component.h"

namespace Vestige
{

void InteractableComponent::update(float /*deltaTime*/)
{
    // Highlight state is managed externally by GrabSystem
}

std::unique_ptr<Component> InteractableComponent::clone() const
{
    auto copy = std::make_unique<InteractableComponent>();
    copy->type = type;
    copy->maxGrabMass = maxGrabMass;
    copy->throwForce = throwForce;
    copy->grabDistance = grabDistance;
    copy->holdDistance = holdDistance;
    copy->holdSpringFrequency = holdSpringFrequency;
    copy->holdSpringDamping = holdSpringDamping;
    copy->promptText = promptText;
    return copy;
}

} // namespace Vestige
