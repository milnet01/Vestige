// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_scoped_cull_face.cpp
/// @brief Phase 10.9 Slice 4 R4 — ScopedCullFace bracket contract.

#include <gtest/gtest.h>

#include "renderer/scoped_cull_face.h"

#include <string>
#include <vector>

namespace
{

struct RecordingCullIo
{
    struct SavedState
    {
        bool enabled = false;
    };

    static thread_local std::vector<std::string>* trace;
    static thread_local SavedState restored;
    static thread_local SavedState savedReturn;
    static thread_local bool appliedEnable;

    static SavedState save()
    {
        if (trace) trace->push_back("save");
        return savedReturn;
    }
    static void apply(bool enable)
    {
        if (trace) trace->push_back("apply");
        appliedEnable = enable;
    }
    static void restore(const SavedState& saved)
    {
        if (trace) trace->push_back("restore");
        restored = saved;
    }
};

thread_local std::vector<std::string>* RecordingCullIo::trace = nullptr;
thread_local RecordingCullIo::SavedState RecordingCullIo::restored = {};
thread_local RecordingCullIo::SavedState RecordingCullIo::savedReturn = {};
thread_local bool RecordingCullIo::appliedEnable = false;

class ScopedCullFaceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_trace.clear();
        RecordingCullIo::trace = &m_trace;
        RecordingCullIo::restored = {};
        RecordingCullIo::savedReturn = {};
        RecordingCullIo::appliedEnable = false;
    }
    void TearDown() override { RecordingCullIo::trace = nullptr; }
    std::vector<std::string> m_trace;
};

} // namespace

using namespace Vestige;

TEST_F(ScopedCullFaceTest, ConstructionCallsSaveThenApply_R4)
{
    {
        ScopedCullFaceImpl<RecordingCullIo> g(false);
        ASSERT_GE(m_trace.size(), 2u);
        EXPECT_EQ(m_trace[0], "save");
        EXPECT_EQ(m_trace[1], "apply");
    }
    ASSERT_EQ(m_trace.size(), 3u);
    EXPECT_EQ(m_trace[2], "restore");
}

TEST_F(ScopedCullFaceTest, ApplyForwardsConstructorBool_R4)
{
    {
        ScopedCullFaceImpl<RecordingCullIo> g(false);
        EXPECT_FALSE(RecordingCullIo::appliedEnable);
    }
    {
        ScopedCullFaceImpl<RecordingCullIo> g(true);
        EXPECT_TRUE(RecordingCullIo::appliedEnable);
    }
}

TEST_F(ScopedCullFaceTest, RestorePreservesCallerEnabledState_R4)
{
    // Caller had cull ON, foliage disables it, RAII restores ON.
    RecordingCullIo::savedReturn = {true};
    {
        ScopedCullFaceImpl<RecordingCullIo> g(false);
    }
    EXPECT_TRUE(RecordingCullIo::restored.enabled);
}

TEST_F(ScopedCullFaceTest, RestorePreservesCallerDisabledState_R4)
{
    // Caller had cull OFF (e.g. editor debug-draw mode); foliage's
    // own disable-then-re-enable would have incorrectly turned it
    // back on. RAII preserves OFF.
    RecordingCullIo::savedReturn = {false};
    {
        ScopedCullFaceImpl<RecordingCullIo> g(false);
    }
    EXPECT_FALSE(RecordingCullIo::restored.enabled);
}

TEST_F(ScopedCullFaceTest, NestedGuardsRestoreInLifo_R4)
{
    // Same nesting contract pinned for ScopedShadowDepthState in R3.
    {
        ScopedCullFaceImpl<RecordingCullIo> outer(false);
        EXPECT_EQ(m_trace.size(), 2u);
        {
            ScopedCullFaceImpl<RecordingCullIo> inner(true);
            EXPECT_EQ(m_trace.size(), 4u);
        }
        EXPECT_EQ(m_trace.size(), 5u);
        ASSERT_GE(m_trace.size(), 5u);
        EXPECT_EQ(m_trace[4], "restore");
    }
    EXPECT_EQ(m_trace.size(), 6u);
    ASSERT_GE(m_trace.size(), 6u);
    EXPECT_EQ(m_trace[5], "restore");
}
