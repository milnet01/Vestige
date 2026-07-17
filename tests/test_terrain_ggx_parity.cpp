// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_terrain_ggx_parity.cpp
/// @brief 3D_E-0031 A2 GGX-consistency gate (design §7). The textured terrain
///        path shades with the engine's canonical Cook-Torrance GGX — the
///        `distributionGGX` NDF is lifted verbatim from `scene.frag.glsl` — so
///        this test pins *reuse*, not fit accuracy:
///
///   1. `DistributionMatchesReferenceOnGpu` (GL): runs the ACTUAL
///      `distributionGGX` GLSL extracted from `terrain.frag.glsl` through a
///      single-pixel pass and compares it, over an NdotH × roughness grid, to
///      the `ggx_distribution` Formula-Library definition
///      (`PhysicsTemplates::createGGXDistribution`) — the documented reference
///      the shader helper is annotated as matching. The reference's free knob is
///      the coefficient `alpha = roughness²` (not roughness directly), so it is
///      driven with `alpha = r*r`; the grid keeps `r ≥ 0.04` so the shader's
///      `roughness = max(roughness, 0.04)` clamp is a no-op and both evaluate the
///      identical closed form.
///   2. `DistributionGgxMatchesSceneVerbatim` (no GL): asserts terrain's
///      `distributionGGX` source is byte-identical (whitespace-normalised) to
///      `scene.frag.glsl`'s — the always-on guard (runs even where the GL
///      context is skipped on headless CI) that stops the copy from drifting
///      from the shared BRDF.

#include "gl_test_fixture.h"
#include "shader_parity_helpers.h"

#include "formula/expression.h"
#include "formula/expression_eval.h"
#include "formula/formula.h"
#include "formula/physics_templates.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vestige::Test
{

namespace
{

/// Evaluate the `ggx_distribution` Formula-Library reference. Its input is
/// `NdotH` and its only coefficient is `alpha = roughness²` — so the reference
/// is driven with alpha = r*r to match the shader helper's internal `a =
/// roughness²`.
float referenceGgx(const FormulaDefinition& def, float NdotH, float roughness)
{
    ExpressionEvaluator eval;
    ExpressionEvaluator::VariableMap vars{{"NdotH", NdotH}};
    std::unordered_map<std::string, float> coeffs{{"alpha", roughness * roughness}};
    return eval.evaluate(*def.getExpression(QualityTier::FULL), vars, coeffs);
}

/// Strip all whitespace so an indentation/newline difference between the two
/// shader files doesn't mask a real formula divergence (or trip a match).
std::string stripWhitespace(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        if (!std::isspace(static_cast<unsigned char>(c)))
        {
            out.push_back(c);
        }
    }
    return out;
}

// NdotH ∈ [0,1] (half-vector cosine) × roughness ∈ [0.04,1] (kept ≥ the shader
// clamp so both sides evaluate the same closed form).
const std::vector<float> kNdotH = {0.0f, 0.1f, 0.25f, 0.4f, 0.55f,
                                   0.7f, 0.85f, 0.95f, 1.0f};
const std::vector<float> kRoughness = {0.04f, 0.1f, 0.2f, 0.35f, 0.5f,
                                       0.65f, 0.8f, 0.9f, 1.0f};

}  // namespace

class TerrainGgxParityTest : public GLTestFixture
{
};

TEST_F(TerrainGgxParityTest, DistributionMatchesReferenceOnGpu)
{
    const std::string terrainSrc = readShaderFile("terrain.frag.glsl");
    ASSERT_FALSE(terrainSrc.empty());
    const std::string ggxFn = extractGlslFunction(terrainSrc, "distributionGGX");
    ASSERT_FALSE(ggxFn.empty()) << "distributionGGX not found in terrain.frag.glsl";

    // Minimal single-pixel harness that calls the production GLSL verbatim.
    const std::string frag =
        "#version 450 core\n"
        "out vec4 outColor;\n"
        "uniform float u_NdotH;\n"
        "uniform float u_roughness;\n"
        "const float PI = 3.14159265359;\n"
        + ggxFn +
        "void main() { outColor = vec4(distributionGGX(u_NdotH, u_roughness)); }\n";

    ShaderProgram prog(frag);
    ASSERT_TRUE(prog.valid());

    const FormulaDefinition ggxDef = PhysicsTemplates::createGGXDistribution();

    for (float r : kRoughness)
    {
        for (float h : kNdotH)
        {
            const float gpu = prog.run({{"u_NdotH", h}, {"u_roughness", r}}).x;
            const float ref = referenceGgx(ggxDef, h, r);
            // GGX is pure arithmetic (mul/div, no transcendentals); the squared
            // denominator makes values large at low roughness, so compare
            // relatively. 1e-3 covers float32 accumulation across drivers.
            const float rel = std::abs(gpu - ref) / std::max(std::abs(ref), 1e-3f);
            EXPECT_LT(rel, 1e-3f)
                << "terrain distributionGGX drifted from ggx_distribution reference"
                << " at NdotH=" << h << " roughness=" << r
                << " (gpu=" << gpu << " ref=" << ref << ")";
        }
    }
}

// No GL context needed — this is the always-on drift guard.
TEST(TerrainGgxSourceParity, DistributionGgxMatchesSceneVerbatim)
{
    const std::string terrainSrc = readShaderFile("terrain.frag.glsl");
    const std::string sceneSrc = readShaderFile("scene.frag.glsl");
    ASSERT_FALSE(terrainSrc.empty());
    ASSERT_FALSE(sceneSrc.empty());

    const std::string terrainFn = extractGlslFunction(terrainSrc, "distributionGGX");
    const std::string sceneFn = extractGlslFunction(sceneSrc, "distributionGGX");
    ASSERT_FALSE(terrainFn.empty());
    ASSERT_FALSE(sceneFn.empty());

    EXPECT_EQ(stripWhitespace(terrainFn), stripWhitespace(sceneFn))
        << "terrain.frag.glsl's distributionGGX has diverged from scene.frag.glsl's "
           "canonical copy — the terrain ground would no longer match the engine BRDF.";
}

}  // namespace Vestige::Test
