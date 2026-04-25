// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_ibl_capture_sequence.cpp
/// @brief Phase 10.9 Slice 4 R1 — IBL capture-sequence guard contract.
///
/// Contract (authored from ROADMAP Phase 10.9 Slice 4 R1 and the
/// `engine/renderer/ibl_capture_sequence.h` design):
///
///   The IBL capture sequence (capture / irradiance / prefilter / BRDF
///   LUT) runs cubemap face captures that need standard NDC depth
///   (`GL_NEGATIVE_ONE_TO_ONE` + `GL_LESS` + clearDepth 1.0). The
///   engine's scene draw uses reverse-Z (`GL_ZERO_TO_ONE` +
///   `GL_GEQUAL` + clearDepth 0.0). `glClipControl` is global state —
///   without an outer `ScopedForwardZ` guard, either the IBL passes
///   render against a reverse-Z depth buffer (corrupting the captured
///   cubemap) or the engine is left in forward-Z afterward
///   (corrupting every subsequent scene draw).
///
///   `runIblCaptureSequenceWith<Guard>(steps)` enforces the bracket:
///   `Guard` is constructed before any step runs and destructed after
///   every step has returned, even when `steps` is empty.
///
/// The test injects a `RecordingGuard` mock whose ctor pushes "BEGIN"
/// and dtor pushes "END" to a per-test trace, then asserts the trace
/// shape. A stub helper that runs the steps without constructing the
/// guard fails every assertion below (no BEGIN, no END). Once the
/// helper's body is `Guard guard; for (...) step();`, all assertions
/// pass.

#include <gtest/gtest.h>

#include "renderer/ibl_capture_sequence.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace
{

// Per-test trace pointer. Tests set it in SetUp; the `RecordingGuard`
// and step lambdas each push events to it through this pointer so
// their writes are observable from the test body.
thread_local std::vector<std::string>* g_iblTrace = nullptr;

struct RecordingGuard
{
    RecordingGuard()  { if (g_iblTrace) g_iblTrace->push_back("BEGIN"); }
    ~RecordingGuard() { if (g_iblTrace) g_iblTrace->push_back("END"); }

    RecordingGuard(const RecordingGuard&) = delete;
    RecordingGuard& operator=(const RecordingGuard&) = delete;
    RecordingGuard(RecordingGuard&&) = delete;
    RecordingGuard& operator=(RecordingGuard&&) = delete;
};

class IblCaptureSequenceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_trace.clear();
        g_iblTrace = &m_trace;
    }

    void TearDown() override
    {
        g_iblTrace = nullptr;
    }

    std::vector<std::string> m_trace;
};

} // namespace

using namespace Vestige;

TEST_F(IblCaptureSequenceTest, EmptyStepsListStillBracketsGuard_R1)
{
    std::vector<std::function<void()>> steps;
    runIblCaptureSequenceWith<RecordingGuard>(steps);

    // Even with zero steps, the guard's ctor + dtor must run so a
    // `generate()` call that early-outs on a missing input still
    // restores GL state correctly.
    ASSERT_EQ(m_trace.size(), 2u);
    EXPECT_EQ(m_trace[0], "BEGIN");
    EXPECT_EQ(m_trace[1], "END");
}

TEST_F(IblCaptureSequenceTest, GuardOpensBeforeFirstStep_R1)
{
    std::vector<std::function<void()>> steps {
        []() { if (g_iblTrace) g_iblTrace->push_back("STEP_A"); }
    };
    runIblCaptureSequenceWith<RecordingGuard>(steps);

    ASSERT_EQ(m_trace.size(), 3u);
    EXPECT_EQ(m_trace[0], "BEGIN");
    EXPECT_EQ(m_trace[1], "STEP_A");
    EXPECT_EQ(m_trace[2], "END");
}

