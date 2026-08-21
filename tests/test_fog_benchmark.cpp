// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_fog_benchmark.cpp
/// @brief Phase 10 slice 11.6 — volumetric fog GPU benchmark (design § 8 / § 7).
///
/// Times the three froxel compute passes (inject → scatter → integrate) at the
/// shipped 160×90×64 grid and asserts the per-frame GPU cost stays inside the
/// 2.0 ms fog-stack budget the design pins for the High preset on the dev rig
/// (RX 6600, research § 7) — the hard 60 FPS floor for the volumetric path.
///
/// The dispatch is the dominant cost of the fog stack; the per-pixel composite
/// sample it feeds is a single texture fetch folded into the existing
/// screen-quad pass and is not separately timed here.
///
/// **Software-renderer guard.** The budget is defined on a real GPU. Headless
/// CI runs under llvmpipe, where the compute passes rasterise on the CPU and
/// the wall-clock is meaningless against a GPU budget. So the path always runs
/// (proving it does not crash), but the assertion only fires on hardware; under
/// a software renderer the test SKIPs after logging the measured median. This
/// is an environment guard, not a workaround — the gate is real wherever a real
/// GPU exists.
///
/// **Quality-tier guard (3D_E-0615).** Design § 8's budget table is per preset,
/// and both figures below are its **High** row — measured on the RX 6600 dev
/// rig. The table states no volumetric figure below High, and the Tier-1
/// scalability design § 4.2 has Low drop the pass for performance outright, so
/// there is no number to hold a lower-tier machine to. The preset being gated
/// for is read from `VESTIGE_QUALITY_PRESET` and defaults to High, so the dev
/// rig and CI are unchanged; a machine the preset system would not run at High
/// declares itself (see `scripts/wintest.sh`) and gets the median reported
/// rather than asserted. Same shape as the software-renderer guard above: an
/// environment guard, not a workaround.
///
/// **The god-ray gate is the exception (3D_E-0624).** It carries TWO budgets,
/// because it is the one pass here that runs on Low and Medium and not on
/// High/Ultra. A Low/Medium declaration therefore ASSERTS -- against a tier
/// budget derived from a 60 FPS frame rather than from the RX 6600 -- and the
/// pass is timed at that tier's own `renderScale`. Only `Custom`, and a box that
/// is neither the reference class nor a god-ray tier, still reports a median.
/// See design § 8, "How the two budgets are selected".
#include <gtest/gtest.h>

#include "renderer/volumetric_fog_pass.h"

#include "renderer/framebuffer.h"
#include "renderer/fullscreen_quad.h"
#include "renderer/shader.h"

#include "core/settings.h"
#include "core/settings_apply.h"

#include "gl_test_fixture.h"
#include "lsan_guard.h"

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace Vestige;

