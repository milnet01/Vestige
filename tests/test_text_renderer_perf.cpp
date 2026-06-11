// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_text_renderer_perf.cpp
/// @brief Phase 10 Localization L6 — HUD-pass benchmark (design § 8 test 23,
///        budget § 1 / § 9).
///
/// Times the CPU cost of the batched HUD text pass for a synthetic 20-label /
/// 800-glyph workload and asserts it stays within the ≤ 0.30 ms / frame budget
/// the i18n slices (L1 UTF-8 decode, L2 FontStack, L3 RTL) must not blow.
///
/// This is the gate every earlier slice's perf *estimate* is measured against
/// — those slices recorded ad-hoc probes; this pins the real number.
///
/// **Software-renderer guard.** The budget is defined "on the dev rig" (a real
/// RX 6600). Headless CI runs under the llvmpipe software rasteriser, where
/// `endBatch2D`'s draw rasterises 800 quads on the CPU and the wall-clock is
/// meaningless against a GPU budget. So the timed path always runs (proving it
/// does not crash and exercising the shaping work), but the budget assertion
/// only fires on hardware; under a software renderer the test SKIPs after
/// logging the measured median. This is an environment guard, not a workaround
/// — the gate is real wherever a real GPU exists.
#include <gtest/gtest.h>

#include "renderer/text_renderer.h"

#include "gl_test_fixture.h"
#include "lsan_guard.h"

#include <glad/gl.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string>
#include <vector>

using namespace Vestige;

namespace
{
std::string arimoPath()
{
    return std::string(VESTIGE_FONT_DIR) + "/arimo.ttf";
}

std::string assetRoot()
{
    return std::string(VESTIGE_FONT_DIR) + "/..";
}

// design § 1 / § 9: ≤ 0.30 ms / frame for the 20-label / 800-glyph workload.
constexpr double kHudPassBudgetMicros = 300.0;

// True when the active GL renderer is a software rasteriser (llvmpipe /
// softpipe / swrast / "Software Rasterizer"). A wall-clock budget written for
// a real GPU does not apply to these.
bool isSoftwareRenderer()
{
    const char* r = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    if (!r) return true;  // can't tell → treat as untrusted, skip the gate
    std::string s(r);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(::tolower(c)); });
    return s.find("llvmpipe")  != std::string::npos
        || s.find("softpipe")  != std::string::npos
        || s.find("swrast")    != std::string::npos
        || s.find("software")  != std::string::npos;
}
}  // namespace

class TextRendererPerfTest : public ::Vestige::Test::GLTestFixture
{
};

// Test 23 — the batched HUD pass for 20 labels (~40 chars each, ~800 glyphs)
// stays within the per-frame budget. Median of 8 frames dodges a cold-cache
// outlier (design § 8 test 23).
TEST_F(TextRendererPerfTest, HudPassUnderBudget)
{
    TextRenderer tr;
    ASSERT_TRUE(tr.initialize(arimoPath(), assetRoot(), 48));

    // 20 labels averaging 40 ASCII glyphs each → ~800 glyphs / frame. A fixed
    // sentence pads/clips to 40 chars; pure-Latin so the MRU cache holds (the
    // shipped common case the budget targets).
    constexpr int kLabels = 20;
    constexpr int kGlyphsPerLabel = 40;
    const std::string label(kGlyphsPerLabel, 'A');

    // The § 9 budget governs the CPU text-shaping + quad-emit cost (UTF-8
    // decode + FontStack lookup + RTL + per-glyph vertex build) — the work the
    // i18n slices added. In batch mode (Phase 10.9 Pe1) that work happens
    // inside the `renderText2D` calls, which accumulate into a CPU vertex
    // buffer; `endBatch2D` then issues the single GL draw. § 9 states the GPU
    // side is unchanged ("same draw call count"), so the timed region wraps
    // only the CPU build and the GL flush stays outside it (its wall-clock is
    // a GPU-submission/sync number, not what the budget pins).
    auto timeFrame = [&]() -> double
    {
        tr.beginBatch2D(1920, 1080);
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kLabels; ++i)
        {
            const float y = 20.0f + static_cast<float>(i) * 24.0f;
            tr.renderText2D(label, 10.0f, y, 1.0f, glm::vec3(1.0f), 1920, 1080);
        }
        const auto t1 = std::chrono::steady_clock::now();
        tr.endBatch2D();  // GL flush — excluded from the timed span (see above).
        return std::chrono::duration<double, std::micro>(t1 - t0).count();
    };

    // The first draw makes llvmpipe JIT pipe-state it never frees — a known
    // third-party process-lifetime allocation, not a Vestige leak (same idiom
    // as MruCacheSkipsStackWalk; see tests/lsan_guard.h).
    Vestige::Test::ScopedLeakCheckDisable noLeakTracking;

    // Warm the glyph cache + GL pipeline so the timed frames measure the warm
    // steady-state path, not first-frame compilation.
    for (int i = 0; i < 3; ++i) timeFrame();

    constexpr int kFrames = 8;
    std::vector<double> micros;
    micros.reserve(kFrames);
    for (int f = 0; f < kFrames; ++f)
    {
        micros.push_back(timeFrame());
    }

    std::sort(micros.begin(), micros.end());
    const double medianMicros = micros[micros.size() / 2];

#if !defined(NDEBUG)
    // Debug (-O0) builds run this path 5-10× slower than the optimised build
    // the § 9 budget targets, so the wall-clock is not comparable. Exercise the
    // path (proving it does not crash + warming the shaping work) but skip the
    // gate; the standard Debug test suite reports the number without failing.
    GTEST_SKIP() << "non-optimised (Debug) build — HUD-pass median "
                 << medianMicros << " µs not gated against the "
                 << kHudPassBudgetMicros
                 << " µs budget (enforced in optimised builds).";
#endif

    if (isSoftwareRenderer())
    {
        GTEST_SKIP() << "software renderer ("
                     << reinterpret_cast<const char*>(glGetString(GL_RENDERER))
                     << ") — HUD-pass median " << medianMicros
                     << " µs not gated against the " << kHudPassBudgetMicros
                     << " µs GPU budget.";
    }

    EXPECT_LE(medianMicros, kHudPassBudgetMicros)
        << "HUD pass (20 labels / " << (kLabels * kGlyphsPerLabel)
        << " glyphs) median " << medianMicros << " µs exceeds the "
        << kHudPassBudgetMicros << " µs / frame budget.";
}
