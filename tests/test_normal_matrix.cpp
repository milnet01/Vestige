// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_normal_matrix.cpp
/// @brief Phase 10.9 Slice 13 Pe6 — `computeNormalMatrix` correctness pin.
///
/// Two contracts to prove:
///   (a) For uniform-scale model matrices the helper agrees with the
///       canonical `mat3(transpose(inverse(model)))` *after*
///       per-column normalize() — that's the equivalence the vertex
///       shader's `normalize(N * normal)` step requires.
///   (b) For non-uniform-scale model matrices the helper takes the
///       full inverse-transpose path so the result is a true
///       inverse-transpose, not a faster-but-wrong shortcut.

#include <gtest/gtest.h>

#include "renderer/normal_matrix.h"

#include <glm/gtc/matrix_transform.hpp>

using namespace Vestige;

namespace
{

bool nearlyParallel(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f)
{
    const auto na = glm::normalize(a);
    const auto nb = glm::normalize(b);
    return std::abs(glm::dot(na, nb) - 1.0f) < eps;
}

} // namespace

TEST(NormalMatrix, IdentityIsIdentity_Pe6)
{
    const glm::mat4 m = glm::mat4(1.0f);
    const glm::mat3 n = computeNormalMatrix(m);
    EXPECT_FLOAT_EQ(n[0][0], 1.0f);
    EXPECT_FLOAT_EQ(n[1][1], 1.0f);
    EXPECT_FLOAT_EQ(n[2][2], 1.0f);
}

TEST(NormalMatrix, UniformScaleAgreesWithInverseTransposeAfterNormalize_Pe6)
{
    // 3× uniform scale + 30° rotation about Y.
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::scale(model, glm::vec3(3.0f));
    model = glm::rotate(model, glm::radians(30.0f), glm::vec3(0, 1, 0));

    const glm::mat3 fast      = computeNormalMatrix(model);
    const glm::mat3 reference = glm::mat3(glm::transpose(glm::inverse(model)));

    // Each column of the normal matrix is a normal direction. After
    // normalize() in the shader the two paths must produce the same
    // direction. Magnitude difference is allowed (the shader normalizes
    // anyway), direction difference is the bug.
    EXPECT_TRUE(nearlyParallel(fast[0], reference[0]));
    EXPECT_TRUE(nearlyParallel(fast[1], reference[1]));
    EXPECT_TRUE(nearlyParallel(fast[2], reference[2]));
}

TEST(NormalMatrix, RotationOnlyAgreesExactlyWithReference_Pe6)
{
    // Pure rotation: m3 is orthonormal, inverse-transpose is itself.
    glm::mat4 model = glm::rotate(glm::mat4(1.0f),
                                   glm::radians(45.0f),
                                   glm::vec3(0, 0, 1));
    const glm::mat3 fast      = computeNormalMatrix(model);
    const glm::mat3 reference = glm::mat3(glm::transpose(glm::inverse(model)));

    for (int c = 0; c < 3; ++c)
    {
        for (int r = 0; r < 3; ++r)
        {
            EXPECT_NEAR(fast[c][r], reference[c][r], 1e-6f);
        }
    }
}

TEST(NormalMatrix, NonUniformScaleTakesInverseTransposePath_Pe6)
{
    // 2× X, 1× Y, 0.5× Z — a deliberately squashed-and-stretched mesh.
    // The fast m3-shortcut would give a *wrong* answer here because the
    // inverse-transpose is `diag(1/s)`-ish, not `diag(s)`-ish; the
    // helper must detect this and fall through to the full path.
    const glm::mat4 model = glm::scale(glm::mat4(1.0f),
                                        glm::vec3(2.0f, 1.0f, 0.5f));
    const glm::mat3 result = computeNormalMatrix(model);

    // Diagonal entries should be 0.5, 1.0, 2.0 (inverse-transpose of a
    // diagonal matrix is diag(1/s)). Pinned exactly so a regression
    // that returns mat3(model) instead would visibly fail.
    EXPECT_FLOAT_EQ(result[0][0], 0.5f);
    EXPECT_FLOAT_EQ(result[1][1], 1.0f);
    EXPECT_FLOAT_EQ(result[2][2], 2.0f);
}

TEST(NormalMatrix, TranslationOnlyKeepsIdentity_Pe6)
{
    // Translation does not affect normals; the upper-left 3×3 is
    // identity, so the helper should return identity regardless of
    // which path it takes.
    const glm::mat4 model = glm::translate(glm::mat4(1.0f),
                                            glm::vec3(10.0f, -20.0f, 5.0f));
    const glm::mat3 n = computeNormalMatrix(model);
    EXPECT_FLOAT_EQ(n[0][0], 1.0f);
    EXPECT_FLOAT_EQ(n[1][1], 1.0f);
    EXPECT_FLOAT_EQ(n[2][2], 1.0f);
    // Off-diagonal entries should be zero.
    EXPECT_FLOAT_EQ(n[0][1], 0.0f);
    EXPECT_FLOAT_EQ(n[1][2], 0.0f);
}