namespace
{
// design § 7: the full fog stack must stay inside 2.0 ms / frame at 1080p on
// the RX 6600 dev rig (hard 60 FPS floor). The froxel dispatch is the bulk of
// that stack.
constexpr double kVolumetricBudgetMicros = 2000.0;

// design § 11.2: the dynamic-GI inject is one extra RGBA16F 160×90×64 image
// write (~one fog pass). The primary gate R4 controls is the inject dispatch
// timed in isolation staying ≤ 0.4 ms on the RX 6600 at 1080p (est. ~0.3 ms).
constexpr double kGiInjectBudgetMicros = 400.0;

// design § 8 row "God rays, screen-space -- RX 6600 reference, renderScale 1.0
// | 0.6-1.2 ms". An RX 6600 @ 1080p figure like the two above. It is the
// REFERENCE row: High and Ultra are the presets that render at renderScale 1.0,
// and they are also the two that never run this pass (heavyPost = true), so this
// budget polices the shader on the dev rig rather than the tiers that ship it.
// The tiers get kGodRayTierBudgetMicros below.
//
// Gate the band's UPPER bound, the same choice the volumetric gate above makes
// in taking the 2.0 ms stack total rather than the ~1.2 ms pass figure. The
// shipped shader is at the band's low tap count (64) but its high per-tap
// content cost (2 samples), and § 8 records why the 64-tap end alone is too
// tight: research § 7's extrapolation is itself optimistic by ~1.7x on this
// texture-bandwidth-bound pass. 1.2 ms is still not slack -- mutating the
// shader's NUM_SAMPLES to its 128-tap ceiling measures 1.21 ms here and goes
// red, against 0.69 ms for the shipped 64-tap worst case.
constexpr double kGodRayBudgetMicros = 1200.0;

// design § 8 row "God rays, screen-space -- Low/Med tier budget | 1.75 ms"
// (3D_E-0624). NOT an RX 6600 figure and NOT fitted to any measurement -- it is
// derived from the frame: 16.6 ms at every tier (scalability strategy § 2), of
// which design § 8 allows the whole fog stack 2.0 ms (~12%), less the three
// always-on analytic layers' ceilings (< 0.05 + < 0.1 + < 0.1 = 0.25 ms). Low
// and Medium run no froxel pass, so god rays are the only heavy consumer of the
// remainder. That chain would have produced 1.75 ms before any weak GPU was
// measured, which is precisely what lets a measurement FAIL it -- a budget
// fitted to the box it polices can only ever pass.
constexpr double kGodRayTierBudgetMicros = 1750.0;

// True when the active GL renderer is a software rasteriser (llvmpipe /
// softpipe / swrast). A wall-clock budget written for a real GPU does not apply.
bool isSoftwareRenderer()
{
    const char* r = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    if (!r) return true;  // can't tell → treat as untrusted, skip the gate
    std::string s(r);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(::tolower(c)); });
    return s.find("llvmpipe") != std::string::npos
        || s.find("softpipe") != std::string::npos
        || s.find("swrast")   != std::string::npos
        || s.find("software") != std::string::npos;
}

// The quality preset these budgets are being gated for. Defaults to High, the
// preset design § 8's table states them for, so an ordinary run on the dev rig
// or in CI behaves exactly as before. A machine the preset system would not run
// at High sets VESTIGE_QUALITY_PRESET to say so.
QualityPreset gatedPreset()
{
    const char* env = std::getenv("VESTIGE_QUALITY_PRESET");
    if (!env || !*env) return QualityPreset::High;
    // qualityPresetFromString matches lowercase only, and silently returns the
    // fallback otherwise — so an env value spelled "Medium" would read as High
    // and the gate would fire anyway. Fold the case here rather than making the
    // caller remember.
    std::string name(env);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(::tolower(c)); });
    return qualityPresetFromString(name, QualityPreset::High);
}

// Whether this MACHINE is the hardware class every budget in this file was
// measured on. That is the one thing the three gates below share, and it is a
// question about the GPU rather than about which passes a preset runs.
//
// Every figure here -- volumetric, GI inject and god-ray alike -- comes from
// design § 8 / § 11.2, which cite research § 7, whose table is headed
// "Performance Targets (RDNA2 / RX 6600)". VESTIGE_QUALITY_PRESET is how a box
// declares it is NOT that class: a machine the preset system would not run at
// High (see scripts/wintest.sh) reports its median instead of asserting it.
// Ultra is High's grid or larger on the same class of GPU, so it qualifies.
//
// Read it as a hardware-class check, NOT as "does this preset run this pass"
// (3D_E-0616). The two questions give OPPOSITE answers for god rays, which run
// only where the froxel pass does not -- so a predicate meaning the second
// would have to be inverted between the gates below, and it is not.
bool budgetsApplyToThisMachine(QualityPreset q)
{
    return q == QualityPreset::High || q == QualityPreset::Ultra;
}

// Whether the god-ray gate uses the TIER budget rather than the RX 6600
// reference one. This is the "does this preset run the pass" question, and it is
// deliberately a SEPARATE predicate from budgetsApplyToThisMachine above rather
// than an inversion of it: that one keeps its hardware-class meaning and the
// volumetric and GI gates keep using it unchanged (3D_E-0624, design § 8 "How
// the two budgets are selected"). The tier branch runs AHEAD of it, so the
// reference row is still protected by the hardware-class question it was
// measured under.
bool usesTierGodRayBudget(QualityPreset q)
{
    return q == QualityPreset::Low || q == QualityPreset::Medium;
}

