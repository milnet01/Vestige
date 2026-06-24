// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file gi_math.h
/// @brief CPU spec for the dynamic-GI froxel math (Slice R4, Variant A), pinned
///        against the GLSL.
///
/// The froxel↔world/slice reconstruction, the reprojected history sample, and the
/// confidence-weighted EMA the GI inject compute (`gi_inject.comp.glsl`) and the
/// per-fragment read (`scene.frag.glsl`) share are mirrored here as pure functions
/// so GL-free parity tests can pin the math (project Rule 7: dual CPU-spec /
/// GPU-runtime impl with a parity test). The slice-coord helper is the CPU twin of
/// the GLSL `volumetricSliceCoord` the fog composite already uses (design §11);
/// reusing it keeps the GI cache addressed identically to the fog volume it
/// co-locates in. The three GI_* tuning constants are the single source of truth
/// shared by both sides — the GLSL declares the same literals (design §11.2).
///
/// TODO: revisit GI_ALPHA / GI_DECAY / GI_STRENGTH_DEFAULT via Formula Workbench
/// once a reference GI capture exists (design §11.2 — Rule 6 carve-out: no
/// ground-truth dataset today, same disposition as the fog/SSS constants).
#pragma once

#include "renderer/volumetric_fog.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace Vestige
{

/// EMA blend weight: fraction of this frame's injected radiance folded in per frame.
constexpr float GI_ALPHA = 0.1f;
/// Confidence bleed per frame when a warm froxel stops receiving injection.
constexpr float GI_DECAY = 0.05f;
/// Default scale on the dynamic indirect term — bounds the residual overlap with the
/// baked SH floor's first bounce (design §11.2). The dynamic GI's only magnitude knob.
constexpr float GI_STRENGTH_DEFAULT = 0.5f;

/// @brief Normalised [0,1] depth-slice sample coordinate for a view-space depth.
///
/// CPU twin of the GLSL `volumetricSliceCoord(viewDepth, near, far) =
/// clamp(log(vd/near)/log(far/near), 0, 1)` the fog composite uses. This equals
/// `(viewDepthToFroxelSlice(cfg, vd) + 0.5) / resZ` (the fog-spec slice path), so
/// froxel `k`'s centre depth maps to the texel-centre coord `(k + 0.5)/resZ` — the
/// read and the inject write address the same texel. Degenerate ranges clamp to 0.
inline float giVolumetricSliceCoord(float viewDepth, float nearD, float farD)
{
    if (!(nearD > 0.0f) || !(farD > nearD) || !(viewDepth > 0.0f))
    {
        return 0.0f;
    }
    float c = std::log(viewDepth / nearD) / std::log(farD / nearD);
    return std::clamp(c, 0.0f, 1.0f);
}

/// @brief The [0,1]³ GI-volume sample coordinate for a fragment (or froxel centre)
///        at screen-UV @p screenUv and view-space depth @p viewDepth.
///
/// Shared by the inject write and the per-fragment read so they address the same
/// texel (guards an off-by-half-texel read/write mismatch). `.xy` is the screen UV;
/// `.z` is @ref giVolumetricSliceCoord.
inline glm::vec3 giSampleCoord(const glm::vec2& screenUv, float viewDepth,
                               const FroxelGridConfig& cfg)
{
    return glm::vec3(screenUv.x, screenUv.y,
                     giVolumetricSliceCoord(viewDepth, cfg.near, cfg.far));
}

/// @brief World-space position of the centre of froxel `(i, j, k)`.
///
/// Mirrors the froxel→world reconstruction the fog inject performs
/// (`volumetric_inject.comp.glsl`): screen-UV at the froxel-column centre → NDC →
/// view ray via @p invProjection → scaled to the exponential slice depth → world via
/// @p invView. @p invProjection / @p invView are *this* frame's inverse matrices.
inline glm::vec3 giFroxelCenterWorld(int i, int j, int k,
                                     const glm::mat4& invProjection,
                                     const glm::mat4& invView,
                                     const FroxelGridConfig& cfg)
{
    glm::vec2 uv((static_cast<float>(i) + 0.5f) / static_cast<float>(cfg.resX),
                 (static_cast<float>(j) + 0.5f) / static_cast<float>(cfg.resY));
    glm::vec2 ndc = uv * 2.0f - 1.0f;
    glm::vec4 vp = invProjection * glm::vec4(ndc, 1.0f, 1.0f);
    glm::vec3 ray = glm::vec3(vp) / vp.w;
    float viewDepth = froxelSliceToViewDepth(cfg, k);
    glm::vec3 viewPos = ray * (viewDepth / std::max(-ray.z, 1e-4f));
    return glm::vec3(invView * glm::vec4(viewPos, 1.0f));
}

/// @brief Result of reprojecting a froxel centre into the previous frame's cache.
struct GiReprojection
{
    bool      inFrustum = false;          ///< true ⇒ warm (sample history); false ⇒ cold.
    glm::vec3 historyUvw{0.0f, 0.0f, 0.0f}; ///< [0,1]³ history sample coord (valid iff inFrustum).
};

/// @brief Reproject a world position into the previous frame's GI froxel cache.
///
/// The froxel grid is view-aligned, so froxel `(i,j,k)` is a *different* world point
/// each frame — history MUST be read at the reprojected coord, not at `(i,j,k)`
/// (design §11.2 C1). Transforms @p worldPos through the previous frame's
/// view-projection (@p prevViewProj) for the screen UV and through @p prevView for
/// the view depth → slice coord. `inFrustum` is false (cold start, no stale smear)
/// when the point is behind the previous camera, off-screen, or outside the
/// volumetric `[near, far]` range.
inline GiReprojection giReprojectToHistory(const glm::vec3& worldPos,
                                           const glm::mat4& prevViewProj,
                                           const glm::mat4& prevView,
                                           const FroxelGridConfig& cfg)
{
    GiReprojection r;
    glm::vec4 clip = prevViewProj * glm::vec4(worldPos, 1.0f);
    if (clip.w <= 0.0f)
    {
        return r; // behind the previous camera → cold
    }
    glm::vec2 uv = (glm::vec2(clip) / clip.w) * 0.5f + 0.5f;
    float prevViewDepth = -(prevView * glm::vec4(worldPos, 1.0f)).z;
    r.historyUvw = glm::vec3(uv.x, uv.y,
                             giVolumetricSliceCoord(prevViewDepth, cfg.near, cfg.far));
    r.inFrustum = uv.x >= 0.0f && uv.x <= 1.0f
               && uv.y >= 0.0f && uv.y <= 1.0f
               && prevViewDepth >= cfg.near && prevViewDepth <= cfg.far;
    return r;
}

/// @brief Confidence-weighted EMA update for one froxel (design §11.2 step 4).
///
/// `.rgb` = accumulated indirect radiance, `.a` = confidence in [0,1].
/// @p warm  — the reprojection landed inside the previous frustum (history usable).
/// @p valid — the froxel sits on the visible surface this frame (depth match) and so
///            receives @p injected radiance.
///
/// Cold froxel (newly revealed): take the injection outright with confidence @p alpha,
/// never blending against undefined history. Warm froxel: EMA-blend toward @p injected
/// when valid, else hold the radiance and bleed confidence by `(1 - decay)`. With
/// `alpha = 0` *and* `decay = 0` (reduce-motion) the output `.rgb·.a` is frozen.
inline glm::vec4 giConfidenceBlend(const glm::vec4& history, const glm::vec3& injected,
                                   bool warm, bool valid, float alpha, float decay)
{
    if (!warm)
    {
        return valid ? glm::vec4(injected, alpha) : glm::vec4(0.0f);
    }
    glm::vec3 rgb = valid ? glm::mix(glm::vec3(history), injected, alpha)
                          : glm::vec3(history);
    float a = valid ? std::min(history.a + alpha, 1.0f)
                    : history.a * (1.0f - decay);
    return glm::vec4(rgb, a);
}

}  // namespace Vestige
