// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_sh_probe_grid.cpp
/// @brief Unit tests for SH probe grid pure math
///        (projectCubemapToSH, convolveRadianceToIrradiance).
///
/// These cover the L2 spherical harmonic projection and the
/// Ramamoorthi & Hanrahan (2001) cosine-lobe convolution coefficients
/// without requiring an OpenGL context. GPU-side upload/bind are
/// intentionally out of scope here — those need a GL context and are
/// exercised via the scene renderer's live capture path.
#include <gtest/gtest.h>
#include <glm/glm.hpp>

#include "renderer/sh_probe_grid.h"

#include <vector>

namespace
{

constexpr float kPi = 3.14159265358979323846f;

/// Build a uniform-color cubemap of the given face size. Stored as
/// `6 * faceSize * faceSize * 3` floats in the layout expected by
/// SHProbeGrid::projectCubemapToSH: `[face][y][x][rgb]`.
std::vector<float> makeUniformCubemap(int faceSize, glm::vec3 color)
{
    std::vector<float> data(static_cast<size_t>(6 * faceSize * faceSize * 3));
    for (size_t i = 0; i < data.size(); i += 3)
    {
        data[i + 0] = color.r;
        data[i + 1] = color.g;
        data[i + 2] = color.b;
    }
    return data;
}

}  // namespace


// ============================================================================
// projectCubemapToSH
// ============================================================================

/// A uniformly-coloured cubemap is the classic "ambient-only" case. All 9 SH
/// coefficients after projection should have band-0 (dc) energy equal to the
/// colour, and every other band should be ~0 because there's no directional
/// variation to project onto.
TEST(ShProbeGridTest, UniformCubemapProjectsToDcOnly)
{
    const int faceSize = 16;
    const glm::vec3 color(0.5f, 0.5f, 0.5f);
    auto cubemap = makeUniformCubemap(faceSize, color);

    glm::vec3 coeffs[9];
    Vestige::SHProbeGrid::projectCubemapToSH(cubemap.data(), faceSize, coeffs);

    // The band-0 basis is Y_00 = 0.282095 and the DC coefficient of a
    // uniform-colour radiance function is (color * 4*pi) * Y_00 = color *
    // 4*pi * 0.282095 ≈ color * 3.5449. That's what integrating a constant
    // RGB over the sphere with Y_00 weighting produces.
    const float expectedDc = 4.0f * kPi * 0.282095f;
    EXPECT_NEAR(coeffs[0].r, color.r * expectedDc, 0.05f);
    EXPECT_NEAR(coeffs[0].g, color.g * expectedDc, 0.05f);
    EXPECT_NEAR(coeffs[0].b, color.b * expectedDc, 0.05f);

    // Bands 1 and 2 should integrate to near zero over a uniform function.
    for (int i = 1; i < 9; i++)
    {
        EXPECT_NEAR(coeffs[i].r, 0.0f, 0.01f) << "band coeff " << i << ".r";
        EXPECT_NEAR(coeffs[i].g, 0.0f, 0.01f) << "band coeff " << i << ".g";
        EXPECT_NEAR(coeffs[i].b, 0.0f, 0.01f) << "band coeff " << i << ".b";
    }
}


/// A zero cubemap projects to zero coefficients — sanity / NaN guard.
TEST(ShProbeGridTest, ZeroCubemapProjectsToZero)
{
    const int faceSize = 8;
    auto cubemap = makeUniformCubemap(faceSize, glm::vec3(0.0f));

    glm::vec3 coeffs[9];
    Vestige::SHProbeGrid::projectCubemapToSH(cubemap.data(), faceSize, coeffs);

    for (int i = 0; i < 9; i++)
    {
        EXPECT_EQ(coeffs[i].r, 0.0f);
        EXPECT_EQ(coeffs[i].g, 0.0f);
        EXPECT_EQ(coeffs[i].b, 0.0f);
    }
}


/// Values ≥ 50 get clamped during projection to prevent a single-pixel
/// bright spot from exploding into absurd SH coefficients. Verify that
/// a mostly-uniform cubemap with one hot pixel does not exceed the
/// clamped-projection upper bound.
TEST(ShProbeGridTest, ExtremeHdrValuesAreClamped)
{
    const int faceSize = 8;
    auto cubemap = makeUniformCubemap(faceSize, glm::vec3(1.0f));

    // Splat one absurdly bright pixel into face 0 texel (0, 0).
    cubemap[0] = 1e6f;  // R
    cubemap[1] = 1e6f;  // G
    cubemap[2] = 1e6f;  // B

    glm::vec3 coeffs[9];
    Vestige::SHProbeGrid::projectCubemapToSH(cubemap.data(), faceSize, coeffs);

    // Post-clamp upper bound: 50.0f per channel, band-0 coefficient
    // integrates to at most ≈ 50 * 4*pi * 0.282095 ≈ 177. The DC with the
    // uniform 1.0 baseline plus one clamped-50 pixel should stay well
    // below 200 and well above zero.
    EXPECT_LT(coeffs[0].r, 200.0f);
    EXPECT_LT(coeffs[0].g, 200.0f);
    EXPECT_LT(coeffs[0].b, 200.0f);
    EXPECT_GT(coeffs[0].r, 0.0f);
}