// A sink that discards every renderer-side knob. applyQualityPreset writes the
// render scale to the DisplaySettings and everything else here; only the scale
// is wanted.
class NullQualitySink final : public RendererQualitySink
{
public:
    void setAntiAliasMode(AntiAliasMode) override {}
    void setSsaoEnabled(bool) override {}
    void setBloomEnabled(bool) override {}
    void setHeavyPostEnabled(bool) override {}
    void setTerrainGroundQuality(TerrainGroundQuality) override {}
    void setFoliageQuality(FoliageQuality) override {}
    void setGrassQuality(GrassQuality) override {}
    void setTreeQuality(TreeQuality) override {}
};

// The internal-resolution factor this preset renders at, read back from the
// SHIPPED applyQualityPreset instead of copied out of the Tier-1 design § 4.1
// table. A benchmark that copied it would be a fourth place the preset table
// lives, and would silently keep timing the old resolution after a preset
// change -- the two-copy drift shape 3D_E-0617 was filed for.
//
// Custom returns from applyQualityPreset without writing a scale, leaving
// DisplaySettings' 1.0 default -- which is what design § 8's selection table
// specifies for it, so that the median it reports stays comparable with the
// 0.69 ms and 3.1 ms figures this project sets its budgets from.
float tierRenderScale(QualityPreset q)
{
    DisplaySettings display;
    NullQualitySink sink;
    applyQualityPreset(q, display, sink);
    return display.renderScale;
}

// 1×1 lit depth array so the benchmark times the shadowed (god-ray) scatter
// path — the shipped default — not just the unshadowed lobe.
GLuint makeLitShadowArray()
{
    GLuint tex = 0;
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &tex);
    glTextureStorage3D(tex, 1, GL_DEPTH_COMPONENT32F, 1, 1, 1);
    const float lit = 1.0f;
    glClearTexImage(tex, 0, GL_DEPTH_COMPONENT, GL_FLOAT, &lit);
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return tex;
}

// Full-res (1080p) 2D texture for the GI inject's per-froxel att3 / depth
// samples — representative of a shipped frame's sampler cache behaviour.
GLuint makeSceneResTex(int width, int height, GLenum internalFormat, GLenum format,
                       float clearValue)
{
    GLuint tex = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &tex);
    glTextureStorage2D(tex, 1, internalFormat, width, height);
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const glm::vec4 v(clearValue);
    glClearTexImage(tex, 0, format, GL_FLOAT, &v.x);
    return tex;
}

GLuint makeFullResTex(GLenum internalFormat, GLenum format, float clearValue)
{
    return makeSceneResTex(1920, 1080, internalFormat, format, clearValue);
}
}  // namespace

class FogBenchmarkTest : public ::Vestige::Test::GLTestFixture
{
};

