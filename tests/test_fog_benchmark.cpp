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
#include <gtest/gtest.h>

#include "renderer/volumetric_fog_pass.h"

#include "core/settings.h"

#include "gl_test_fixture.h"
#include "lsan_guard.h"

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
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

// design § 8 gives a volumetric figure for the High row only. Ultra is High's
// froxel grid or larger, so the High budget is still the floor it must beat.
// Below High there is no published volumetric number and there should not be:
// Tier-1 design § 4.1's preset table drops the pass at BOTH Low and Medium
// (§ 4.2 describes the shared mechanism; § 4.1's table is what assigns it per
// preset), so those tiers never dispatch a froxel grid to measure. Their
// published budget is design § 8's screen-space god-ray row, 0.3-0.6 ms, and
// gating that is 3D_E-0616 -- not a volumetric figure for Medium, which would
// police a pass that does not run.
bool budgetApplies(QualityPreset q)
{
    return q == QualityPreset::High || q == QualityPreset::Ultra;
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
GLuint makeFullResTex(GLenum internalFormat, GLenum format, float clearValue)
{
    GLuint tex = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &tex);
    glTextureStorage2D(tex, 1, internalFormat, 1920, 1080);
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const glm::vec4 v(clearValue);
    glClearTexImage(tex, 0, format, GL_FLOAT, &v.x);
    return tex;
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

    if (!budgetApplies(gatedPreset()))
    {
        GTEST_SKIP() << "quality preset " << qualityPresetLabel(gatedPreset())
                     << " — " << kVolumetricBudgetMicros
                     << " µs is design § 8's High-preset stack budget and no"
                        " volumetric figure is published below High; volumetric"
                        " dispatch median " << medianMicros
                     << " µs recorded, not gated.";
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

    if (!budgetApplies(gatedPreset()))
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
