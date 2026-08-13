// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_grass_shadow_parity.cpp
/// @brief Pins the one-generator contract behind GPU-grass shadow casting
///        (3D_E-0042): the shadow caster links the SAME `grass.vert.glsl` the
///        visible pass does, so a blade's shadow cannot drift from its
///        silhouette.
///
/// Why this needs a test at all: a mismatch here fails SILENTLY. If someone
/// renames a varying in `grass.vert.glsl`, the visible program still links
/// (its own fragment shader is edited alongside) while the shadow program
/// fails to link at runtime — `GrassRenderer::init` logs a warning, the field
/// keeps drawing, and the shadows simply stop existing. No build error, no
/// test failure, no crash. The GLSL text is therefore the spec, exactly as the
/// Sh2/Sh3 cloth parity tests treat it (tests/CMakeLists.txt VESTIGE_SHADER_DIR
/// — no GL context in this binary).

#include "shader_parity_helpers.h"

#include <gtest/gtest.h>

#include <regex>
#include <string>

namespace
{

using Vestige::Test::readShaderFile;

/// Does @p src declare `<qualifier> <type> <name>;` at top level?
bool declaresVarying(const std::string& src,
                     const std::string& qualifier,
                     const std::string& type,
                     const std::string& name)
{
    const std::regex re("(^|\\n)\\s*" + qualifier + "\\s+" + type + "\\s+" + name + "\\s*;");
    return std::regex_search(src, re);
}

// The varyings grass_shadow.frag.glsl consumes. Every one must be produced by
// grass.vert.glsl or the shadow program will not link.
constexpr const char* kCasterInputs[] = {"v_normal", "v_tint"};

}  // namespace

// Every `in` the caster's fragment stage declares is an `out` of the shared
// vertex shader. This is the assertion that goes red on a renamed varying.
TEST(GrassShadowParity, CasterInputsAreProducedByTheSharedVertexShader)
{
    const std::string vert = readShaderFile("grass.vert.glsl");
    const std::string frag = readShaderFile("grass_shadow.frag.glsl");
    ASSERT_FALSE(vert.empty());
    ASSERT_FALSE(frag.empty());

    for (const char* name : kCasterInputs)
    {
        EXPECT_TRUE(declaresVarying(frag, "in", "vec3", name))
            << "grass_shadow.frag.glsl no longer reads " << name
            << " — update kCasterInputs if that is deliberate";
        EXPECT_TRUE(declaresVarying(vert, "out", "vec3", name))
            << "grass.vert.glsl no longer writes " << name
            << ", so the grass shadow program will fail to link at runtime and "
               "the field will silently stop casting shadows (3D_E-0042)";
    }
}

// The caster writes the RSM flux attachment the directional shadow map's MRT
// expects (the shadow_depth.frag contract, Phase 13 G1) — a caster that writes
// depth but no flux leaves a hole in the world-space GI inject pass.
TEST(GrassShadowParity, CasterWritesTheRsmFluxAttachment)
{
    const std::string frag = readShaderFile("grass_shadow.frag.glsl");
    ASSERT_FALSE(frag.empty());

    EXPECT_NE(frag.find("layout(location = 0) out vec4 fluxOut;"), std::string::npos)
        << "grass_shadow.frag.glsl must declare the RSM flux output at location 0, "
           "like foliage_shadow.frag.glsl and tree_shadow.frag.glsl do";
    EXPECT_NE(frag.find("u_lightRadiance"), std::string::npos);
    EXPECT_NE(frag.find("u_lightDir"), std::string::npos);
}

// Grass blades are real opaque geometry, not alpha-cut cards. The absence of
// `discard` is what keeps early-Z alive through the shadow pass and makes this
// feature affordable at all (docs/research/grass_shadows_research.md §2.2) —
// so a `discard` appearing here is a performance regression worth failing on.
TEST(GrassShadowParity, CasterDoesNotDiscard)
{
    const std::string frag = readShaderFile("grass_shadow.frag.glsl");
    ASSERT_FALSE(frag.empty());

    // Match the statement, not the word — the file's comments discuss `discard`.
    const std::regex discardStmt("(^|\\n)\\s*[^/\\n]*\\bdiscard\\s*;");
    EXPECT_FALSE(std::regex_search(frag, discardStmt))
        << "a discard in the grass shadow caster disables early-Z for the whole "
           "shadow pass; grass blades are opaque geometry with nothing to cut out";
}