TEST_F(FogBenchmarkTest, VolumetricDispatchUnderBudget)
{
    VolumetricFogPass pass;  // default 160×90×64 grid (the shipped config)
    ASSERT_TRUE(pass.init(VESTIGE_SHADER_DIR));

    // Representative frame: shadowed sun lobe (csmCascadeCount = 1) so the timed
    // path matches the shipped god-ray default.
    const GLuint litMap = makeLitShadowArray();
    VolumetricFogPass::FrameParams p;
    p.scattering  = glm::vec3(0.02f);
    p.extinction  = 0.02f;
    p.anisotropy  = 0.3f;
    p.sunRadiance = glm::vec3(2.0f);
    p.ambient     = glm::vec3(0.05f);
    p.csmCascadeCount          = 1;
    p.csmCascadeSplits[0]      = 1.0e6f;
    p.csmLightSpaceMatrices[0] = glm::mat4(1.0f);
    p.invView                  = glm::mat4(1.0f);
    p.csmShadowTexture         = litMap;

    // Slice 11.8: the renderer ships density noise on by default, so the gate
    // must include the inject-pass FBM cost (3 octaves). Matches the renderer's
    // provisional look constants.
    p.noise.enabled      = true;
    p.noise.frequency    = 0.03f;
    p.noise.strength     = 0.5f;
    p.noise.octaves      = 3;
    p.noise.windVelocity = glm::vec3(0.4f, 0.0f, 0.15f);
    p.elapsedSeconds     = 1.0f;

    // Slice 11.11: time a representative heavy mist scene. A real perspective
    // projection (instead of the uniform-medium tests' identity) gives the
    // froxels sane world positions, so localized volumes overlap a realistic
    // fraction of the grid — froxels outside a volume skip its turbulence FBM
    // via the `falloff > 0` guard, exactly as in a shipped frame. Twelve
    // volumes scattered through the frustum, half animated, is a rich scene
    // (the Tabernacle's morning mist + altar dust + incense need far fewer).
    p.invProjection = glm::inverse(
        glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 500.0f));
    for (int i = 0; i < 12; ++i)
    {
        FogVolume v;
        v.shape        = (i % 3 == 0) ? FogVolumeShape::Sphere : FogVolumeShape::Box;
        // View-space -z is forward; spread the volumes across depth and screen.
        const float fi = static_cast<float>(i);
        v.center       = glm::vec3(fi * 4.0f - 22.0f, fi * 2.0f - 12.0f, -(8.0f + fi * 12.0f));
        v.halfExtents  = glm::vec3(14.0f, 10.0f, 14.0f);
        v.edgeSoftness = 0.3f;
        v.density      = 0.03f;
        v.animSpeed    = (i % 2 == 0) ? 0.5f : 0.0f; // half animated (FBM path)
        p.volumes.push_back(v);
    }

    // First dispatch JIT-compiles pipe state the driver never frees — a
    // process-lifetime third-party allocation, not a Vestige leak.
    Vestige::Test::ScopedLeakCheckDisable noLeakTracking;

    auto timeDispatch = [&]() -> double
    {
        glFinish();  // drain prior GPU work so t0 starts clean
        const auto t0 = std::chrono::steady_clock::now();
        pass.dispatch(p);
        glFinish();  // wait for the three passes to complete
        const auto t1 = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::micro>(t1 - t0).count();
    };

    for (int i = 0; i < 3; ++i) timeDispatch();  // warm the pipeline

    constexpr int kFrames = 8;
    std::vector<double> micros;
    micros.reserve(kFrames);
    for (int f = 0; f < kFrames; ++f) micros.push_back(timeDispatch());
    std::sort(micros.begin(), micros.end());
    const double medianMicros = micros[micros.size() / 2];

    glDeleteTextures(1, &litMap);

#if !defined(NDEBUG)
    GTEST_SKIP() << "non-optimised (Debug) build — volumetric dispatch median "
                 << medianMicros << " µs not gated against the "
                 << kVolumetricBudgetMicros
                 << " µs budget (enforced in optimised builds).";
#endif

    if (isSoftwareRenderer())
    {
        GTEST_SKIP() << "software renderer ("
                     << reinterpret_cast<const char*>(glGetString(GL_RENDERER))
                     << ") — volumetric dispatch median " << medianMicros
                     << " µs not gated against the " << kVolumetricBudgetMicros
                     << " µs GPU budget.";
    }

    if (!budgetsApplyToThisMachine(gatedPreset()))
    {
        GTEST_SKIP() << "quality preset " << qualityPresetLabel(gatedPreset())
                     << " — " << kVolumetricBudgetMicros
                     << " µs is design § 8's RX 6600 stack budget, and this box"
                        " is not that class. (No volumetric figure is published"
                        " below High either: Tier-1 design § 4.1 drops the pass"
                        " at Low and Medium, so those tiers dispatch no froxel"
                        " grid at all.) Volumetric dispatch median "
                     << medianMicros << " µs recorded, not gated.";
    }

    EXPECT_LE(medianMicros, kVolumetricBudgetMicros)
        << "volumetric froxel dispatch (160×90×64) median " << medianMicros
        << " µs exceeds the " << kVolumetricBudgetMicros << " µs fog-stack budget";
}

