// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file sprite_system.h
/// @brief SpriteSystem — ISystem owning the 2D sprite rendering pass
/// (Phase 9F-1).
///
/// Sits alongside UISystem and the 3D Renderer. Each frame:
/// 1. `update()`   ticks every SpriteComponent's animation (via the
///                  component's own Component::update, invoked by the
///                  scene traversal).
/// 2. `render()`   gathers visible SpriteComponents, sorts by
///                 (layer, order, y, entity-id), packs into batches
///                 keyed on the atlas texture, and issues one instanced
///                 draw per batch.
///
/// The pass runs after the 3D post-process / tonemap (LDR main
/// framebuffer) and before the UISystem HUD overlay.
///
/// All sort / pack logic is headless — tests drive it without a GL
/// context by calling @ref buildDrawList with a fabricated entity list.
#pragma once

#include "core/i_system.h"
#include "renderer/sprite_renderer.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Vestige
{

class Entity;
class Scene;
class SpriteComponent;

/// @brief One entry in the sprite draw list, pre-sort.
struct SpriteDrawEntry
{
    const SpriteComponent* component = nullptr;
    glm::mat4 worldMatrix = glm::mat4(1.0f);
    int       sortingLayer = 0;
    int       orderInLayer = 0;
    float     yForSort     = 0.0f;    ///< World-space Y (reversed in sort).
    uint32_t  entityId     = 0;       ///< Stable tiebreaker for tests.
};

/// @brief Render-pass kind. Used to separate opaque vs. transparent
/// sprites; the two passes have different depth-write / blend state.
enum class SpritePass
{
    Opaque,
    Transparent
};

class SpriteSystem : public ISystem
{
public:
    SpriteSystem() = default;

    // -- ISystem --
    const std::string& getSystemName() const override { return m_name; }
    bool initialize(Engine& engine) override;
    void shutdown() override;
    void update(float deltaTime) override;

    /// @brief Issues the sprite pass. Call once per frame after the 3D
    /// post-process, before the UI overlay. No-op in headless test builds
    /// (SpriteRenderer::isInitialized() returns false).
    /// @param viewProj Combined view-projection for the 2D camera. For a
    /// 3D-camera scene an orthographic projection matching the viewport
    /// produces screen-space sprites.
    void render(const glm::mat4& viewProj);

    /// @brief Inspector accessor for the underlying renderer.
    SpriteRenderer& getRenderer() { return m_renderer; }

    // -- Headless (test-facing) --

    /// @brief Walks @p scene and collects visible SpriteComponents into
    /// @p out. Skips disabled components and invisible entities.
    /// @return Number of entries pushed.
    static std::size_t collectVisible(const Scene& scene,
                                      std::vector<SpriteDrawEntry>& out);

    /// @brief Orders entries in-place using the Unity 2D sort: stable
    /// sort by (sortingLayer asc, orderInLayer asc, -yForSort when
    /// component->sortByY is set, entityId asc).
    static void sortDrawList(std::vector<SpriteDrawEntry>& entries);

    /// @brief Packs a sorted list of entries into SpriteInstance records,
    /// splitting by (atlas, pass) into separate batches. Depth is
    /// distributed linearly from 0.99 (back) to 0.999 (front) within
    /// the list.
    struct Batch
    {
        const void*               atlas = nullptr;   ///< SpriteAtlas pointer
        SpritePass                pass  = SpritePass::Transparent;
        std::vector<SpriteInstance> instances;
    };
    static std::vector<Batch> buildBatches(const std::vector<SpriteDrawEntry>& entries);

private:
    static inline const std::string m_name = "Sprite";
    Engine* m_engine = nullptr;
    SpriteRenderer m_renderer;
};

} // namespace Vestige
