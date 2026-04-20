// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file rigid_body_2d_component.cpp
/// @brief RigidBody2DComponent implementation.
#include "scene/rigid_body_2d_component.h"

namespace Vestige
{

void RigidBody2DComponent::update(float /*deltaTime*/)
{
    // Physics2DSystem owns the simulation and writes cached state into
    // linearVelocity / angularVelocity after each step.
}

std::unique_ptr<Component> RigidBody2DComponent::clone() const
{
    auto c = std::make_unique<RigidBody2DComponent>();
    c->type           = type;
    c->mass           = mass;
    c->friction       = friction;
    c->restitution    = restitution;
    c->linearDamping  = linearDamping;
    c->angularDamping = angularDamping;
    c->gravityScale   = gravityScale;
    c->fixedRotation  = fixedRotation;
    c->categoryBits   = categoryBits;
    c->maskBits       = maskBits;
    // Runtime state (bodyId, velocities) is not cloned — the clone gets
    // its own body at next Physics2DSystem::onSceneLoad.
    c->setEnabled(m_isEnabled);
    return c;
}

} // namespace Vestige