// ============================================================================
// convolveRadianceToIrradiance
// ============================================================================

/// Ramamoorthi & Hanrahan (2001) Table 1: the band-wise cosine-lobe
/// convolution constants are A0 = π, A1 = 2π/3, A2 = π/4. Convolution is
/// just a per-band multiply; verify the ratios match.
TEST(ShProbeGridTest, ConvolveAppliesRamamoorthiCoefficients)
{
    glm::vec3 coeffs[9];
    for (int i = 0; i < 9; i++)
    {
        coeffs[i] = glm::vec3(1.0f);
    }

    Vestige::SHProbeGrid::convolveRadianceToIrradiance(coeffs);

    // Band 0 (index 0): × π
    EXPECT_NEAR(coeffs[0].r, kPi, 1e-3f);

    // Band 1 (indices 1, 2, 3): × 2π/3
    const float a1 = 2.0f * kPi / 3.0f;
    EXPECT_NEAR(coeffs[1].r, a1, 1e-3f);
    EXPECT_NEAR(coeffs[2].r, a1, 1e-3f);
    EXPECT_NEAR(coeffs[3].r, a1, 1e-3f);

    // Band 2 (indices 4..8): × π/4
    const float a2 = kPi / 4.0f;
    for (int i = 4; i < 9; i++)
    {
        EXPECT_NEAR(coeffs[i].r, a2, 1e-3f) << "band 2 coeff " << i;
    }
}


/// Convolution is in-place on the coeffs array. Zero in → zero out.
TEST(ShProbeGridTest, ConvolveZeroInputStaysZero)
{
    glm::vec3 coeffs[9] = {};

    Vestige::SHProbeGrid::convolveRadianceToIrradiance(coeffs);

    for (int i = 0; i < 9; i++)
    {
        EXPECT_EQ(coeffs[i].r, 0.0f);
        EXPECT_EQ(coeffs[i].g, 0.0f);
        EXPECT_EQ(coeffs[i].b, 0.0f);
    }
}


/// End-to-end pipeline: a uniformly-white cubemap, projected and then
/// convolved, should produce an irradiance SH whose band-0 coefficient
/// equals the expected diffuse irradiance of a unit-radiance ambient
/// environment. The closed form is
///     E = π * (radiance * 4π * Y_00) * Y_00 ≈ radiance * π ≈ 3.14
/// in the irradiance-per-pixel sense (SH band-0 contribution = coeff[0]
/// * Y_00 = radiance * 4π * Y_00^2 * π).
TEST(ShProbeGridTest, UniformCubemapFullPipelineDiffuse)
{
    const int faceSize = 16;
    auto cubemap = makeUniformCubemap(faceSize, glm::vec3(1.0f));

    glm::vec3 coeffs[9];
    Vestige::SHProbeGrid::projectCubemapToSH(cubemap.data(), faceSize, coeffs);
    Vestige::SHProbeGrid::convolveRadianceToIrradiance(coeffs);

    // Evaluated along any direction d, irradiance = sum(coeffs[i] *
    // basis[i](d)). For uniform ambient, bands 1+ vanish, so
    // irradiance(d) = coeffs[0] * Y_00 = coeffs[0] * 0.282095. This
    // should equal π * radiance = π for a radiance-1 environment.
    const float irradianceAtAnyDirection = coeffs[0].r * 0.282095f;
    EXPECT_NEAR(irradianceAtAnyDirection, kPi, 0.05f);
}


// ============================================================================
// R7 — full path: CPU pipeline → shader-equivalent evaluator.
//
// The shader at scene.frag.glsl:537-602 evaluates SH using
// Ramamoorthi-Hanrahan 2001 Eq. 13. The c1..c5 constants in that
// formulation already fold in the per-band cosine-lobe weights A_ℓ
// (c4 = A_0 × Y_00 = √π / 2, c1 = A_2 × Y_2,±2 / 2, etc.). The shader
// therefore expects RADIANCE-SH on input and returns irradiance / π
// (the LearnOpenGL diffuse-IBL convention).
//
// Before R7, `Renderer::captureSHGrid` ran `projectCubemapToSH` then
// `convolveRadianceToIrradiance` on the CPU before upload — a second
// application of A_ℓ on top of the shader's. For a uniform-radiance
// cubemap the shader output is therefore π × radiance instead of
// radiance — band-0 ambient was ≈π× over-bright since day one, and
// every PBR material has been lit through this corrupted value.
//
// The R7 fix removes the CPU-side convolution; the test below pins
// the corrected end-to-end magnitude. Because the test runs the
// whole production CPU pipeline (`computeProbeShFromCubemap`, which
// `Renderer::captureSHGrid` also calls), it would fail against a
// regression that re-introduced the convolution.
// ============================================================================

