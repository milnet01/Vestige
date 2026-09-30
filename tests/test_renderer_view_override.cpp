// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_renderer_view_override.cpp
/// @brief A probe capture's view override reaches every draw path (3D_E-0735).
///
/// `Renderer::renderScene` takes a view override for probe captures and stores
/// the view it is using in `m_lastView`. Three of its draw paths, and
/// `drawMesh`, handed the shader the main camera's view instead. Most static
/// geometry and the sky were then drawn from the main camera on every cubemap
/// face, so every probe in the SH grid captured the same picture and the
/// "probe lighting" was one colour everywhere.
///
/// No test here can build a Renderer (it needs a window and a GL context with
/// the whole asset set), so this reads the source: no `u_view` uniform may be
/// set from the camera's own view matrix. `m_lastView` equals the camera's view
/// whenever there is no override, so nothing legitimate is ruled out.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

TEST(RendererViewOverride, NoDrawPathTakesItsViewFromTheCamera)
{
    const std::filesystem::path source =
        std::filesystem::path(VESTIGE_SHADER_DIR) / ".." / ".." / "engine" / "renderer" / "renderer.cpp";
    std::ifstream in(source);
    ASSERT_TRUE(in.good()) << "cannot open " << source;

    std::string line;
    int lineNumber = 0;
    int uViewSets = 0;
    while (std::getline(in, line))
    {
        ++lineNumber;
        if (line.find("setMat4(\"u_view\"") == std::string::npos)
        {
            continue;
        }
        ++uViewSets;
        EXPECT_EQ(line.find("camera.getViewMatrix()"), std::string::npos)
            << "renderer.cpp:" << lineNumber << " sets u_view from the camera, which ignores a "
            << "capture's view override: " << line;
    }
    // The anchor is real: if the uniform is renamed this test must not pass by finding nothing.
    EXPECT_GT(uViewSets, 0) << "no u_view uniform found in " << source;
}
