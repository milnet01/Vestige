// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_gi_gpu.cpp
/// @brief Slice R4 (Variant A) — headless GPU verification of the dynamic-GI
///        pipeline. Runs on the GL test fixture's hidden 4.5-core context.
///
/// Covers the parts the CPU mirror (test_gi.cpp) cannot:
///   * The production shaders actually COMPILE + LINK — both the GI inject
///     compute (gi_inject.comp.glsl, via VolumetricFogPass::init) and the full
///     scene program (scene.vert + scene.frag, which gained the att3 GI
///     injection-source output, the per-light diffuse-direct out-params, and the
///     GI read). No other test compiles the whole scene shader.
///   * The `giSliceCoord` helper is duplicated in THREE places — gi_math.h (CPU),
///     gi_inject.comp.glsl, and scene.frag.glsl — pinned here so the copies
///     can't drift (CLAUDE.md Rule 7).
///   * The inject's depth-match gate (design §11.6 test 4): a froxel whose slice
///     depth matches the depth-buffer surface takes the injected radiance; a
///     froxel at a non-matching slice keeps its (cold) zero — run on the GPU and
///     read back with glGetTextureImage, checked against gi_math.h.
///
/// The energy-model (test 5) and additive-composition (test 6) guards live with
/// the scene-shader structure (the att3 write is the Lambertian diffuse only;
/// the read is `+= u_giStrength·gi.a·kD·gi.rgb·albedo·ao`); the CPU EMA + the
/// inject behaviour here pin the numerics. The frame-budget gate (test 8) lives
/// in test_fog_benchmark.cpp.

#include "gl_test_fixture.h"
#include "shader_parity_helpers.h"

#include "renderer/gi_math.h"
#include "renderer/shader.h"
#include "renderer/volumetric_fog.h"
#include "renderer/volumetric_fog_pass.h"

#include <gtest/gtest.h>

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <string>
#include <vector>

namespace Vestige::Test
{

namespace
{

// Small froxel grid keeps the readback cheap while still spanning enough slices
// to separate a matching slice from a non-matching one.
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

size_t idx(const FroxelGridConfig& g, int x, int y, int z)
{
    return (static_cast<size_t>((z * g.resY + y) * g.resX + x)) * 4;
}

std::vector<float> readbackRGBA(GLuint tex, const FroxelGridConfig& g)
{
    std::vector<float> px(static_cast<size_t>(g.resX * g.resY * g.resZ) * 4);
    glGetTextureImage(tex, 0, GL_RGBA, GL_FLOAT,
                      static_cast<GLsizei>(px.size() * sizeof(float)), px.data());
    return px;
}

// A 2D texture filled with one RGBA value — stands in for the att3 injection
// source (constant radiance everywhere).
GLuint makeConstColor(int w, int h, glm::vec4 v)
{
    GLuint tex = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &tex);
    glTextureStorage2D(tex, 1, GL_RGBA16F, w, h);
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glClearTexImage(tex, 0, GL_RGBA, GL_FLOAT, &v.x);
    return tex;
}

// A single-channel R32F texture filled with one depth value — stands in for the
// resolved scene depth (constant surface depth across the frame).
GLuint makeConstDepth(int w, int h, float depthNdc)
{
    GLuint tex = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &tex);
    glTextureStorage2D(tex, 1, GL_R32F, w, h);
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glClearTexImage(tex, 0, GL_RED, GL_FLOAT, &depthNdc);
    return tex;
}

}  // namespace

class GiGpuTest : public GLTestFixture {};

// -----------------------------------------------------------------------------
// Shader compilation — the production shaders must compile + link.
// -----------------------------------------------------------------------------

// The full scene program (gained the att3 GI source, the per-light
// diffuse-direct out-params, and the GI read in the PBR path). No other test
// compiles the whole scene shader, so this guards every R4 edit to it.
TEST_F(GiGpuTest, SceneShaderCompilesAndLinks)
{
    Shader scene;
    EXPECT_TRUE(scene.loadFromFiles(
        std::string(VESTIGE_SHADER_DIR) + "/scene.vert.glsl",
        std::string(VESTIGE_SHADER_DIR) + "/scene.frag.glsl"))
        << "scene.frag.glsl (R4 GI source + read) failed to compile/link";
}

// VolumetricFogPass::init loads gi_inject.comp.glsl among the fog passes; a
// successful init proves the inject compute compiles + links.
TEST_F(GiGpuTest, GiInjectShaderCompiles)
{
    VolumetricFogPass pass;
    EXPECT_TRUE(pass.init(VESTIGE_SHADER_DIR, smallGrid()))
        << "gi_inject.comp.glsl failed to compile/link";
    pass.destroy();
}

// -----------------------------------------------------------------------------
// giSliceCoord parity — the helper is duplicated in gi_math.h (CPU),
// gi_inject.comp.glsl, and scene.frag.glsl. Pin all three together.
// -----------------------------------------------------------------------------

