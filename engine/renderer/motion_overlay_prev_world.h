// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file motion_overlay_prev_world.h
/// @brief Prev-frame world-matrix cache update for the motion-vector overlay.
///
/// The `Renderer::renderScene` per-object motion-vector overlay reads each
/// entity's prev-frame world matrix from `m_prevWorldMatrices` keyed by
/// `entityId`. Before R10 the cache was cleared + populated only inside
/// the `if (isTaa)` end-of-frame block, so non-TAA modes (MSAA / SMAA /
/// None) carried stale entries from a prior TAA session forever — the
/// motion overlay then read those stale matrices on a subsequent TAA
/// toggle-back, blending current geometry against a possibly-different
/// mesh's old transform (entityIds get reused).
///
/// `updateMotionOverlayPrevWorld` lifts the bracket pattern into a
/// template free function so the clear-vs-populate contract is unit-
/// testable without a GL context (production passes `RenderItem`
/// vectors; tests pass mock item types with the same `entityId` /
/// `worldMatrix` fields).
#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <unordered_map>

namespace Vestige
{

/// @brief Refresh the prev-world-matrix cache at end-of-frame.
///
/// @tparam ItemRange Any iterable whose elements have `uint32_t entityId`
///         and `glm::mat4 worldMatrix` fields. In production this is
///         `std::vector<SceneRenderData::RenderItem>`; tests pass any
///         duck-typed container.
///
/// @param cache The renderer's prev-frame matrix cache.
/// @param isTaa Whether TAA is the active anti-aliasing mode this
///        frame. The cache is read by the per-object motion-vector
///        overlay only in TAA mode; non-TAA modes need only the clear.
/// @param renderItems Current-frame opaque scene items.
/// @param transparentItems Current-frame transparent scene items.
///
/// Contract:
/// - The cache is cleared **unconditionally** before any population
///   (the R10 fix — closes the cross-mode-switch staleness window).
/// - Population runs only when `isTaa` is true.
/// - Items with `entityId == 0` are skipped (sentinel for "no entity").
template <typename ItemRange>
void updateMotionOverlayPrevWorld(
    std::unordered_map<uint32_t, glm::mat4>& /*cache*/,
    bool /*isTaa*/,
    const ItemRange& /*renderItems*/,
    const ItemRange& /*transparentItems*/)
{
    // RED stub — body intentionally absent. Production callers compile
    // + link, but the cache stays as the caller passed it in. Replaced
    // in the green commit by the unconditional `cache.clear()` + TAA-
    // gated population per the R10 fix.
}

} // namespace Vestige
