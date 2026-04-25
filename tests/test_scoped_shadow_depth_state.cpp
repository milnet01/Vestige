// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_scoped_shadow_depth_state.cpp
/// @brief Phase 10.9 Slice 4 R3 — shadow-pass GL state RAII contract.
///
/// Contract (authored from ROADMAP Phase 10.9 Slice 4 R3 and the
/// `engine/renderer/scoped_shadow_depth_state.h` design):
///
///   The shadow pass must save / apply / restore `GL_CLIP_DISTANCE0`
///   and `GL_DEPTH_CLAMP` so a caller that had either bit set (e.g.
///   the water reflection / refraction pass leaves
///   `GL_CLIP_DISTANCE0` enabled to clip the underwater half of the
///   scene) sees its state preserved across the shadow pass.
///
///   Before R3 the bits were toggled with bare `glDisable` /
///   `glEnable` calls inside `renderShadowPass` that didn't snapshot
///   the prior state — `GL_CLIP_DISTANCE0` was permanently disabled
///   after the first shadow render, and `GL_DEPTH_CLAMP` was always
///   left off (which happened to be the global default but isn't
///   guaranteed forever).
///
///   `ScopedShadowDepthStateImpl<Io>` enforces:
///     - ctor calls `Io::save()` to snapshot the prior state.
///     - ctor calls `Io::applyShadowState()` to set the shadow-pass
///       values (`GL_CLIP_DISTANCE0` off, `GL_DEPTH_CLAMP` on).
///     - dtor calls `Io::restore(saved)` with the original snapshot.
///
/// The test injects a `RecordingIo` whose `save` / `applyShadowState`
/// / `restore` calls push events to a per-test trace plus a savedState
/// register. A stub dtor that omits the restore call fails every
/// invariant below; the green commit's full body passes them all.

#include <gtest/gtest.h>

#include "renderer/scoped_shadow_depth_state.h"

#include <string>
#include <vector>

namespace
{

struct RecordingIo
{
    struct SavedState
    {
        bool clipDistance0 = false;
        bool depthClamp = false;
    };

    static thread_local std::vector<std::string>* trace;
    static thread_local SavedState restored;
    static thread_local SavedState savedReturn;

    static SavedState save()
    {
        if (trace) trace->push_back("save");
        return savedReturn;
    }

    static void applyShadowState()
    {
        if (trace) trace->push_back("apply");
    }

    static void restore(const SavedState& saved)
    {
        if (trace) trace->push_back("restore");
        restored = saved;
    }
};

thread_local std::vector<std::string>* RecordingIo::trace = nullptr;
thread_local RecordingIo::SavedState RecordingIo::restored = {};
thread_local RecordingIo::SavedState RecordingIo::savedReturn = {};

class ScopedShadowDepthStateTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_trace.clear();
        RecordingIo::trace = &m_trace;
        RecordingIo::restored = {};
        RecordingIo::savedReturn = {};
    }

    void TearDown() override
    {
        RecordingIo::trace = nullptr;
    }

    std::vector<std::string> m_trace;
};

} // namespace

using namespace Vestige;

TEST_F(ScopedShadowDepthStateTest, ConstructionCallsSaveThenApply_R3)
{
    {
        ScopedShadowDepthStateImpl<RecordingIo> guard;
        // Restore is not yet expected — the guard is still alive.
        ASSERT_GE(m_trace.size(), 2u);
        EXPECT_EQ(m_trace[0], "save");
        EXPECT_EQ(m_trace[1], "apply");
    }
    // Out of scope — restore must have fired.
    ASSERT_EQ(m_trace.size(), 3u);
    EXPECT_EQ(m_trace[2], "restore");
}

