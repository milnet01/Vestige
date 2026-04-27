// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file normal_matrix.h
/// @brief Phase 10.9 Slice 13 Pe6 — single-source normal-matrix helper.
///
/// Extracts the per-draw `glm::mat3(glm::transpose(glm::inverse(model)))`
/// computation that previously lived inline in `Renderer::drawMesh`,
/// the per-cloth path, and any future per-mesh upload, so:
///
///   1. The math has one definition (CLAUDE.md Rule 3 — reuse before
///      rewriting; the audit's headline complaint was the duplication).
///   2. Adding a uniform-scale fast path improves all callers in one
///      place instead of having to find them.
///
/// Fast path: for uniform-scale (translate + rotate + uniform scale)
/// transforms the inverse-transpose collapses to `(1/s) * m3`, which
/// after `normalize(...)` in the vertex shader is bit-identical to the
/// upper-left 3×3 of the model matrix. We return that directly and
/// avoid the 4×4 inverse + 3×3 transpose entirely. Only a non-uniform
/// scale (squash-and-stretch animations, anisotropic billboards) falls
/// through to the full inverse-transpose path.
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <cmath>

namespace Vestige
{

/// @brief Returns the matrix to multiply object-space normals by before
///        the vertex shader's `normalize(...)` step.
inline glm::mat3 computeNormalMatrix(const glm::mat4& modelMatrix)
{
    const glm::mat3 m3(modelMatrix);
    const float l0 = glm::dot(m3[0], m3[0]);
    const float l1 = glm::dot(m3[1], m3[1]);
    const float l2 = glm::dot(m3[2], m3[2]);

    // Tolerance is relative to the largest squared length so we don't
    // mis-classify a 1000× scale as non-uniform because absolute deltas
    // grow with scale magnitude. ~0.01% relative spread is well below
    // the threshold at which a vertex shader normalize() can mask the
    // difference.
    const float lmax = std::max(l0, std::max(l1, l2));
    const float tol  = 1e-4f * lmax;
    if (std::abs(l0 - l1) < tol && std::abs(l1 - l2) < tol)
    {
        return m3;
    }
    return glm::mat3(glm::transpose(glm::inverse(modelMatrix)));
}

} // namespace Vestige
