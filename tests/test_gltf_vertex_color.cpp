// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_gltf_vertex_color.cpp
/// @brief 3D_E-0874 — glTF vertex colours stored as 16-bit integers load.
///
/// glTF allows COLOR_0 as float, normalised unsigned byte or normalised
/// unsigned short. The loader read the first two and silently skipped the
/// third, so such a model stayed white.
///
/// Needs a GL context: the loader uploads the mesh, and the test reads
/// the uploaded vertices back from the VBO.

#include "gl_test_fixture.h"
#include "utils/gltf_loader.h"
#include "resource/resource_manager.h"
#include "renderer/mesh.h"

#include <gtest/gtest.h>
#include <glad/gl.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace fs = std::filesystem;

namespace Vestige::GltfVertexColor::Test
{

class GltfVertexColorTest : public ::Vestige::Test::GLTestFixture
{
protected:
    fs::path m_root;

    void SetUp() override
    {
        GLTestFixture::SetUp();
        m_root = fs::temp_directory_path()
               / ("vestige_gltf_vertex_color_test_" + Testing::vestigeTestStamp());
        std::error_code ec;
        fs::remove_all(m_root, ec);
        fs::create_directories(m_root);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_root, ec);
    }
};

TEST_F(GltfVertexColorTest, UnsignedShortColorsAreRead_0874)
{
    // One triangle: three float positions, then three RGBA colours of
    // 16-bit unsigned integers, in one buffer.
    const std::array<float, 9> positions = {
        0.0f, 0.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f, 0.0f,
    };
    const std::array<std::uint16_t, 12> colors = {
        65535, 0,     0,     65535,
        0,     65535, 0,     65535,
        0,     0,     13107, 65535,   // 13107 / 65535 = 0.2
    };
    {
        std::ofstream bin(m_root / "tri.bin", std::ios::binary);
        bin.write(reinterpret_cast<const char*>(positions.data()),
                  static_cast<std::streamsize>(sizeof(positions)));
        bin.write(reinterpret_cast<const char*>(colors.data()),
                  static_cast<std::streamsize>(sizeof(colors)));
    }
    std::ofstream{m_root / "tri.gltf"} << R"({
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "COLOR_0": 1}}]}],
        "buffers": [{"uri": "tri.bin", "byteLength": 60}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": 36},
            {"buffer": 0, "byteOffset": 36, "byteLength": 24}
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
             "min": [0, 0, 0], "max": [1, 1, 0]},
            {"bufferView": 1, "componentType": 5123, "normalized": true,
             "count": 3, "type": "VEC4"}
        ]
    })";

    ResourceManager rm;
    auto model = GltfLoader::load((m_root / "tri.gltf").string(), rm);
    ASSERT_NE(model, nullptr);
    ASSERT_EQ(model->m_primitives.size(), 1u);
    const auto& mesh = model->m_primitives[0].mesh;
    ASSERT_NE(mesh, nullptr);

    GLint vbo = 0;
    glGetVertexArrayIndexediv(mesh->getVao(), 0, GL_VERTEX_BINDING_BUFFER, &vbo);
    ASSERT_NE(vbo, 0);
    std::vector<Vertex> vertices(3);
    glGetNamedBufferSubData(static_cast<GLuint>(vbo), 0,
                            static_cast<GLsizeiptr>(sizeof(Vertex) * vertices.size()),
                            vertices.data());

    EXPECT_FLOAT_EQ(vertices[0].color.r, 1.0f);
    EXPECT_FLOAT_EQ(vertices[0].color.g, 0.0f);
    EXPECT_FLOAT_EQ(vertices[0].color.b, 0.0f);
    EXPECT_FLOAT_EQ(vertices[1].color.r, 0.0f);
    EXPECT_FLOAT_EQ(vertices[1].color.g, 1.0f);
    EXPECT_FLOAT_EQ(vertices[2].color.g, 0.0f);
    EXPECT_FLOAT_EQ(vertices[2].color.b, 0.2f);
}

}  // namespace Vestige::GltfVertexColor::Test
