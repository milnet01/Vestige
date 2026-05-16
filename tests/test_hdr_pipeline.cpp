// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_hdr_pipeline.cpp
/// @brief Unit tests for the HDR rendering pipeline math.
///
/// Phase 10.9 Slice 18 Ts1 cleanup: the previous Reinhard / ACES /
/// gamma-correction tests reimplemented the GLSL tonemappers as static
/// helpers in this file and asserted their own constants against
/// themselves. The shader at `assets/shaders/screen_quad.frag.glsl`
/// could be rewritten with wrong coefficients (e.g. ACES 2.43 → 2.34)
/// and every assertion would have stayed green. Replaced by a proper
/// GLSL-source-extracted parity test in `test_hdr_pipeline_parity.cpp`
/// (shipped in the same slice — Ts2 authoring phase).
///
/// Renderer state tests (exposure / tonemapMode / debugMode getters
/// and setters) require an OpenGL context and are verified at engine
/// launch.
#include <gtest/gtest.h>
#include <glm/glm.hpp>

// --- Luminance calculation ---
//
// BT.709 luminance coefficients are a documented IEC spec — the test
// pin protects against accidental coefficient drift if someone edits
// the constants below. The same coefficients are duplicated in
// `test_bloom.cpp::bt709Luminance` (Ts3 finding); the canonical
// home for the helper is the bloom file. These tests pin the
// invariants in isolation.

TEST(HDRPipelineTest, LuminanceWhite)
{
    // Luminance of white (1,1,1) = 0.2126 + 0.7152 + 0.0722 = 1.0
    glm::vec3 white(1.0f);
    float luminance = glm::dot(white, glm::vec3(0.2126f, 0.7152f, 0.0722f));
    EXPECT_NEAR(luminance, 1.0f, 0.001f);
}

TEST(HDRPipelineTest, LuminancePureGreenIsBrightest)
{
    // Green has the highest luminance coefficient (0.7152)
    glm::vec3 coefficients(0.2126f, 0.7152f, 0.0722f);
    float redLum = glm::dot(glm::vec3(1.0f, 0.0f, 0.0f), coefficients);
    float greenLum = glm::dot(glm::vec3(0.0f, 1.0f, 0.0f), coefficients);
    float blueLum = glm::dot(glm::vec3(0.0f, 0.0f, 1.0f), coefficients);
    EXPECT_GT(greenLum, redLum);
    EXPECT_GT(greenLum, blueLum);
}
