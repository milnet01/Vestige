// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file motion_vector_math.h
/// @brief CPU spec for the motion-vector math (Slice R1), pinned against the GLSL.
///
/// The geometry-pass motion output (scene.vert.glsl / scene.frag.glsl) and the
/// motion-combine pass are mirrored here as pure functions so a GL-free parity
/// test can pin the math (project Rule 7: dual CPU-spec / GPU-runtime impl with a
/// parity test). These reproduce the now-deleted per-object overlay shaders
/// (motion_vectors_object.{vert,frag}.glsl) byte-for-byte for the common case.
#pragma once

#include <glm/glm.hpp>

namespace Vestige
{

/// @brief Perspective divide with the conventional 1e-6 |w| guard.
/// Mirrors `safeClipDivide` in the motion fragment shaders: a vertex on or behind
/// the camera plane (w ~= 0) would divide-by-zero to NaN/Inf, which bleeds into
/// neighbours via bilinear sampling — so it clamps to (0,0) instead.
/// TODO: revisit clip-divide epsilon via Formula Workbench once reference data exists.
inline glm::vec2 safeClipDivide(const glm::vec4& clip)
{
    return (glm::abs(clip.w) > 1e-6f) ? glm::vec2(clip.x / clip.w, clip.y / clip.w)
                                      : glm::vec2(0.0f);
}

/// @brief Screen-space motion vector (currentUV - previousUV) for a single
///        object-space point, the value the geometry pass writes to the motion
///        attachment's `.rg`.
///
/// @param model              Current-frame model matrix.
/// @param prevModel          Previous-frame model matrix (== model for static /
///                           first-frame / missing-from-cache objects → zero object motion).
/// @param viewProjection     Current-frame view-projection (jittered, == projection * view).
/// @param prevViewProjection Previous-frame view-projection (carries the previous jitter).
/// @param objSpacePos        The **base** (pre-skin / pre-morph) object-space position —
///                           skinned/morph meshes deliberately get rigid-body motion in R1,
///                           matching the overlay (R2 adds animated-pose motion).
/// @return UV-space motion in [-1,1]-ish range; (0,0) when both clip ws are degenerate.
inline glm::vec2 computeMotionVectorUV(const glm::mat4& model,
                                       const glm::mat4& prevModel,
                                       const glm::mat4& viewProjection,
                                       const glm::mat4& prevViewProjection,
                                       const glm::vec3& objSpacePos)
{
    const glm::vec4 basePos(objSpacePos, 1.0f);
    const glm::vec4 currentClip = viewProjection * (model * basePos);
    const glm::vec4 prevClip = prevViewProjection * (prevModel * basePos);

    const glm::vec2 currUV = safeClipDivide(currentClip) * 0.5f + 0.5f;
    const glm::vec2 prevUV = safeClipDivide(prevClip) * 0.5f + 0.5f;

    return currUV - prevUV;
}

/// @brief The motion-combine selector (mirrors §4.4 / the combine fragment shader).
///
/// Chooses the final motion value per pixel: opaque renderItems that wrote object
/// motion (coverage flag set) win; sky (reverse-Z far) is zero; everything else
/// (cloth / terrain / water / particles, and behind depth-write-off transparent
/// geometry) falls back to the camera-reprojection motion.
///
/// @param coverageFlag  The motion attachment's `.b` — 1.0 where the opaque pass wrote.
/// @param sceneMotion   The motion attachment's `.rg` (object motion).
/// @param depth         Resolved scene depth (reverse-Z: sky == far == 0).
/// @param cameraMotion  Camera-reprojection motion for this pixel (from depth).
inline glm::vec2 combineMotion(float coverageFlag,
                               const glm::vec2& sceneMotion,
                               float depth,
                               const glm::vec2& cameraMotion)
{
    if (coverageFlag > 0.5f)
    {
        return sceneMotion;          // opaque renderItem wrote object motion
    }
    if (depth <= 0.0001f)
    {
        return glm::vec2(0.0f);      // sky (reverse-Z far plane) — unchanged
    }
    return cameraMotion;             // cloth / terrain / etc. + behind transparent
}

} // namespace Vestige
