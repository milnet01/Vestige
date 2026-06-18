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
#include <gtest/gtest.h>

#include "renderer/volumetric_fog_pass.h"

#include "gl_test_fixture.h"
#include "lsan_guard.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string>
#include <vector>

using namespace Vestige;

namespace
{
// design § 7: the full fog stack must stay inside 2.0 ms / frame at 1080p on
// the RX 6600 dev rig (hard 60 FPS floor). The froxel dispatch is the bulk of
// that stack.
constexpr double kVolumetricBudgetMicros = 2000.0;

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

    EXPECT_LE(medianMicros, kVolumetricBudgetMicros)
        << "volumetric froxel dispatch (160×90×64) median " << medianMicros
        << " µs exceeds the " << kVolumetricBudgetMicros << " µs fog-stack budget";
}