/// Pin the headline R7 invariant: a uniform-radiance ambient
/// environment, when round-tripped through the CPU pipeline + shader-
/// equivalent evaluator, should return the input radiance for any
/// surface normal.
TEST(ShProbeGridTest, UniformCubemapEndToEndEqualsRadiance_R7)
{
    const int faceSize = 16;
    const glm::vec3 radiance(0.5f, 0.5f, 0.5f);
    auto cubemap = makeUniformCubemap(faceSize, radiance);

    glm::vec3 coeffs[9];
    Vestige::SHProbeGrid::computeProbeShFromCubemap(cubemap.data(), faceSize, coeffs);

    // For a uniform-radiance environment irradiance is direction-
    // independent. Sample three orthogonal normals as cross-checks.
    glm::vec3 e_y = Vestige::SHProbeGrid::evaluateIrradianceCpu(coeffs, glm::vec3(0, 1, 0));
    glm::vec3 e_x = Vestige::SHProbeGrid::evaluateIrradianceCpu(coeffs, glm::vec3(1, 0, 0));
    glm::vec3 e_z = Vestige::SHProbeGrid::evaluateIrradianceCpu(coeffs, glm::vec3(0, 0, 1));

    // 0.02 absolute tolerance — covers both the cubemap's
    // discretisation residue (faceSize=16) and small float rounding
    // in c1..c5. Catching the π× over-bright bug needs ~3.0 of
    // headroom, so the tolerance is 100× tighter than the bug
    // signature.
    EXPECT_NEAR(e_y.r, radiance.r, 0.02f);
    EXPECT_NEAR(e_y.g, radiance.g, 0.02f);
    EXPECT_NEAR(e_y.b, radiance.b, 0.02f);
    EXPECT_NEAR(e_x.r, radiance.r, 0.02f);
    EXPECT_NEAR(e_z.r, radiance.r, 0.02f);
}

/// Direction independence for a uniform input. Sampling along any
/// two normals should give the same irradiance regardless of the
/// underlying SH evaluator constants.
TEST(ShProbeGridTest, UniformCubemapShaderEvalDirectionIndependent_R7)
{
    const int faceSize = 16;
    auto cubemap = makeUniformCubemap(faceSize, glm::vec3(0.5f));

    glm::vec3 coeffs[9];
    Vestige::SHProbeGrid::computeProbeShFromCubemap(cubemap.data(), faceSize, coeffs);

    glm::vec3 e_y = Vestige::SHProbeGrid::evaluateIrradianceCpu(coeffs, glm::vec3(0, 1, 0));
    glm::vec3 e_x = Vestige::SHProbeGrid::evaluateIrradianceCpu(coeffs, glm::vec3(1, 0, 0));
    glm::vec3 e_neg_y = Vestige::SHProbeGrid::evaluateIrradianceCpu(coeffs, glm::vec3(0, -1, 0));

    EXPECT_NEAR(e_x.r, e_y.r, 0.02f);
    EXPECT_NEAR(e_neg_y.r, e_y.r, 0.02f);
}

/// Linear scaling of the evaluator. Doubling the input radiance must
/// exactly double the output irradiance — pin it so a future fix to
/// the evaluator can't accidentally introduce a non-linearity.
TEST(ShProbeGridTest, UniformCubemapEvaluatorIsLinearInRadiance_R7)
{
    const int faceSize = 16;
    auto cubemapHalf = makeUniformCubemap(faceSize, glm::vec3(0.25f));
    auto cubemapWhole = makeUniformCubemap(faceSize, glm::vec3(0.50f));

    glm::vec3 coeffsHalf[9];
    glm::vec3 coeffsWhole[9];
    Vestige::SHProbeGrid::computeProbeShFromCubemap(cubemapHalf.data(), faceSize, coeffsHalf);
    Vestige::SHProbeGrid::computeProbeShFromCubemap(cubemapWhole.data(), faceSize, coeffsWhole);

    glm::vec3 e_half = Vestige::SHProbeGrid::evaluateIrradianceCpu(coeffsHalf,
                                                                    glm::vec3(0, 1, 0));
    glm::vec3 e_whole = Vestige::SHProbeGrid::evaluateIrradianceCpu(coeffsWhole,
                                                                     glm::vec3(0, 1, 0));

    EXPECT_NEAR(e_whole.r, 2.0f * e_half.r, 0.02f);
}

/// Zero radiance produces zero irradiance — sanity / NaN guard.
TEST(ShProbeGridTest, ZeroCubemapEndToEndEqualsZero_R7)
{
    const int faceSize = 8;
    auto cubemap = makeUniformCubemap(faceSize, glm::vec3(0.0f));

    glm::vec3 coeffs[9];
    Vestige::SHProbeGrid::computeProbeShFromCubemap(cubemap.data(), faceSize, coeffs);

    glm::vec3 e = Vestige::SHProbeGrid::evaluateIrradianceCpu(coeffs, glm::vec3(0, 1, 0));
    EXPECT_EQ(e.r, 0.0f);
    EXPECT_EQ(e.g, 0.0f);
    EXPECT_EQ(e.b, 0.0f);
}