TEST_F(ScopedShadowDepthStateTest, DestructionCallsRestoreWithSavedState_R3)
{
    // Pretend the caller had GL_CLIP_DISTANCE0 enabled and
    // GL_DEPTH_CLAMP disabled when the shadow pass started.
    RecordingIo::SavedState priorState;
    priorState.clipDistance0 = true;
    priorState.depthClamp = false;
    RecordingIo::savedReturn = priorState;

    {
        ScopedShadowDepthStateImpl<RecordingIo> guard;
    }

    // The restore must have been called with EXACTLY the snapshot
    // that `save()` returned — that's the headline R3 invariant. A
    // dtor that just calls `glDisable(GL_DEPTH_CLAMP)` (the pre-R3
    // shadow pass's manual cleanup) would leave clipDistance0
    // permanently off, which is what R3 closes.
    EXPECT_TRUE(RecordingIo::restored.clipDistance0);
    EXPECT_FALSE(RecordingIo::restored.depthClamp);
}

TEST_F(ScopedShadowDepthStateTest, RestoreFiresEvenOnEarlyScopeExit_R3)
{
    bool reachedAfter = false;
    {
        ScopedShadowDepthStateImpl<RecordingIo> guard;
        // No early-return here, but the RAII contract is that any exit
        // path triggers the dtor — pin it so a future refactor that
        // adds early returns can't accidentally bypass restore.
        reachedAfter = true;
    }

    EXPECT_TRUE(reachedAfter);
    ASSERT_FALSE(m_trace.empty());
    EXPECT_EQ(m_trace.back(), "restore");
}

TEST_F(ScopedShadowDepthStateTest, RestorePreservesClipDistance0OffState_R3)
{
    // Caller previously had GL_CLIP_DISTANCE0 disabled — the most
    // common case (only water passes flip it on). The guard must
    // preserve "off-ness" too, not just "on-ness".
    //
    // The trace check below is load-bearing: a stub dtor that omits
    // restore() leaves `RecordingIo::restored` at its default-init
    // {false, false} value, so the EXPECT_FALSE pair below matches
    // by accident (false-positive). The trace check pins that the
    // restore call actually happened.
    RecordingIo::SavedState priorState;
    priorState.clipDistance0 = false;
    priorState.depthClamp = false;
    RecordingIo::savedReturn = priorState;

    {
        ScopedShadowDepthStateImpl<RecordingIo> guard;
    }

    ASSERT_FALSE(m_trace.empty());
    EXPECT_EQ(m_trace.back(), "restore");
    EXPECT_FALSE(RecordingIo::restored.clipDistance0);
    EXPECT_FALSE(RecordingIo::restored.depthClamp);
}

TEST_F(ScopedShadowDepthStateTest, RestorePreservesBothOnState_R3)
{
    // Unusual case: caller had both bits enabled. Pin that the guard
    // restores both to "on" rather than collapsing to "off".
    RecordingIo::SavedState priorState;
    priorState.clipDistance0 = true;
    priorState.depthClamp = true;
    RecordingIo::savedReturn = priorState;

    {
        ScopedShadowDepthStateImpl<RecordingIo> guard;
    }

    EXPECT_TRUE(RecordingIo::restored.clipDistance0);
    EXPECT_TRUE(RecordingIo::restored.depthClamp);
}

TEST_F(ScopedShadowDepthStateTest, NestedGuardsRestoreInLifo_R3)
{
    // Two nested guards (e.g. an inner shadow sub-pass) must each
    // restore their own snapshot at scope exit, in reverse order
    // of construction.
    //
    // The ASSERT_GE bounds-checks below are load-bearing: against the
    // RED stub (empty dtor) the EXPECT_EQ size checks fail, leaving
    // the trace vector smaller than its expected indices — without
    // the asserts, the subsequent `m_trace[4]` / `m_trace[5]`
    // accesses dereference past `end()` and SEGV inside gtest's
    // failure-message formatter.
    {
        ScopedShadowDepthStateImpl<RecordingIo> outer;
        EXPECT_EQ(m_trace.size(), 2u);
        {
            ScopedShadowDepthStateImpl<RecordingIo> inner;
            EXPECT_EQ(m_trace.size(), 4u);
        }
        // Inner dtor fired.
        EXPECT_EQ(m_trace.size(), 5u);
        ASSERT_GE(m_trace.size(), 5u);
        EXPECT_EQ(m_trace[4], "restore");
    }
    // Outer dtor fired.
    EXPECT_EQ(m_trace.size(), 6u);
    ASSERT_GE(m_trace.size(), 6u);
    EXPECT_EQ(m_trace[5], "restore");
}
