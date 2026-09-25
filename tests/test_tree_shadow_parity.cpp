// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_tree_shadow_parity.cpp
/// @brief Pins the copied screen-door dissolve between the tree's visible pass
///        and its shadow caster (3D_E-0685).
///
/// There is no GLSL `#include`, so `tree_shadow.frag.glsl` carries its own
/// copy of `interleavedGradientNoise` and of the signed dissolve that uses it.
/// The copy exists so the ground shadow fades in lockstep with the drawn
/// canopy. If the two drift, nothing fails to build or link: the shadow just
/// dissolves on a different pattern from the tree above it. The GLSL text is
/// therefore the spec, as in test_grass_shadow_parity.cpp.

#include "shader_parity_helpers.h"

#include <gtest/gtest.h>

#include <regex>
#include <string>

namespace
{

using Vestige::Test::extractGlslFunction;
using Vestige::Test::readShaderFile;

/// Strip `//` comments and collapse whitespace, so a reworded comment or a
/// re-indent is not drift but a changed constant or operator is.
std::string normalise(const std::string& src)
{
    const std::string noComments = std::regex_replace(src, std::regex("//[^\\n]*"), "");
    return std::regex_replace(noComments, std::regex("\\s+"), " ");
}

/// The signed dissolve: from the `dither` declaration through the closing
/// brace of the `else` branch. Empty if the shape is not found.
std::string extractDissolve(const std::string& src)
{
    const size_t start = src.find("float dither = interleavedGradientNoise(");
    if (start == std::string::npos)
        return {};
    const size_t elsePos = src.find("else", start);
    if (elsePos == std::string::npos)
        return {};
    const size_t open = src.find('{', elsePos);
    if (open == std::string::npos)
        return {};
    int depth = 0;
    for (size_t i = open; i < src.size(); ++i)
    {
        if (src[i] == '{')
            ++depth;
        else if (src[i] == '}' && --depth == 0)
            return src.substr(start, i + 1 - start);
    }
    return {};
}

}  // namespace

TEST(TreeShadowParity, NoiseFunctionMatchesTheVisiblePass)
{
    const std::string mesh = readShaderFile("tree_mesh.frag.glsl");
    const std::string shadow = readShaderFile("tree_shadow.frag.glsl");
    ASSERT_FALSE(mesh.empty());
    ASSERT_FALSE(shadow.empty());

    const std::string meshFn = extractGlslFunction(mesh, "interleavedGradientNoise");
    const std::string shadowFn = extractGlslFunction(shadow, "interleavedGradientNoise");
    ASSERT_FALSE(meshFn.empty());
    EXPECT_EQ(normalise(shadowFn), normalise(meshFn))
        << "tree_shadow.frag.glsl's interleavedGradientNoise has drifted from "
           "tree_mesh.frag.glsl's, so tree shadows no longer dissolve with the canopy";
}

TEST(TreeShadowParity, DissolveMatchesTheVisiblePass)
{
    const std::string mesh = readShaderFile("tree_mesh.frag.glsl");
    const std::string shadow = readShaderFile("tree_shadow.frag.glsl");

    const std::string meshBlock = extractDissolve(mesh);
    const std::string shadowBlock = extractDissolve(shadow);
    ASSERT_FALSE(meshBlock.empty()) << "dissolve block not found in tree_mesh.frag.glsl";
    ASSERT_FALSE(shadowBlock.empty()) << "dissolve block not found in tree_shadow.frag.glsl";
    EXPECT_EQ(normalise(shadowBlock), normalise(meshBlock))
        << "the signed dissolve in tree_shadow.frag.glsl no longer matches "
           "tree_mesh.frag.glsl's";
}