TEST_F(IblCaptureSequenceTest, StepsRunInOrderBetweenBeginAndEnd_R1)
{
    std::vector<std::function<void()>> steps {
        []() { if (g_iblTrace) g_iblTrace->push_back("CAPTURE"); },
        []() { if (g_iblTrace) g_iblTrace->push_back("IRRADIANCE"); },
        []() { if (g_iblTrace) g_iblTrace->push_back("PREFILTER"); },
        []() { if (g_iblTrace) g_iblTrace->push_back("BRDF_LUT"); },
    };
    runIblCaptureSequenceWith<RecordingGuard>(steps);

    ASSERT_EQ(m_trace.size(), 6u);
    EXPECT_EQ(m_trace[0], "BEGIN");
    EXPECT_EQ(m_trace[1], "CAPTURE");
    EXPECT_EQ(m_trace[2], "IRRADIANCE");
    EXPECT_EQ(m_trace[3], "PREFILTER");
    EXPECT_EQ(m_trace[4], "BRDF_LUT");
    EXPECT_EQ(m_trace[5], "END");
}

TEST_F(IblCaptureSequenceTest, GuardDestructsAfterLastStep_R1)
{
    std::vector<std::function<void()>> steps {
        []() { if (g_iblTrace) g_iblTrace->push_back("STEP_A"); },
        []() { if (g_iblTrace) g_iblTrace->push_back("STEP_B"); },
    };
    runIblCaptureSequenceWith<RecordingGuard>(steps);

    // The "destruct after last step" half of the bracket is the load-
    // bearing one for R1 — `EnvironmentMap::generate` is followed by a
    // scene draw that relies on reverse-Z being already restored. If
    // the guard tore down before the last step, BRDF LUT / prefilter
    // would render under the wrong clip mode.
    ASSERT_FALSE(m_trace.empty());
    EXPECT_EQ(m_trace.back(), "END");
    ASSERT_GE(m_trace.size(), 2u);
    EXPECT_EQ(m_trace[m_trace.size() - 2], "STEP_B");
}

TEST_F(IblCaptureSequenceTest, NullStepIsSkippedWithoutThrowing_R1)
{
    std::vector<std::function<void()>> steps {
        []() { if (g_iblTrace) g_iblTrace->push_back("STEP_A"); },
        std::function<void()>{},
        []() { if (g_iblTrace) g_iblTrace->push_back("STEP_C"); },
    };
    runIblCaptureSequenceWith<RecordingGuard>(steps);

    ASSERT_EQ(m_trace.size(), 4u);
    EXPECT_EQ(m_trace[0], "BEGIN");
    EXPECT_EQ(m_trace[1], "STEP_A");
    EXPECT_EQ(m_trace[2], "STEP_C");
    EXPECT_EQ(m_trace[3], "END");
}

TEST_F(IblCaptureSequenceTest, GuardLifetimeContainsEverySingleStep_R1)
{
    // Strong-form invariant: every "STEP_*" entry in the trace must
    // appear at an index strictly between the "BEGIN" and "END"
    // entries. This is what "wrap" actually means in ScopedForwardZ
    // terms — no step starts before the guard, none finishes after.
    std::vector<std::function<void()>> steps {
        []() { if (g_iblTrace) g_iblTrace->push_back("STEP_A"); },
        []() { if (g_iblTrace) g_iblTrace->push_back("STEP_B"); },
        []() { if (g_iblTrace) g_iblTrace->push_back("STEP_C"); },
    };
    runIblCaptureSequenceWith<RecordingGuard>(steps);

    auto beginIt = std::find(m_trace.begin(), m_trace.end(), "BEGIN");
    auto endIt = std::find(m_trace.begin(), m_trace.end(), "END");
    ASSERT_NE(beginIt, m_trace.end()) << "guard ctor never recorded BEGIN";
    ASSERT_NE(endIt, m_trace.end()) << "guard dtor never recorded END";
    ASSERT_LT(beginIt, endIt);

    for (auto it = m_trace.begin(); it != m_trace.end(); ++it)
    {
        if (*it == "BEGIN" || *it == "END") continue;
        EXPECT_LT(beginIt, it) << "step '" << *it << "' fired before BEGIN";
        EXPECT_LT(it, endIt) << "step '" << *it << "' fired after END";
    }
}
