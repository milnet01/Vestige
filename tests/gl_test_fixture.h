// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#pragma once

/// @file gl_test_fixture.h
/// @brief GL context fixture for shader CPU/GPU parity tests.
///
/// One hidden GLFW window + OpenGL 4.5 core context is created at process
/// start via `::testing::Environment` and destroyed at process end. Tests
/// derive from `GLTestFixture` and call `requireGLContext()` in SetUp;
/// the base implementation issues `GTEST_SKIP()` when the environment
/// failed to acquire a context (Mesa/Wayland combinations without a
/// usable display, headless CI runners without `xvfb-run`, etc.).
///
/// Deliberately simple: no input handling, no resize callbacks, no
/// double-buffering. The fixture exists only so shader-parity tests can
/// compile a fragment shader against single-pixel inputs and compare
/// `glReadPixels` output to a CPU oracle.

#include <gtest/gtest.h>

namespace Vestige::Test
{

/// @brief Environment-scoped GL context. Registered in main() of the
///        test runner via `::testing::AddGlobalTestEnvironment`.
///
/// `wasInitialized()` returns true only when both `glfwInit` and the
/// 4.5-core context creation succeeded. Tests check this and skip if
/// false rather than fail — a missing GL context on a CI runner is an
/// environment limitation, not a test failure.
class GLTestEnvironment : public ::testing::Environment
{
public:
    void SetUp() override;
    void TearDown() override;

    static bool wasInitialized();
};

/// @brief Base fixture for GL-context tests. Skip if no context.
///
/// Usage:
///   class MyShaderParityTest : public ::Vestige::Test::GLTestFixture {};
///   TEST_F(MyShaderParityTest, FooMatchesCpu) { ... }
///
/// `SetUp()` calls `GTEST_SKIP()` when the GL context could not be
/// initialised; subclasses can override for additional setup but must
/// invoke `GLTestFixture::SetUp()` first to keep the skip path live.
class GLTestFixture : public ::testing::Test
{
protected:
    void SetUp() override;
};

}  // namespace Vestige::Test
