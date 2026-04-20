// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file sprite_component.cpp
/// @brief SpriteComponent implementation.
#include "scene/sprite_component.h"

namespace Vestige
{

void SpriteComponent::update(float deltaTime)
{
    if (!m_isEnabled)
    {
        return;
    }
    if (animation)
    {
        animation->tick(deltaTime);
        const std::string& name = animation->currentFrameName();
        if (!name.empty())
        {
            frameName = name;
        }
    }
}

std::unique_ptr<Component> SpriteComponent::clone() const
{
    auto c = std::make_unique<SpriteComponent>();
    c->atlas         = atlas;        // shared_ptr — shared ownership is correct
    c->frameName     = frameName;
    c->tint          = tint;
    c->pivot         = pivot;
    c->flipX         = flipX;
    c->flipY         = flipY;
    c->pixelsPerUnit = pixelsPerUnit;
    c->sortingLayer  = sortingLayer;
    c->orderInLayer  = orderInLayer;
    c->sortByY       = sortByY;
    c->isTransparent = isTransparent;
    // Animation state: clone the state machine so cloned entities play
    // independently. A shared_ptr copy would link playback across twins.
    if (animation)
    {
        c->animation = std::make_shared<SpriteAnimation>(*animation);
    }
    c->setEnabled(m_isEnabled);
    return c;
}

} // namespace Vestige
