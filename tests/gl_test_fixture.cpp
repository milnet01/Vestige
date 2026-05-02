// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "gl_test_fixture.h"

#include "core/logger.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <string>

namespace Vestige::Test
{

namespace
{

GLFWwindow* g_window         = nullptr;
bool        g_initialized    = false;

// Register the environment with gtest at static-init so the test runner
// (which uses GTest::gtest_main) picks it up before RUN_ALL_TESTS().
// AddGlobalTestEnvironment takes ownership of the heap-allocated object.
const auto* g_envHandle = ::testing::AddGlobalTestEnvironment(
    new GLTestEnvironment);

}  // namespace

void GLTestEnvironment::SetUp()
{
    if (!glfwInit())
    {
        Logger::warning("GLTestEnvironment: glfwInit failed — GL parity "
                        "tests will skip. (No display? Try `xvfb-run`.)");
        g_initialized = false;
        return;
    }

    // Match production hints (engine/core/window.cpp). The only addition
    // is GLFW_VISIBLE=FALSE so the parity tests don't pop a window.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    g_window = glfwCreateWindow(16, 16, "vestige_tests_gl_ctx", nullptr, nullptr);
    if (!g_window)
    {
        Logger::warning("GLTestEnvironment: glfwCreateWindow failed — GL "
                        "parity tests will skip.");
        glfwTerminate();
        g_initialized = false;
        return;
    }

    glfwMakeContextCurrent(g_window);

    int version = gladLoadGL(glfwGetProcAddress);
    if (!version)
    {
        Logger::warning("GLTestEnvironment: gladLoadGL failed — GL parity "
                        "tests will skip.");
        glfwDestroyWindow(g_window);
        g_window = nullptr;
        glfwTerminate();
        g_initialized = false;
        return;
    }

    g_initialized = true;
    Logger::info(std::string("GLTestEnvironment: ")
        + reinterpret_cast<const char*>(glGetString(GL_VERSION))
        + " on "
        + reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
}

void GLTestEnvironment::TearDown()
{
    if (g_window)
    {
        glfwDestroyWindow(g_window);
        g_window = nullptr;
    }
    if (g_initialized)
    {
        glfwTerminate();
    }
    g_initialized = false;
}

bool GLTestEnvironment::wasInitialized()
{
    return g_initialized;
}

void GLTestFixture::SetUp()
{
    if (!GLTestEnvironment::wasInitialized())
    {
        GTEST_SKIP() << "no GL context available "
                        "(see GLTestEnvironment startup log)";
    }
}

}  // namespace Vestige::Test
