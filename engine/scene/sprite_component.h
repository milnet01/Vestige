// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file sprite_component.h
/// @brief SpriteComponent — 2D sprite attached to an entity (Phase 9F-1).
///
/// A sprite is the 2D analogue of MeshRenderer: it references a frame in a
/// shared atlas, sits in the scene at the entity's Transform, and is drawn
/// by SpriteSystem in a single batched pass.
///
/// Keep the data model narrow for Phase 9F-1:
/// - one atlas per sprite (batching groups by atlas texture id)
/// - one current frame name (driven by SpriteAnimation if attached)
/// - tint + pivot + flips for artist control
/// - sortingLayer + orderInLayer + yFromBottom for z-order (Unity model)
///
/// Materials with normal maps / emission are Phase 18.
#pragma once

#include "animation/sprite_animation.h"
#include "scene/component.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>

namespace Vestige
{

class SpriteAtlas;

/// @brief 2D sprite component — atlas frame reference + artist controls.
class SpriteComponent : public Component
{
public:
    SpriteComponent() = default;
    ~SpriteComponent() override = default;

    void update(float deltaTime) override;
    std::unique_ptr<Component> clone() const override;

    /// @brief Atlas providing the frame for this sprite. Shared — multiple
    /// sprites typically point at the same atlas to keep batching tight.
    std::shared_ptr<SpriteAtlas> atlas;

    /// @brief Name of the frame to render. Updated automatically each
    /// frame if @ref animation is non-null.
    std::string frameName;

    /// @brief Multiplicative colour tint (1,1,1,1 = unchanged).
    glm::vec4 tint = glm::vec4(1.0f);

    /// @brief Pivot in normalised 0..1 of the sprite's source size.
    /// (0,0) = top-left, (0.5, 0.5) = centre, (1,1) = bottom-right.
    glm::vec2 pivot = glm::vec2(0.5f, 0.5f);

    /// @brief Flip the frame on the X axis (mirrors horizontally).
    bool flipX = false;

    /// @brief Flip the frame on the Y axis.
    bool flipY = false;

    /// @brief Pixels per world unit. 100 matches Unity's default —
    /// a 100-pixel-tall sprite becomes 1 metre.
    float pixelsPerUnit = 100.0f;

    /// @brief Coarse sort key — larger layers draw on top.
    int sortingLayer = 0;

    /// @brief Fine sort key within a layer — larger orders draw on top.
    int orderInLayer = 0;

    /// @brief When true, SpriteSystem breaks ties within a layer using
    /// the entity's world-space `y` (smaller y = further back, draws
    /// first). Standard top-down / isometric behaviour.
    bool sortByY = false;

    /// @brief When true, the sprite is drawn in the transparent pass
    /// (back-to-front, no depth write). False for fully-opaque / alpha-
    /// tested sprites that can participate in the depth buffer.
    bool isTransparent = true;

    /// @brief Optional animation state machine. When set, `tick()` drives
    /// `frameName` each update.
    std::shared_ptr<SpriteAnimation> animation;
};

} // namespace Vestige