// Slice R4 (design § 11.6 test 8): the GI inject dispatch timed in isolation
// against the 0.4 ms gate, plus a combined fog+GI report against the fog
// baseline. Same Debug / software-renderer guards as the fog gate above.
TEST_F(FogBenchmarkTest, GiInjectDispatchUnderBudget)
{
    VolumetricFogPass pass;  // default 160×90×64 grid (the shipped config)
    ASSERT_TRUE(pass.init(VESTIGE_SHADER_DIR));

    GLuint att3  = makeFullResTex(GL_RGBA16F, GL_RGBA, 0.5f);
    GLuint depth = makeFullResTex(GL_R32F,    GL_RED,  0.9f);

    const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 500.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f),
                                       glm::vec3(0.0f, 1.0f, 0.0f));
    VolumetricFogPass::GiFrameParams gp;
    gp.invProjection      = glm::inverse(proj);
    gp.invView            = glm::inverse(view);
    gp.prevViewProjection = proj * view;   // mostly-warm: exercises the history fetch
    gp.prevView           = view;
    gp.injectionSourceTex = att3;
    gp.sceneDepthTex      = depth;

    Vestige::Test::ScopedLeakCheckDisable noLeakTracking;

    auto timeGi = [&]() -> double
    {
        glFinish();
        const auto t0 = std::chrono::steady_clock::now();
        pass.dispatchGi(gp);
        glFinish();
        const auto t1 = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::micro>(t1 - t0).count();
    };

    for (int i = 0; i < 3; ++i) timeGi();  // warm

    constexpr int kFrames = 8;
    std::vector<double> micros;
    micros.reserve(kFrames);
    for (int f = 0; f < kFrames; ++f) micros.push_back(timeGi());
    std::sort(micros.begin(), micros.end());
    const double medianMicros = micros[micros.size() / 2];

    glDeleteTextures(1, &att3);
    glDeleteTextures(1, &depth);

#if !defined(NDEBUG)
    GTEST_SKIP() << "non-optimised (Debug) build — GI inject dispatch median "
                 << medianMicros << " µs not gated against the "
                 << kGiInjectBudgetMicros << " µs budget.";
#endif

    if (isSoftwareRenderer())
    {
        GTEST_SKIP() << "software renderer ("
                     << reinterpret_cast<const char*>(glGetString(GL_RENDERER))
                     << ") — GI inject dispatch median " << medianMicros
                     << " µs not gated against the " << kGiInjectBudgetMicros
                     << " µs GPU budget.";
    }

    if (!budgetsApplyToThisMachine(gatedPreset()))
    {
        GTEST_SKIP() << "quality preset " << qualityPresetLabel(gatedPreset())
                     << " — " << kGiInjectBudgetMicros
                     << " µs is design § 11.2's High-preset gate; GI inject"
                        " median " << medianMicros << " µs recorded, not gated.";
    }

    EXPECT_LE(medianMicros, kGiInjectBudgetMicros)
        << "GI inject dispatch (160×90×64) median " << medianMicros
        << " µs exceeds the " << kGiInjectBudgetMicros << " µs budget (design § 11.2)";
}

