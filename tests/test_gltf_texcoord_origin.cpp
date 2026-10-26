// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_gltf_texcoord_origin.cpp
/// @brief 3D_E-0803 — glTF texture coordinates are converted to the
///        engine's bottom-left texture origin.
///
/// glTF puts UV (0, 0) at the top-left of an image. Texture::loadFromFile
/// flips every image on load (stb_image), so the engine's (0, 0) is the
/// image's bottom-left. Without converting v to 1 - v, every glTF model
/// samples its textures upside down. A model whose UV island sits in the
/// middle of the image looks right by luck; one using the top third of an
/// atlas samples the bottom third instead (the meadow's moss rocks hit an
/// empty black band and rendered as chrome foil).
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
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace fs = std::filesystem;

namespace Vestige::GltfTexcoordOrigin::Test
{

class GltfTexcoordOriginTest : public ::Vestige::Test::GLTestFixture
{
protected:
    fs::path m_root;

    void SetUp() override
    {
        GLTestFixture::SetUp();
        m_root = fs::temp_directory_path()
               / ("vestige_gltf_texcoord_test_" + Testing::vestigeTestStamp());
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

TEST_F(GltfTexcoordOriginTest, TexcoordVIsMeasuredFromTheBottom_0803)
{
    // One triangle: three positions, then three UVs, in one buffer.
    const std::array<float, 15> data = {
        0.0f, 0.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f, 0.0f,  // POSITION
        0.0f, 0.25f,        1.0f, 0.25f,        0.0f, 1.0f,        // TEXCOORD_0
    };
    {
        std::ofstream bin(m_root / "tri.bin", std::ios::binary);
        bin.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(sizeof(data)));
    }
    std::ofstream{m_root / "tri.gltf"} << R"({
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "TEXCOORD_0": 1}}]}],
        "buffers": [{"uri": "tri.bin", "byteLength": 60}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": 36},
            {"buffer": 0, "byteOffset": 36, "byteLength": 24}
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
             "min": [0, 0, 0], "max": [1, 1, 0]},
            {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC2"}
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

    // glTF v = 0.25 (a quarter down from the top) is engine v = 0.75.
    EXPECT_FLOAT_EQ(vertices[0].texCoord.x, 0.0f);
    EXPECT_FLOAT_EQ(vertices[0].texCoord.y, 0.75f);
    EXPECT_FLOAT_EQ(vertices[1].texCoord.x, 1.0f);
    EXPECT_FLOAT_EQ(vertices[1].texCoord.y, 0.75f);
    EXPECT_FLOAT_EQ(vertices[2].texCoord.y, 0.0f);
}

}  // namespace Vestige::GltfTexcoordOrigin::Test
