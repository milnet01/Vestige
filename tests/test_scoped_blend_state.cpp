// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_scoped_blend_state.cpp
/// @brief Phase 10.9 Slice 4 R4 — ScopedBlendState bracket contract.
///
/// Same shape as R3's ScopedShadowDepthState test. Pin: ctor saves
/// + applies the requested state; dtor restores the snapshot.

#include <gtest/gtest.h>

#include "renderer/scoped_blend_state.h"

#include <string>
#include <vector>

namespace
{

struct RecordingBlendIo
{
    struct SavedState
    {
        bool enabled = false;
        GLenum srcRgb = GL_ONE;
        GLenum dstRgb = GL_ZERO;
        GLenum srcAlpha = GL_ONE;
        GLenum dstAlpha = GL_ZERO;
    };

    static thread_local std::vector<std::string>* trace;
    static thread_local SavedState restored;
    static thread_local SavedState savedReturn;
    static thread_local bool        appliedEnable;
    static thread_local GLenum      appliedSrc;
    static thread_local GLenum      appliedDst;

    static SavedState save()
    {
        if (trace) trace->push_back("save");
        return savedReturn;
    }

    static void apply(bool enable, GLenum src, GLenum dst)
    {
        if (trace) trace->push_back("apply");
        appliedEnable = enable;
        appliedSrc = src;
        appliedDst = dst;
    }

    static void restore(const SavedState& saved)
    {
        if (trace) trace->push_back("restore");
        restored = saved;
    }
};

thread_local std::vector<std::string>* RecordingBlendIo::trace = nullptr;
thread_local RecordingBlendIo::SavedState RecordingBlendIo::restored = {};
thread_local RecordingBlendIo::SavedState RecordingBlendIo::savedReturn = {};
thread_local bool   RecordingBlendIo::appliedEnable = false;
thread_local GLenum RecordingBlendIo::appliedSrc = 0;
thread_local GLenum RecordingBlendIo::appliedDst = 0;

class ScopedBlendStateTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_trace.clear();
        RecordingBlendIo::trace = &m_trace;
        RecordingBlendIo::restored = {};
        RecordingBlendIo::savedReturn = {};
        RecordingBlendIo::appliedEnable = false;
        RecordingBlendIo::appliedSrc = 0;
        RecordingBlendIo::appliedDst = 0;
    }
    void TearDown() override { RecordingBlendIo::trace = nullptr; }
    std::vector<std::string> m_trace;
};

} // namespace

using namespace Vestige;

TEST_F(ScopedBlendStateTest, ConstructionCallsSaveThenApply_R4)
{
    {
        ScopedBlendStateImpl<RecordingBlendIo> g(true, GL_SRC_ALPHA,
                                                   GL_ONE_MINUS_SRC_ALPHA);
        ASSERT_GE(m_trace.size(), 2u);
        EXPECT_EQ(m_trace[0], "save");
        EXPECT_EQ(m_trace[1], "apply");
    }
    ASSERT_EQ(m_trace.size(), 3u);
    EXPECT_EQ(m_trace[2], "restore");
}

TEST_F(ScopedBlendStateTest, ApplyForwardsConstructorParameters_R4)
{
    {
        ScopedBlendStateImpl<RecordingBlendIo> g(true, GL_SRC_ALPHA,
                                                   GL_ONE_MINUS_SRC_ALPHA);
        EXPECT_TRUE(RecordingBlendIo::appliedEnable);
        EXPECT_EQ(RecordingBlendIo::appliedSrc, static_cast<GLenum>(GL_SRC_ALPHA));
        EXPECT_EQ(RecordingBlendIo::appliedDst, static_cast<GLenum>(GL_ONE_MINUS_SRC_ALPHA));
    }
}

// Slice 18 Ts4: dropped `DestructionRestoresSnapshottedState_R4` —
// `RestorePreservesCallerHadBlendOnState_R4` below subsumes it (both
// test the same restore-from-snapshot contract; the latter exercises
// the more informative caller-was-blending case where a bug would
// silently restore the wrong factors).

TEST_F(ScopedBlendStateTest, RestorePreservesCallerHadBlendOnState_R4)
{
    // Caller had an existing alpha blend active. The pass changes
    // factors (e.g. additive blend); the RAII must restore the
    // alpha-blend factors, not foliage's choice.
    RecordingBlendIo::SavedState prior;
    prior.enabled = true;
    prior.srcRgb = GL_SRC_ALPHA;
    prior.dstRgb = GL_ONE_MINUS_SRC_ALPHA;
    prior.srcAlpha = GL_ONE;
    prior.dstAlpha = GL_ONE_MINUS_SRC_ALPHA;
    RecordingBlendIo::savedReturn = prior;

    {
        ScopedBlendStateImpl<RecordingBlendIo> g(true, GL_ONE, GL_ONE);
    }

    EXPECT_TRUE(RecordingBlendIo::restored.enabled);
    EXPECT_EQ(RecordingBlendIo::restored.srcRgb, static_cast<GLenum>(GL_SRC_ALPHA));
    EXPECT_EQ(RecordingBlendIo::restored.dstRgb,
              static_cast<GLenum>(GL_ONE_MINUS_SRC_ALPHA));
}