// 3D_E-0616: the screen-space god-ray pass (slice 11.5) against design § 8's
// "God rays, screen-space (Low/Med)" row. Nothing benchmarked this pass before
// — design § 5.8 said so outright ("no standalone subsystem class ... so there
// is no separate micro-benchmark; its cost rides in the full composite"), which
// left the one fog technique the lower presets DO run with no gate at all.
//
// It has no class to instantiate, so this reconstructs the shipped pair of
// draws from the same two fragment shaders and the same screen-quad vertex
// stage the renderer links (`renderer.cpp` § 4.9): pass A gathers 64 radial
// taps at half-res into an RGBA16F target, pass B additively combines that into
// the full-res HDR scene. A GLSL regression in either — a raised NUM_SAMPLES, a
// full-res gather — shows up here as a budget failure.
//
// **The gather RESOLUTION is a copy, and is NOT gated.** `gatherCfg` below
// hard-codes 1920/2 x 1080/2 rather than reading renderer.cpp's godRaysConfig,
// which has no shared constant to pin against. So this catches a raised
// NUM_SAMPLES (the shader is loaded from disk) but would NOT catch godRaysConfig
// being changed to full-res -- it would keep timing a half-res gather and stay
// green. Same two-copy drift shape as 3D_E-0617, one layer up; stated here and
// in design § 8 rather than left for a reader to discover.
//
// **All-sky worst case, on purpose.** The gather's per-tap cost is one depth
// texelFetch plus a scene fetch taken only where the pixel is sky (reverse-Z
// depth ≈ 0). Clearing the depth to 0 makes every tap pay both fetches with no
// branch divergence — the most expensive frame the shader can be handed, and a
// deterministic one. A gate the worst case passes, every real frame passes.
TEST_F(FogBenchmarkTest, GodRayPassUnderBudget)
{
    const std::string vert = std::string(VESTIGE_SHADER_DIR) + "/screen_quad.vert.glsl";

    Shader gather;
    ASSERT_TRUE(gather.loadFromFiles(
        vert, std::string(VESTIGE_SHADER_DIR) + "/god_rays.frag.glsl"));
    Shader combine;
    ASSERT_TRUE(combine.loadFromFiles(
        vert, std::string(VESTIGE_SHADER_DIR) + "/god_rays_combine.frag.glsl"));

    // The tier's internal resolution. Resolved HERE, above the timed region,
    // because the framebuffer and source-texture sizes are fixed before the
    // timing lambda runs -- the budget half of the selection sits below the
    // guards further down (design § 8, "The selection splits across the timed
    // region"). Putting the whole branch below them would time the pass at
    // renderScale 1.0 and then judge it against the tier row: 3.1 ms against
    // 1.75 ms on the GTX 1050, permanently red on the one box it exists for.
    const float renderScale = tierRenderScale(gatedPreset());
    const int   internalW   = static_cast<int>(std::lround(1920.0 * renderScale));
    const int   internalH   = static_cast<int>(std::lround(1080.0 * renderScale));

    // Pre-bloom HDR scene + resolved depth at the internal res, as the renderer
    // binds them (`resizeRenderTarget` sizes the resolve/TAA-scene targets to
    // the already-scaled res). These SOURCES, not the draw targets, are what the
    // pass's cost is bound by -- 64 taps, each a depth texelFetch plus a scene
    // fetch -- so timing a scaled gather against 1080p sources would carry a
    // working set no tier draws and inflate the result (3D_E-0624).
    // Depth 0.0 = sky in reverse-Z (see god_rays.frag.glsl `lightAt`).
    const GLuint sceneTex = makeSceneResTex(internalW, internalH, GL_RGBA16F, GL_RGBA, 1.0f);
    const GLuint depthTex =
        makeSceneResTex(internalW, internalH, GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, 0.0f);

    // Half-res RGBA16F gather target — renderer.cpp's `godRaysConfig`, which is
    // the SSAO config at width/2 × height/2 with isFloatingPoint set. The half
    // and the 1920×1080 base are still copies: renderer.cpp open-codes the half
    // at two sites with no named constant, and the base lives on
    // Editor::m_playModeWidth/Height. Only the renderScale is derived.
    FramebufferConfig gatherCfg;
    gatherCfg.width              = internalW / 2;
    gatherCfg.height             = internalH / 2;
    gatherCfg.samples            = 1;
    gatherCfg.hasColorAttachment = true;
    gatherCfg.hasDepthAttachment = false;
    gatherCfg.isFloatingPoint    = true;
    Framebuffer godRaysFbo(gatherCfg);
    ASSERT_TRUE(godRaysFbo.isComplete());

    // The full-internal-res HDR scene FBO pass B blends into.
    FramebufferConfig sceneCfg = gatherCfg;
    sceneCfg.width  = internalW;
    sceneCfg.height = internalH;
    Framebuffer hdrFbo(sceneCfg);
    ASSERT_TRUE(hdrFbo.isComplete());

    FullscreenQuad quad;

    // First draw JIT-compiles pipe state the driver never frees — a
    // process-lifetime third-party allocation, not a Vestige leak.
    Vestige::Test::ScopedLeakCheckDisable noLeakTracking;

    auto timeGodRays = [&]() -> double
    {
        glFinish();  // drain prior GPU work so t0 starts clean
        const auto t0 = std::chrono::steady_clock::now();

        // Pass A — half-res radial gather.
        godRaysFbo.bind();
        glViewport(0, 0, godRaysFbo.getWidth(), godRaysFbo.getHeight());
        gather.use();
        glBindTextureUnit(0, sceneTex);
        gather.setInt("u_sceneTexture", 0);
        glBindTextureUnit(1, depthTex);
        gather.setInt("u_depthTexture", 1);
        gather.setVec2("u_sunUV", glm::vec2(0.5f, 0.5f));
        gather.setFloat("u_intensity", 1.0f);  // > 0, else the shader early-outs
        quad.draw();

        // Pass B — full-res additive upsample-combine.
        glBindFramebuffer(GL_FRAMEBUFFER, hdrFbo.getId());
        glViewport(0, 0, internalW, internalH);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        combine.use();
        godRaysFbo.bindColorTexture(0);
        combine.setInt("u_godRaysTexture", 0);
        quad.draw();
        glDisable(GL_BLEND);

        glFinish();  // wait for both draws to complete
        const auto t1 = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::micro>(t1 - t0).count();
    };

    for (int i = 0; i < 3; ++i) timeGodRays();  // warm the pipeline

    constexpr int kFrames = 8;
    std::vector<double> micros;
    micros.reserve(kFrames);
    for (int f = 0; f < kFrames; ++f) micros.push_back(timeGodRays());
    std::sort(micros.begin(), micros.end());
    const double medianMicros = micros[micros.size() / 2];

    Framebuffer::unbind();
    glDeleteTextures(1, &sceneTex);
    glDeleteTextures(1, &depthTex);

#if !defined(NDEBUG)
    GTEST_SKIP() << "non-optimised (Debug) build — god-ray pass median "
                 << medianMicros
                 << " µs not gated against either budget (enforced in optimised"
                    " builds).";
#endif

    if (isSoftwareRenderer())
    {
        GTEST_SKIP() << "software renderer ("
                     << reinterpret_cast<const char*>(glGetString(GL_RENDERER))
                     << ") — god-ray pass median " << medianMicros
                     << " µs not gated against either GPU budget. This guard"
                        " applies to the tier budget too: that figure is derived"
                        " from a frame rather than from a GPU, but the thing"
                        " measured is still a GPU wall-clock (3D_E-0624).";
    }

    // Always record the median, whichever branch follows. A passing EXPECT_LE is
    // silent, so without this a green run says nothing about how close it came --
    // and this gate's margin is narrow enough that the distance matters as much
    // as the verdict (3D_E-0624).
    std::cout << "[   PERF   ] god-ray pass median " << medianMicros
              << " µs at renderScale " << renderScale << " (" << (internalW / 2) << "×"
              << (internalH / 2) << " gather + " << internalW << "×" << internalH
              << " combine), preset " << qualityPresetLabel(gatedPreset()) << "\n";

    // Low and Medium are the presets that actually RUN this pass, and they get
    // the tier budget -- derived from a 60 FPS frame, not from any box, so it is
    // assertable on whatever hardware declares that tier. This branch runs ahead
    // of the hardware-class check below on purpose: that check keeps its own
    // meaning for the reference row and for the volumetric and GI gates
    // (3D_E-0624, design § 8).
    if (usesTierGodRayBudget(gatedPreset()))
    {
        EXPECT_LE(medianMicros, kGodRayTierBudgetMicros)
            << "screen-space god-ray pass at the " << qualityPresetLabel(gatedPreset())
            << " preset (renderScale " << renderScale << " → " << (internalW / 2) << "×"
            << (internalH / 2) << " gather + " << internalW << "×" << internalH
            << " combine) median " << medianMicros << " µs exceeds design § 8's "
            << kGodRayTierBudgetMicros
            << " µs Low/Med tier budget. That budget is derived from a 60 FPS"
               " frame and is NOT to be raised to fit this measurement -- the"
               " remedy is a cheaper god-ray config at this tier (fewer taps, or"
               " a quarter-res gather), and a resolution change must move the"
               " design § 8 row with it (3D_E-0624).";
        return;
    }

    if (!budgetsApplyToThisMachine(gatedPreset()))
    {
        GTEST_SKIP() << "quality preset " << qualityPresetLabel(gatedPreset())
                     << " — " << kGodRayBudgetMicros
                     << " µs is research § 7's RX 6600 figure and this box is"
                        " not that class, and the preset is not one that runs"
                        " god rays either, so neither budget applies. God-ray"
                        " pass median " << medianMicros << " µs recorded at"
                        " renderScale " << renderScale << ".";
    }

    EXPECT_LE(medianMicros, kGodRayBudgetMicros)
        << "screen-space god-ray pass (64-tap gather at " << (internalW / 2) << "×"
        << (internalH / 2) << " + full-res combine) median " << medianMicros
        << " µs exceeds design § 8's " << kGodRayBudgetMicros
        << " µs RX 6600 reference budget";
}
