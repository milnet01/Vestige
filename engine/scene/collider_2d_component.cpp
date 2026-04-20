// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file collider_2d_component.cpp
/// @brief Collider2DComponent implementation.
#include "scene/collider_2d_component.h"

namespace Vestige
{

void Collider2DComponent::update(float /*deltaTime*/)
{
    // Collider is authoring-only. Physics2DSystem rebuilds the Jolt
    // shape when the component is marked dirty (TODO: hot-edit support
    // in Phase 9F-6's editor panel).
}

std::unique_ptr<Component> Collider2DComponent::clone() const
{
    auto c = std::make_unique<Collider2DComponent>();
    c->shape       = shape;
    c->halfExtents = halfExtents;
    c->radius      = radius;
    c->height      = height;
    c->vertices    = vertices;
    c->isSensor    = isSensor;
    c->zThickness  = zThickness;
    c->zOffset     = zOffset;
    c->setEnabled(m_isEnabled);
    return c;
}

} // namespace Vestige
