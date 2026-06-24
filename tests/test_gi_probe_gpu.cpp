// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_gi_probe_gpu.cpp
/// @brief Phase 13 G1 (world-space GI) — headless GPU verification of the RSM
///        flux attachment. Runs on the GL test fixture's hidden 4.5-core context.
///
/// Covers the G1 gate (design §4 G1, §8):
///   * The shadow shaders that now write flux actually COMPILE + LINK
///     (shadow_depth, point_shadow_depth, foliage_shadow) — the att-0 flux output
///     and the new albedo/light uniforms must not break the depth pass.
///   * Flux readback matches the CPU reference: the production GLSL
///     `giRsmFluxDirectional` is extracted verbatim and pinned against the CPU
///     twin in gi_probe_math.h, so the two cannot drift (CLAUDE.md Rule 7).
///   * The CSM + point shadow FBOs are COMPLETE with the RGBA16F flux colour
///     attachment bound (depth + colour), and the flux textures exist.

#include "gl_test_fixture.h"
#include "shader_parity_helpers.h"

#include "renderer/cascaded_shadow_map.h"
#include "renderer/gi_probe_math.h"
#include "renderer/point_shadow_map.h"
#include "renderer/shader.h"

#include <gtest/gtest.h>

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <array>
#include <string>

namespace Vestige::Test
{

class GiProbeGpuTest : public GLTestFixture {};

// -----------------------------------------------------------------------------
// Shader compilation — the flux-writing shadow shaders must compile + link.
// -----------------------------------------------------------------------------

TEST_F(GiProbeGpuTest, ShadowFluxShadersCompileAndLink)
{
    const std::string dir = std::string(VESTIGE_SHADER_DIR) + "/";

    Shader csm;
    EXPECT_TRUE(csm.loadFromFiles(dir + "shadow_depth.vert.glsl",
                                  dir + "shadow_depth.frag.glsl"))
        << "shadow_depth (directional RSM flux) failed to compile/link";

    Shader point;
    EXPECT_TRUE(point.loadFromFiles(dir + "point_shadow_depth.vert.glsl",
                                    dir + "point_shadow_depth.frag.glsl"))
        << "point_shadow_depth (point RSM flux) failed to compile/link";

    Shader foliage;
    EXPECT_TRUE(foliage.loadFromFiles(dir + "foliage_shadow.vert.glsl",
                                      dir + "foliage_shadow.frag.glsl"))
        << "foliage_shadow (foliage RSM flux) failed to compile/link";
}

// -----------------------------------------------------------------------------
// Flux parity — the production GLSL giRsmFluxDirectional must equal the CPU twin
// across normals (including back-facing), light directions, and colours.
// -----------------------------------------------------------------------------

TEST_F(GiProbeGpuTest, RsmFluxDirectionalMatchesCpu)
{
    const std::string fn =
        extractGlslFunction(readShaderFile("shadow_depth.frag.glsl"),
                            "giRsmFluxDirectional");
    ASSERT_FALSE(fn.empty()) << "giRsmFluxDirectional not found in shadow_depth.frag.glsl";

    ShaderProgram prog(
        "#version 450 core\n"
        "layout(location = 0) out vec4 outColor;\n"
        "uniform vec3 u_albedo;\n"
        "uniform vec3 u_radiance;\n"
        "uniform vec3 u_normal;\n"
        "uniform vec3 u_lightDir;\n"
        + fn +
        "void main() { outColor = giRsmFluxDirectional(u_albedo, u_radiance, u_normal, u_lightDir); }\n");
    ASSERT_TRUE(prog.valid());

    struct Case { glm::vec3 albedo, radiance, normal, lightDir; };
    const std::array<Case, 6> cases = {{
        // Surface facing straight up, sun straight down ⇒ full N·L = 1.
        {{0.8f, 0.8f, 0.8f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},
        // Back-facing (normal away from the light) ⇒ clamped to zero.
        {{0.8f, 0.8f, 0.8f}, {1.0f, 1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},
        // Oblique incidence + coloured albedo and radiance.
        {{0.9f, 0.4f, 0.2f}, {1.2f, 1.1f, 0.9f}, {0.3f, 0.8f, 0.2f}, {-0.2f, -1.0f, -0.3f}},
        // Non-unit normal (must be renormalised inside the function).
        {{0.5f, 0.5f, 0.5f}, {2.0f, 2.0f, 2.0f}, {0.0f, 3.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},
        // Grazing angle.
        {{1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.05f, 0.0f}, {0.0f, -1.0f, 0.0f}},
        // HDR radiance > 1.
        {{0.6f, 0.7f, 0.8f}, {5.0f, 3.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},
    }};

    for (size_t i = 0; i < cases.size(); ++i)
    {
        const Case& c = cases[i];
        glm::vec4 gpu = prog.run({{"u_albedo", c.albedo},
                                  {"u_radiance", c.radiance},
                                  {"u_normal", c.normal},
                                  {"u_lightDir", c.lightDir}});
        glm::vec4 cpu = giRsmFluxDirectional(c.albedo, c.radiance, c.normal, c.lightDir);
        EXPECT_NEAR(gpu.r, cpu.r, 1e-4f) << "case " << i << " R";
        EXPECT_NEAR(gpu.g, cpu.g, 1e-4f) << "case " << i << " G";
        EXPECT_NEAR(gpu.b, cpu.b, 1e-4f) << "case " << i << " B";
        EXPECT_NEAR(gpu.a, cpu.a, 1e-4f) << "case " << i << " A";
    }
}

// -----------------------------------------------------------------------------
// FBO completeness — both shadow FBOs must stay complete with the flux colour
// attachment bound (depth + RGBA16F colour), and the flux textures must exist.
// -----------------------------------------------------------------------------

TEST_F(GiProbeGpuTest, CascadedShadowFboCompleteWithFlux)
{
    CascadedShadowConfig cfg;
    cfg.resolution = 64;   // small — completeness is format/attachment-driven
    cfg.cascadeCount = 2;
    CascadedShadowMap csm(cfg);

    EXPECT_NE(csm.fluxTextureArray(), 0u) << "flux texture array not created";

    csm.beginCascade(0);   // binds the FBO + attaches depth & flux layer 0
    EXPECT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE)
        << "CSM FBO incomplete with flux attachment";
    csm.endCascade();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

TEST_F(GiProbeGpuTest, PointShadowFboCompleteWithFlux)
{
    PointShadowConfig cfg;
    cfg.resolution = 64;
    PointShadowMap psm(cfg);

    EXPECT_NE(psm.fluxCubemap(), 0u) << "flux cubemap not created";

    psm.beginFace(0);      // binds the FBO + attaches depth & flux face 0
    EXPECT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE)
        << "point shadow FBO incomplete with flux attachment";
    psm.endFace();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

}  // namespace Vestige::Test
