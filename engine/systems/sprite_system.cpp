// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file sprite_system.cpp
/// @brief SpriteSystem implementation.
#include "systems/sprite_system.h"
#include "core/engine.h"
#include "core/logger.h"
#include "renderer/sprite_atlas.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "scene/scene_manager.h"
#include "scene/sprite_component.h"

#include <algorithm>

namespace Vestige
{

namespace
{
/// @brief Remaps a normalised sort position (0..1) into the depth range
/// sprites occupy in the shared depth buffer. The range is empirical —
/// [0.99, 0.999] sits just in front of cleared depth (far plane = 1.0)
/// yet leaves headroom for the UI overlay that draws in clip-space.
constexpr float kSpriteDepthMin = 0.99f;
constexpr float kSpriteDepthMax = 0.999f;

/// @brief Builds the 2x3 affine rows used by the shader from a world
/// matrix + per-sprite pivot / flips / scale. The sprite mesh is a unit
/// quad at origin; this transform moves the quad to the entity pose and
/// stretches it to the frame's pixel size divided by pixelsPerUnit.
void composeAffine(const glm::mat4& world,
                   const SpriteComponent& sc,
                   const glm::vec2& sizeUnits,
                   SpriteInstance& out)
{
    // World rotation + scale are the upper-left 2x2 of the model matrix.
    // Multiply by sprite size so the unit quad stretches to world size.
    glm::mat2 basis = glm::mat2(world[0].x, world[0].y,
                                world[1].x, world[1].y);
    glm::vec2 sx = basis[0] * (sc.flipX ? -sizeUnits.x : sizeUnits.x);
    glm::vec2 sy = basis[1] * (sc.flipY ? -sizeUnits.y : sizeUnits.y);

    // Pivot shifts the centred unit quad so the entity's origin lands at
    // the artist's pivot. The vertex shader centres on (0.5, 0.5) already,
    // so the pivot offset is (pivot - 0.5) in local sprite space.
    glm::vec2 pivotOffset = (sc.pivot - glm::vec2(0.5f, 0.5f)) * sizeUnits;
    if (sc.flipX) { pivotOffset.x = -pivotOffset.x; }
    if (sc.flipY) { pivotOffset.y = -pivotOffset.y; }
    glm::vec2 pivotWorld = basis * pivotOffset;

    glm::vec2 translation = glm::vec2(world[3].x, world[3].y) - pivotWorld;

    out.transformRow0 = glm::vec4(sx.x, sy.x, translation.x, 0.0f);
    out.transformRow1 = glm::vec4(sx.y, sy.y, translation.y, 0.0f);
}

} // namespace

bool SpriteSystem::initialize(Engine& engine)
{
    m_engine = &engine;
    // Asset path convention matches UISystem / renderer — "assets" at the
    // working directory root. Headless tests skip initialize().
    if (!m_renderer.initialize("assets"))
    {
        Logger::warning("[SpriteSystem] Renderer init failed — running "
                        "headless (no sprite pass).");
    }
    Logger::info("[SpriteSystem] Initialized");
    return true;
}

void SpriteSystem::shutdown()
{
    m_renderer.shutdown();
}

void SpriteSystem::update(float /*deltaTime*/)
{
    // SpriteComponent::update drives animation per-entity, invoked during
    // Scene::update's traversal. No additional per-system work here.
}

void SpriteSystem::render(const glm::mat4& viewProj)
{
    if (!m_renderer.isInitialized() || !m_engine)
    {
        return;
    }
    Scene* scene = m_engine->getSceneManager().getActiveScene();
    if (!scene)
    {
        return;
    }

    // Phase 10.9 Slice 13 Pe2 — reuse the member scratch vectors instead of
    // constructing fresh ones each frame. clear() preserves capacity, so the
    // first non-empty frame allocates and subsequent frames push into the
    // already-sized buffer with zero allocations.
    m_entriesScratch.clear();
    collectVisible(*scene, m_entriesScratch);
    if (m_entriesScratch.empty())
    {
        return;
    }
    sortDrawList(m_entriesScratch);

    buildBatches(m_entriesScratch, m_batchesScratch);

    m_renderer.begin(viewProj);
    for (const auto& batch : m_batchesScratch)
    {
        const auto* atlas = static_cast<const SpriteAtlas*>(batch.atlas);
        if (!atlas)
        {
            continue;
        }
        m_renderer.drawBatch(*atlas, batch.instances,
                             batch.pass == SpritePass::Transparent);
    }
    m_renderer.end();
}

std::size_t SpriteSystem::collectVisible(const Scene& scene,
                                         std::vector<SpriteDrawEntry>& out)
{
    const std::size_t before = out.size();
    scene.forEachEntity([&out](const Entity& e) {
        if (!e.isActive() || !e.isVisible())
        {
            return;
        }
        if (const auto* sc = e.getComponent<SpriteComponent>())
        {
            if (sc->isEnabled() && sc->atlas)
            {
                SpriteDrawEntry entry;
                entry.component = sc;
                entry.worldMatrix = e.getWorldMatrix();
                entry.sortingLayer = sc->sortingLayer;
                entry.orderInLayer = sc->orderInLayer;
                entry.yForSort = entry.worldMatrix[3].y;
                entry.entityId = e.getId();
                out.push_back(entry);
            }
        }
    });
    return out.size() - before;
}

void SpriteSystem::sortDrawList(std::vector<SpriteDrawEntry>& entries)
{
    std::stable_sort(entries.begin(), entries.end(),
        [](const SpriteDrawEntry& a, const SpriteDrawEntry& b)
        {
            if (a.sortingLayer != b.sortingLayer) return a.sortingLayer < b.sortingLayer;
            if (a.orderInLayer != b.orderInLayer) return a.orderInLayer < b.orderInLayer;

            // Y-sort: only apply when both sprites opt in. Lower y draws
            // first (further back on screen in top-down / isometric).
            const bool ys = a.component && a.component->sortByY &&
                            b.component && b.component->sortByY;
            if (ys && a.yForSort != b.yForSort)
            {
                return a.yForSort > b.yForSort;  // larger y = farther back = earlier
            }

            return a.entityId < b.entityId;
        });
}

void SpriteSystem::buildBatches(const std::vector<SpriteDrawEntry>& entries,
                                std::vector<Batch>& outBatches)
{
    // Phase 10.9 Slice 13 Pe2 — clear first so the per-batch instance
    // vectors get released; capacity of the outer batches vector is
    // preserved for reuse across frames. Per-batch `instances` capacity
    // is also retained because `Batch` lives inside the outer container,
    // and emplace into a slot we already own (when the new frame's batch
    // count is ≤ the previous frame's) avoids a fresh allocation for the
    // common case of a stable scene.
    outBatches.clear();
    if (entries.empty())
    {
        return;
    }

    // Map sort position (index within entries) → depth value in
    // [kSpriteDepthMin, kSpriteDepthMax]. Sorted draw order runs back to
    // front, so earlier indices map to farther depth values.
    const float denom = static_cast<float>(std::max<std::size_t>(entries.size(), 2) - 1);

    auto passOf = [](const SpriteComponent& sc)
    {
        return sc.isTransparent ? SpritePass::Transparent : SpritePass::Opaque;
    };

    // A new batch is emitted whenever (atlas, pass) changes. Because
    // entries are already sorted by (layer, order, y), this does not
    // re-order sprites — it just groups runs.
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries[i];
        if (!e.component || !e.component->atlas)
        {
            continue;
        }
        const auto* atlas = e.component->atlas.get();
        const SpritePass pass = passOf(*e.component);

        if (outBatches.empty() ||
            outBatches.back().atlas != atlas ||
            outBatches.back().pass != pass)
        {
            outBatches.push_back(Batch{atlas, pass, {}});
        }

        const auto* frame = atlas->find(e.component->frameName);
        if (!frame)
        {
            // Missing frame — skip but don't fail the batch; keeps the
            // render loop robust against stale frame names (e.g. during
            // asset hot-reload).
            continue;
        }

        const glm::vec2 sizeUnits = frame->sourceSize / e.component->pixelsPerUnit;
        SpriteInstance inst{};
        composeAffine(e.worldMatrix, *e.component, sizeUnits, inst);
        inst.uvRect = frame->uv;
        inst.tint   = e.component->tint;

        const float t = denom > 0.0f
                            ? static_cast<float>(i) / denom
                            : 0.5f;
        // Invert so earlier (back) sprites get the smaller GL depth value
        // and later (front) sprites get the larger — standard back-to-front
        // order with LEQUAL depth test.
        inst.depth = kSpriteDepthMin + t * (kSpriteDepthMax - kSpriteDepthMin);

        outBatches.back().instances.push_back(inst);
    }
}

std::vector<SpriteSystem::Batch> SpriteSystem::buildBatches(
    const std::vector<SpriteDrawEntry>& entries)
{
    // Backward-compatible by-value form for tests + any external caller.
    std::vector<Batch> batches;
    buildBatches(entries, batches);
    return batches;
}

} // namespace Vestige
