// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_sun_scale_parity.cpp
/// @brief Pins one unit for light colours across the surface shaders
///        (3D_E-0808): diffuse light is surface colour / PI x light colour.
///
/// A shader that skips the division draws its surface about 3.14 x brighter
/// than its neighbours, and nothing fails: the meadow's sun was once tuned on
/// grass that skipped it, which left every prop dark. The GLSL text is the
/// spec here, as in the other shader parity tests (no GL context needed).

#include "shader_parity_helpers.h"

#include <gtest/gtest.h>

#include <regex>
#include <sstream>
#include <string>

namespace
{

using Vestige::Test::readShaderFile;

/// The code on @p line with any trailing // comment removed.
std::string codeOf(const std::string& line)
{
    return line.substr(0, line.find("//"));
}

}  // namespace

// Every line that lights a surface colour with a light's colour divides by PI.
TEST(SunScaleParity, EverySurfaceShaderDividesDiffuseLightByPi)
{
    const std::regex lightColour("u_lightColor|u_(dirLight|pointLights|spotLights)_diffuse");
    const std::regex surfaceColour("\\b(albedo|base|baseColor)\\b");

    for (const char* name : {"scene.frag.glsl", "terrain.frag.glsl", "grass.frag.glsl",
                             "foliage.frag.glsl", "tree_mesh.frag.glsl",})
    {
        std::istringstream source(readShaderFile(name));
        std::string line;
        int litLines = 0;
        while (std::getline(source, line))
        {
            const std::string code = codeOf(line);
            if (std::regex_search(code, lightColour) && std::regex_search(code, surfaceColour))
            {
                ++litLines;
                EXPECT_NE(code.find("/ PI"), std::string::npos) << name << ": " << line;
            }
        }
        EXPECT_GT(litLines, 0) << name << " has no line this test recognises";
    }
}

// Highlights and caustics use the sun colour with no surface colour on the
// line, so the check above cannot see them. They were tuned against a sun
// one PI lower and divide to keep their look.
TEST(SunScaleParity, WaterHighlightAndCausticsUseTheSameScale)
{
    EXPECT_NE(readShaderFile("water.frag.glsl").find("u_lightColor * specular * 0.6 / PI"),
              std::string::npos);
    EXPECT_NE(readShaderFile("scene.frag.glsl").find("length(u_dirLight_diffuse) * 0.5 / PI"),
              std::string::npos);
    EXPECT_NE(readShaderFile("terrain.frag.glsl").find("length(u_lightColor) * 0.25 / PI"),
              std::string::npos);
}
