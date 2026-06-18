// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_volumetric_fog_gpu.cpp
/// @brief Phase 10 slice 11.6 — headless GPU verification of the froxel
///        volumetric pipeline (VolumetricFogPass + the three compute shaders).
///
/// These run on the GL test fixture's hidden 4.5-core context and read froxel
/// texels back with `glGetTextureImage`, so the GPU passes are checked against
/// the CPU closed forms (CLAUDE.md Rule 7), not merely compiled:
///
///   * Beer-Lambert transmittance for a uniform medium has the exact form
///     `T(k) = exp(-sigma_t * (boundary(k+1) - near))`, pinned against
///     `froxelSliceBoundaryViewDepth()`.
///   * The energy-conserving first-slab inscatter integral has a closed form
///     for a uniform isotropic (g = 0) medium.
///   * The GLSL `henyeyGreenstein` helper is extracted and run as a fragment
///     shader, pinned against the CPU `henyeyGreensteinPhase()`.
///
/// What these do NOT verify: the 60 FPS frame budget (needs a real-scene run
/// on the target GPU) and the view-space froxel reconstruction / cosθ path
/// (exercised only with g = 0 here, where the phase is position-independent).
/// Those are validated when the pass is wired into the composite (slice 11.6
/// part B).

#include "gl_test_fixture.h"
#include "shader_parity_helpers.h"

#include "renderer/volumetric_fog.h"
#include "renderer/volumetric_fog_pass.h"