TEST_F(GiGpuTest, GiSliceCoordMatchesCpuAcrossSources)
{
    const FroxelGridConfig cfg;  // default near 0.5, far 200

    for (const char* file : {"scene.frag.glsl", "gi_inject.comp.glsl"})
    {
        const std::string fn = extractGlslFunction(readShaderFile(file), "giSliceCoord");
        ASSERT_FALSE(fn.empty()) << "giSliceCoord not found in " << file;

        ShaderProgram prog(
            "#version 450 core\n"
            "layout(location = 0) out vec4 outColor;\n"
            "uniform float u_vd;\n"
            "uniform float u_near;\n"
            "uniform float u_far;\n"
            + fn +
            "void main() { outColor = vec4(giSliceCoord(u_vd, u_near, u_far), 0.0, 0.0, 1.0); }\n");
        ASSERT_TRUE(prog.valid()) << "compile failed for giSliceCoord from " << file;

        // Sweep [near, far] plus a touch beyond (clamp) and a degenerate <=0.
        for (int s = -2; s <= 202; ++s)
        {
            float vd = cfg.near + (cfg.far - cfg.near) * (static_cast<float>(s) / 200.0f);
            glm::vec4 gpu = prog.run({{"u_vd", vd},
                                      {"u_near", cfg.near},
                                      {"u_far", cfg.far}});
            float cpu = giVolumetricSliceCoord(vd, cfg.near, cfg.far);
            EXPECT_NEAR(gpu.r, cpu, 1e-5f) << file << " vd=" << vd;
        }
    }
}

// -----------------------------------------------------------------------------
// Inject depth-match gate (design §11.6 test 4) — a froxel whose slice depth
// matches the depth-buffer surface takes the injected radiance; a froxel at a
// non-matching slice keeps its cold zero. Forced COLD (history reprojects out of
// the previous frustum) so the matching froxel equals the injection exactly.
// -----------------------------------------------------------------------------

TEST_F(GiGpuTest, InjectWritesOnlyWhereDepthMatches)
{
    const FroxelGridConfig g = smallGrid();
    VolumetricFogPass pass;
    ASSERT_TRUE(pass.init(VESTIGE_SHADER_DIR, g));

    // Current camera at the origin looking down -Z; a square 60° projection.
    const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f),
                                       glm::vec3(0.0f, 1.0f, 0.0f));

    // Put the surface exactly at the centre depth of slice k* — only that slice
    // should pass the ±½-slice-thickness gate.
    const int   kStar = 8;
    const float dStar = froxelSliceToViewDepth(g, kStar);     // view-space metres
    // Encode dStar as the depth-buffer value the shader reconstructs against:
    // forward-project a view point at -dStar through `proj` (the shader inverts
    // it with the same matrix, so this round-trips regardless of clip convention).
    const glm::vec4 clip = proj * glm::vec4(0.0f, 0.0f, -dStar, 1.0f);
    const float depthNdc = clip.z / clip.w;
    ASSERT_GT(depthNdc, 0.0f) << "test depth must clear the >0 (reversed-Z) guard";

    const glm::vec3 injected(0.6f, 0.3f, 0.1f);
    GLuint att3  = makeConstColor(8, 8, glm::vec4(injected, 1.0f));
    GLuint depth = makeConstDepth(8, 8, depthNdc);

    VolumetricFogPass::GiFrameParams gp;
    gp.invProjection = glm::inverse(proj);
    gp.invView       = glm::inverse(view);
    // Previous camera faces the opposite way (+Z), so every froxel (all at world
    // -Z) reprojects BEHIND it ⇒ cold start, no blend against history.
    const glm::mat4 prevView = glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
                                           glm::vec3(0.0f, 1.0f, 0.0f));
    gp.prevViewProjection = proj * prevView;
    gp.prevView           = prevView;
    gp.injectionSourceTex = att3;
    gp.sceneDepthTex      = depth;
    gp.alpha = GI_ALPHA;
    gp.decay = GI_DECAY;

    pass.dispatchGi(gp);
    pass.swapGiBuffers();  // the just-written cache becomes giReadTexture()

    const std::vector<float> px = readbackRGBA(pass.giReadTexture(), g);
    pass.destroy();

    // Interior column, matching slice k*: cold + valid ⇒ rgb == injected, a == alpha.
    const size_t m = idx(g, 2, 2, kStar);
    EXPECT_NEAR(px[m + 0], injected.x, 2e-3f);
    EXPECT_NEAR(px[m + 1], injected.y, 2e-3f);
    EXPECT_NEAR(px[m + 2], injected.z, 2e-3f) << "matching froxel must take the injection";
    EXPECT_NEAR(px[m + 3], GI_ALPHA,   2e-3f) << "cold-start confidence == alpha";

    // Same column, a far-off slice: surface depth mismatches ⇒ invalid ⇒ zero.
    const size_t n = idx(g, 2, 2, 0);
    EXPECT_NEAR(px[n + 0], 0.0f, 1e-4f);
    EXPECT_NEAR(px[n + 1], 0.0f, 1e-4f);
    EXPECT_NEAR(px[n + 2], 0.0f, 1e-4f);
    EXPECT_NEAR(px[n + 3], 0.0f, 1e-4f) << "non-matching froxel stays cold/zero";

    glDeleteTextures(1, &att3);
    glDeleteTextures(1, &depth);
}

}  // namespace Vestige::Test
