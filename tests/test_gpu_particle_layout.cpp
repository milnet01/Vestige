// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_gpu_particle_layout.cpp
/// @brief The particle simulate shader reads the behaviour block at the
///        offsets GPUParticleSystem writes it (lane-05 lead, 3D_E-0874).
///
/// GPUParticleSystem memcpy's a tightly packed BehaviorBlockGPU (32-byte
/// entries, float arrays at a 4-byte stride; its static_asserts pin the
/// offsets used below). The shader declared the block std140, where every
/// float-array element takes 16 bytes, so all but the first few fields read
/// the wrong bytes. This asks the compiled shader where each field lives.

#include "gl_test_fixture.h"
#include "renderer/shader.h"

#include <glad/gl.h>
#include <gtest/gtest.h>

#include <string>

using namespace Vestige;

namespace
{

struct FieldLayout
{
    GLint offset = -1;
    GLint arrayStride = -1;
    GLint topLevelStride = -1;  ///< storage-buffer arrays of structs only
};

/// Finds @p name as a uniform-block member or a storage-buffer variable.
FieldLayout fieldLayout(GLuint program, const char* name)
{
    FieldLayout f;
    for (GLenum iface : {GLenum(GL_UNIFORM), GLenum(GL_BUFFER_VARIABLE)})
    {
        const GLuint index = glGetProgramResourceIndex(program, iface, name);
        if (index == GL_INVALID_INDEX)
        {
            continue;
        }
        const GLenum props[] = {GL_OFFSET, GL_ARRAY_STRIDE};
        GLint values[2] = {-1, -1};
        glGetProgramResourceiv(program, iface, index, 2, props, 2, nullptr, values);
        f.offset = values[0];
        f.arrayStride = values[1];
        if (iface == GL_BUFFER_VARIABLE)
        {
            const GLenum top = GL_TOP_LEVEL_ARRAY_STRIDE;
            glGetProgramResourceiv(program, iface, index, 1, &top, 1, nullptr, &f.topLevelStride);
        }
        return f;
    }
    return f;
}

class GpuParticleLayoutTest : public Test::GLTestFixture {};

}  // namespace

TEST_F(GpuParticleLayoutTest, BehaviourBlockMatchesTheCpuPacking)
{
    Shader simulate;
    ASSERT_TRUE(simulate.loadComputeShader(std::string(VESTIGE_SHADER_DIR)
                                           + "/particle_simulate.comp.glsl"));
    const GLuint prog = simulate.getId();

    // One 32-byte entry: type, flags, then six floats 4 bytes apart.
    const FieldLayout params = fieldLayout(prog, "behaviors[0].params[0]");
    EXPECT_EQ(params.offset, 8);
    EXPECT_EQ(params.arrayStride, 4) << "params[1] must sit 4 bytes after params[0]";

    // Entries 32 bytes apart.
    const FieldLayout type0 = fieldLayout(prog, "behaviors[0].type");
    const FieldLayout type1 = fieldLayout(prog, "behaviors[1].type");
    const GLint entryStride = type1.offset >= 0 ? type1.offset - type0.offset : type0.topLevelStride;
    EXPECT_EQ(entryStride, 32);

    // The curves after the 16 entries, the four counts and eight colour stops.
    const FieldLayout stopTimes = fieldLayout(prog, "colorStopTimes[0]");
    EXPECT_EQ(stopTimes.offset, 656);
    EXPECT_EQ(stopTimes.arrayStride, 4);
    EXPECT_EQ(fieldLayout(prog, "speedKeyTimes[0]").offset, 784);
}
