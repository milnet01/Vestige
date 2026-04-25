// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_sampler_fallback.cpp
/// @brief Phase 10.9 Slice 4 R6 — SamplerFallback lazy-init + caching contract.
///
/// The helper itself doesn't fix the Mesa GL_INVALID_OPERATION
/// hazard on its own — the four call sites (foliage no-shadow,
/// water first-frame, gpu-particles no-collision, procedural
/// skybox) do. But the helper's contract is testable in isolation
/// without a GL context: each `get*()` call must trigger exactly
/// one `Creator::create*()` call across its lifetime, and return
/// the cached handle on subsequent calls. `shutdown()` must call
/// `deleteTexture` once per allocated handle.

#include <gtest/gtest.h>

#include "renderer/sampler_fallback.h"

#include <algorithm>
#include <vector>

namespace
{

struct MockTextureCreator
{
    static thread_local int sampler2DCreates;
    static thread_local int samplerCubeCreates;
    static thread_local int sampler2DArrayCreates;
    static thread_local int sampler3DCreates;
    static thread_local std::vector<GLuint>* deletes;

    static GLuint createSampler2D()      { return ++sampler2DCreates      + 100; }
    static GLuint createSamplerCube()    { return ++samplerCubeCreates    + 200; }
    static GLuint createSampler2DArray() { return ++sampler2DArrayCreates + 300; }
    static GLuint createSampler3D()      { return ++sampler3DCreates      + 400; }

    static void deleteTexture(GLuint name)
    {
        if (deletes) deletes->push_back(name);
    }
};

thread_local int MockTextureCreator::sampler2DCreates      = 0;
thread_local int MockTextureCreator::samplerCubeCreates    = 0;
thread_local int MockTextureCreator::sampler2DArrayCreates = 0;
thread_local int MockTextureCreator::sampler3DCreates      = 0;
thread_local std::vector<GLuint>* MockTextureCreator::deletes = nullptr;

class SamplerFallbackTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        MockTextureCreator::sampler2DCreates      = 0;
        MockTextureCreator::samplerCubeCreates    = 0;
        MockTextureCreator::sampler2DArrayCreates = 0;
        MockTextureCreator::sampler3DCreates      = 0;
        m_deletes.clear();
        MockTextureCreator::deletes = &m_deletes;
    }

    void TearDown() override
    {
        MockTextureCreator::deletes = nullptr;
    }

    std::vector<GLuint> m_deletes;
};

} // namespace

using namespace Vestige;

TEST_F(SamplerFallbackTest, FirstGetSampler2DCreatesOnce_R6)
{
    SamplerFallbackImpl<MockTextureCreator> fallback;
    GLuint h = fallback.getSampler2D();

    EXPECT_NE(h, 0u);
    EXPECT_EQ(MockTextureCreator::sampler2DCreates, 1);
}

TEST_F(SamplerFallbackTest, RepeatedGetSampler2DReturnsCachedHandle_R6)
{
    SamplerFallbackImpl<MockTextureCreator> fallback;
    GLuint h1 = fallback.getSampler2D();
    GLuint h2 = fallback.getSampler2D();
    GLuint h3 = fallback.getSampler2D();

    EXPECT_EQ(h1, h2);
    EXPECT_EQ(h2, h3);
    EXPECT_EQ(MockTextureCreator::sampler2DCreates, 1) << "must create exactly once";
}

TEST_F(SamplerFallbackTest, EachSamplerTypeCachedIndependently_R6)
{
    SamplerFallbackImpl<MockTextureCreator> fallback;
    GLuint h2D      = fallback.getSampler2D();
    GLuint hCube    = fallback.getSamplerCube();
    GLuint h2DArray = fallback.getSampler2DArray();
    GLuint h3D      = fallback.getSampler3D();

    // All four distinct handles (the mock encodes the type in the name range).
    EXPECT_NE(h2D, hCube);
    EXPECT_NE(h2D, h2DArray);
    EXPECT_NE(h2D, h3D);
    EXPECT_NE(hCube, h2DArray);
    EXPECT_NE(hCube, h3D);
    EXPECT_NE(h2DArray, h3D);

    // Each created once.
    EXPECT_EQ(MockTextureCreator::sampler2DCreates, 1);
    EXPECT_EQ(MockTextureCreator::samplerCubeCreates, 1);
    EXPECT_EQ(MockTextureCreator::sampler2DArrayCreates, 1);
    EXPECT_EQ(MockTextureCreator::sampler3DCreates, 1);
}

TEST_F(SamplerFallbackTest, ShutdownReleasesAllCreatedHandles_R6)
{
    SamplerFallbackImpl<MockTextureCreator> fallback;
    GLuint h2D      = fallback.getSampler2D();
    GLuint hCube    = fallback.getSamplerCube();
    GLuint h2DArray = fallback.getSampler2DArray();
    GLuint h3D      = fallback.getSampler3D();

    fallback.shutdown();

    ASSERT_EQ(m_deletes.size(), 4u);
    // Order isn't contractual; just check every created handle is in the
    // delete list.
    auto contains = [&](GLuint name) {
        return std::find(m_deletes.begin(), m_deletes.end(), name) != m_deletes.end();
    };
    EXPECT_TRUE(contains(h2D));
    EXPECT_TRUE(contains(hCube));
    EXPECT_TRUE(contains(h2DArray));
    EXPECT_TRUE(contains(h3D));
}

TEST_F(SamplerFallbackTest, ShutdownIsIdempotent_R6)
{
    SamplerFallbackImpl<MockTextureCreator> fallback;
    fallback.getSampler2D();
    fallback.shutdown();
    EXPECT_EQ(m_deletes.size(), 1u);

    // Second shutdown must not delete again.
    m_deletes.clear();
    fallback.shutdown();
    EXPECT_EQ(m_deletes.size(), 0u);
}

TEST_F(SamplerFallbackTest, ShutdownWithoutAnyGetIsNoOp_R6)
{
    SamplerFallbackImpl<MockTextureCreator> fallback;
    fallback.shutdown();
    EXPECT_EQ(m_deletes.size(), 0u);
}

TEST_F(SamplerFallbackTest, GetAfterShutdownReCreates_R6)
{
    // Defensive: if shutdown is called and the fallback is then reused,
    // the next get*() must create a new handle. (Allows graceful reset
    // without leaking the previous instance's handles.)
    SamplerFallbackImpl<MockTextureCreator> fallback;
    fallback.getSampler2D();
    fallback.shutdown();

    GLuint h = fallback.getSampler2D();
    EXPECT_NE(h, 0u);
    EXPECT_EQ(MockTextureCreator::sampler2DCreates, 2)
        << "shutdown then get must create again";
}