#include <gtest/gtest.h>

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Vestige::Test
{

namespace
{

constexpr float kPi = 3.14159265358979323846f;

// A small grid keeps readback cheap while still exercising the exponential
// slice distribution across enough slices to be meaningful.
FroxelGridConfig smallGrid()
{
    FroxelGridConfig g;
    g.resX = 4;
    g.resY = 4;
    g.resZ = 16;
    g.near = 0.5f;
    g.far  = 50.0f;
    return g;
}

// Read an RGBA16F 3D texture back as floats. Layout is x-fastest, then y,
// then z (layer): idx(x,y,z) = ((z*resY + y)*resX + x) * 4.
std::vector<float> readbackRGBA(GLuint tex, const FroxelGridConfig& g)
{
    std::vector<float> px(static_cast<size_t>(g.resX * g.resY * g.resZ) * 4);
    glGetTextureImage(tex, 0, GL_RGBA, GL_FLOAT,
                      static_cast<GLsizei>(px.size() * sizeof(float)), px.data());
    return px;
}

size_t idx(const FroxelGridConfig& g, int x, int y, int z)
{
    return (static_cast<size_t>((z * g.resY + y) * g.resX + x)) * 4;
}

// 1×1, single-layer depth array cleared to `depthValue`, sampled as a plain
// sampler2DArray (.r = raw depth, compare mode NONE). Stands in for the CSM
// depth map so the froxel shadow compare can be exercised headlessly.
GLuint makeShadowArray(float depthValue)
{
    GLuint tex = 0;
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &tex);
    glTextureStorage3D(tex, 1, GL_DEPTH_COMPONENT32F, 1, 1, 1);
    glClearTexImage(tex, 0, GL_DEPTH_COMPONENT, GL_FLOAT, &depthValue);
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

} // namespace

class VolumetricFogGpuTest : public GLTestFixture {};

// =============================================================================
// Resource lifecycle
// =============================================================================

TEST_F(VolumetricFogGpuTest, InitAllocatesAndDestroyReleases)
{
    VolumetricFogPass pass;
    ASSERT_TRUE(pass.init(VESTIGE_SHADER_DIR, smallGrid()));
    EXPECT_TRUE(pass.isInitialized());
    EXPECT_NE(pass.integratedTexture(), 0u);

    pass.destroy();
    EXPECT_FALSE(pass.isInitialized());
    EXPECT_EQ(pass.integratedTexture(), 0u);
}

// =============================================================================
// Beer-Lambert transmittance — uniform medium closed form
// =============================================================================

TEST_F(VolumetricFogGpuTest, TransmittanceMatchesBeerLambertClosedForm)
{
    const FroxelGridConfig g = smallGrid();

    VolumetricFogPass pass;
    ASSERT_TRUE(pass.init(VESTIGE_SHADER_DIR, g));

    VolumetricFogPass::FrameParams p;
    p.scattering = glm::vec3(0.0f); // inscatter irrelevant to transmittance
    p.extinction = 0.05f;           // uniform sigma_t
    p.anisotropy = 0.0f;
    p.sunRadiance = glm::vec3(0.0f);
    pass.dispatch(p);

    const auto px = readbackRGBA(pass.integratedTexture(), g);

    // T(k) = exp(-sigma_t * (boundary(k+1) - near)), identical for every tile.
    for (int k = 0; k < g.resZ; ++k)
    {
        const float marched = froxelSliceBoundaryViewDepth(g, k + 1) - g.near;
        const float expectedT = std::exp(-p.extinction * marched);
        const float gotT = px[idx(g, 1, 2, k) + 3];
        EXPECT_NEAR(gotT, expectedT, 5e-3f) << "transmittance @ slice " << k;
    }
}

// =============================================================================
// First-slab inscatter — energy-conserving integral, isotropic uniform medium
// =============================================================================

TEST_F(VolumetricFogGpuTest, FirstSlabInscatterMatchesClosedForm)
{
    const FroxelGridConfig g = smallGrid();

    VolumetricFogPass pass;
    ASSERT_TRUE(pass.init(VESTIGE_SHADER_DIR, g));

    VolumetricFogPass::FrameParams p;
    p.scattering  = glm::vec3(0.5f, 0.4f, 0.3f); // sigma_s per channel
    p.extinction  = 0.05f;
    p.anisotropy  = 0.0f;                        // isotropic ⇒ phase = 1/(4π)
    p.sunRadiance = glm::vec3(2.0f);
    p.ambient     = glm::vec3(0.0f);
    pass.dispatch(p);

    const auto px = readbackRGBA(pass.integratedTexture(), g);

    // scatter pass writes inscatter = sigma_s * (phase * radiance), phase = 1/(4π).
    // integrate slab 0: accum = inscatter * (1 - exp(-sigma_t*d0)) / sigma_t,
    // with d0 = boundary(1) - near, starting transmittance = 1.
    const float d0     = froxelSliceBoundaryViewDepth(g, 1) - g.near;
    const float slabT0 = std::exp(-p.extinction * d0);
    const float gain0  = (1.0f - slabT0) / p.extinction;

    const glm::vec3 inscatter = p.scattering * (p.sunRadiance / (4.0f * kPi));
    const glm::vec3 expected   = inscatter * gain0;

    const size_t i0 = idx(g, 2, 1, 0);
    EXPECT_NEAR(px[i0 + 0], expected.r, 3e-3f) << "slab-0 inscatter r";
    EXPECT_NEAR(px[i0 + 1], expected.g, 3e-3f) << "slab-0 inscatter g";
    EXPECT_NEAR(px[i0 + 2], expected.b, 3e-3f) << "slab-0 inscatter b";

    // Accumulated inscatter must be monotonically non-decreasing along the
    // column (each slab only adds energy).
    for (int k = 1; k < g.resZ; ++k)
    {
        const float prev = px[idx(g, 2, 1, k - 1) + 0];
        const float cur  = px[idx(g, 2, 1, k) + 0];
        EXPECT_GE(cur, prev - 1e-4f) << "inscatter monotonic @ slice " << k;
        EXPECT_TRUE(std::isfinite(cur)) << "finite @ slice " << k;
    }
}

// =============================================================================
// Henyey-Greenstein phase — GLSL helper pinned to the CPU reference
// =============================================================================

TEST_F(VolumetricFogGpuTest, GlslPhaseMatchesCpuReference)
{
    const std::string fnSrc = extractGlslFunction(
        readShaderFile("volumetric_scatter.comp.glsl"), "henyeyGreenstein");
    ASSERT_FALSE(fnSrc.empty());

    ShaderProgram prog(
        "#version 450 core\n"
        "layout(location = 0) out vec4 outColor;\n"
        "uniform float u_cosTheta;\n"
        "uniform float u_g;\n"
        "const float PI = 3.14159265358979323846;\n"
        + fnSrc +
        "void main() {\n"
        "    outColor = vec4(henyeyGreenstein(u_cosTheta, u_g), 0.0, 0.0, 1.0);\n"
        "}\n");
    ASSERT_TRUE(prog.valid());

    const float cosThetas[] = {-1.0f, -0.5f, 0.0f, 0.5f, 0.9f, 1.0f};
    const float gs[]        = {0.0f, 0.2f, 0.6f, 0.9f};

    for (float gv : gs)
    {
        for (float c : cosThetas)
        {
            const glm::vec4 gpu = prog.run({{"u_cosTheta", c}, {"u_g", gv}});
            const float cpu = henyeyGreensteinPhase(c, gv);
            EXPECT_NEAR(gpu.r, cpu, 1e-4f + 1e-3f * std::abs(cpu))
                << "phase mismatch @ cos=" << c << " g=" << gv;
        }
    }
}

// =============================================================================
// CSM sun shadowing — fully-lit vs fully-occluded synthetic shadow map
// =============================================================================

TEST_F(VolumetricFogGpuTest, CsmShadowGatesSunInscatter)
{
    const FroxelGridConfig g = smallGrid();

    VolumetricFogPass pass;
    ASSERT_TRUE(pass.init(VESTIGE_SHADER_DIR, g));

    // A light-space matrix that maps every world point to clip (0,0,0,1):
    // proj = (0,0,0) → *0.5+0.5 → (0.5,0.5,0.5). Every froxel then samples
    // the shadow map at its centre with receiver depth 0.5, so the lit/dark
    // outcome is decided purely by the occluder depth we store — independent
    // of froxel geometry.
    glm::mat4 collapse(0.0f);
    collapse[3][3] = 1.0f;

    VolumetricFogPass::FrameParams p;
    p.scattering  = glm::vec3(0.5f, 0.4f, 0.3f);
    p.extinction  = 0.05f;
    p.anisotropy  = 0.0f;            // isotropic ⇒ phase = 1/(4π)
    p.sunRadiance = glm::vec3(2.0f);
    p.ambient     = glm::vec3(0.0f); // shadowed froxels then integrate to 0
    p.csmCascadeCount        = 1;
    p.csmCascadeSplits[0]    = 1.0e6f; // every slice falls in cascade 0
    p.csmLightSpaceMatrices[0] = collapse;
    p.invView                = glm::mat4(1.0f);
    p.csmDepthBias           = 0.0015f;

    // Fully lit: occluder depth 1.0 ⇒ 0.5 - bias > 1.0 is false ⇒ visibility 1.
    const GLuint litMap = makeShadowArray(1.0f);
    p.csmShadowTexture = litMap;
    pass.dispatch(p);
    const auto litPx = readbackRGBA(pass.integratedTexture(), g);

    // Fully occluded: occluder depth 0.0 ⇒ 0.5 - bias > 0.0 is true ⇒ visibility 0.
    const GLuint darkMap = makeShadowArray(0.0f);
    p.csmShadowTexture = darkMap;
    pass.dispatch(p);
    const auto darkPx = readbackRGBA(pass.integratedTexture(), g);

    // Lit must reproduce the unshadowed first-slab closed form; dark zeroes
    // the sun term (ambient is 0) so the whole column integrates to nothing.
    const float d0     = froxelSliceBoundaryViewDepth(g, 1) - g.near;
    const float gain0  = (1.0f - std::exp(-p.extinction * d0)) / p.extinction;
    const glm::vec3 expectedLit =
        p.scattering * (p.sunRadiance / (4.0f * kPi)) * gain0;

    const size_t i0 = idx(g, 2, 1, 0);
    EXPECT_NEAR(litPx[i0 + 0], expectedLit.r, 3e-3f) << "lit slab-0 r";
    EXPECT_NEAR(litPx[i0 + 1], expectedLit.g, 3e-3f) << "lit slab-0 g";
    EXPECT_NEAR(litPx[i0 + 2], expectedLit.b, 3e-3f) << "lit slab-0 b";

    // Occluded: every froxel in the column dark → integrated inscatter ~0.
    for (int k = 0; k < g.resZ; ++k)
    {
        EXPECT_NEAR(darkPx[idx(g, 2, 1, k) + 0], 0.0f, 1e-3f)
            << "occluded slab " << k << " must carry no sun inscatter";
    }
    EXPECT_GT(litPx[i0 + 0], darkPx[i0 + 0] + 1e-3f) << "lit must exceed occluded";

    glDeleteTextures(1, &litMap);
    glDeleteTextures(1, &darkMap);
}

// =============================================================================
// Composite depth-slice texcoord — GLSL helper pinned to the CPU spec
// =============================================================================

TEST_F(VolumetricFogGpuTest, SliceCoordMatchesCpuSpec)
{
    // The composite samples the integrated volume at this z-texcoord; it is the
    // inverse of the exponential slice distribution and must agree with
    // viewDepthToFroxelSlice() (in the `(slice + 0.5)/resZ` form), or every
    // pixel reads the wrong froxel. Extract the production helper verbatim so a
    // drift in screen_quad.frag.glsl fails here (CLAUDE.md Rule 7).
    const std::string fnSrc = extractGlslFunction(
        readShaderFile("screen_quad.frag.glsl"), "volumetricSliceCoord");
    ASSERT_FALSE(fnSrc.empty());

    ShaderProgram prog(
        "#version 450 core\n"
        "layout(location = 0) out vec4 outColor;\n"
        "uniform float u_viewDepth;\n"
        "uniform float u_near;\n"
        "uniform float u_far;\n"
        + fnSrc +
        "void main() {\n"
        "    outColor = vec4(volumetricSliceCoord(u_viewDepth, u_near, u_far),"
        " 0.0, 0.0, 1.0);\n"
        "}\n");
    ASSERT_TRUE(prog.valid());

    const FroxelGridConfig g = smallGrid();  // near 0.5, far 50, resZ 16
    // Include a below-near and a beyond-far depth to exercise both clamps.
    const float depths[] = {0.1f, 0.5f, 1.0f, 5.0f, 12.5f, 25.0f, 50.0f, 100.0f};

    for (float vd : depths)
    {
        const glm::vec4 gpu = prog.run(
            {{"u_viewDepth", vd}, {"u_near", g.near}, {"u_far", g.far}});
        const float cpuCoord = std::clamp(
            (viewDepthToFroxelSlice(g, vd) + 0.5f) / static_cast<float>(g.resZ),
            0.0f, 1.0f);
        EXPECT_NEAR(gpu.r, cpuCoord, 1e-5f) << "slice coord @ viewDepth " << vd;
    }
}

} // namespace Vestige::Test
