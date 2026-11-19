// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_gltf_tangents.cpp
/// @brief 3D_E-0874 — a glTF mesh with tangents but no normals gets real
///        bitangents.
///
/// The bitangent is cross(normal, tangent) * handedness. The loader worked
/// it out while reading TANGENT, before the missing normals were generated,
/// so every bitangent came out zero and normal mapping broke.
///
/// Needs a GL context: the loader uploads the mesh, and the test reads
/// the uploaded vertices back from the VBO.

#include "gl_test_fixture.h"
#include "utils/gltf_loader.h"
#include "resource/resource_manager.h"
#include "renderer/mesh.h"

#include <gtest/gtest.h>
#include <glad/gl.h>
#include <glm/glm.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace fs = std::filesystem;

namespace Vestige::GltfTangents::Test
{

class GltfTangentTest : public ::Vestige::Test::GLTestFixture
{
protected:
    fs::path m_root;

    void SetUp() override
    {
        GLTestFixture::SetUp();
        m_root = fs::temp_directory_path()
               / ("vestige_gltf_tangent_test_" + Testing::vestigeTestStamp());
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

TEST_F(GltfTangentTest, TangentsWithoutNormalsGetBitangents_0874)
{
    // One triangle in the XY plane: three positions, then three tangents
    // along +X with handedness -1. No NORMAL attribute.
    const std::array<float, 9> positions = {
        0.0f, 0.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f, 0.0f,
    };
    const std::array<float, 12> tangents = {
        1.0f, 0.0f, 0.0f, -1.0f,
        1.0f, 0.0f, 0.0f, -1.0f,
        1.0f, 0.0f, 0.0f, -1.0f,
    };
    {
        std::ofstream bin(m_root / "tri.bin", std::ios::binary);
        bin.write(reinterpret_cast<const char*>(positions.data()),
                  static_cast<std::streamsize>(sizeof(positions)));
        bin.write(reinterpret_cast<const char*>(tangents.data()),
                  static_cast<std::streamsize>(sizeof(tangents)));
    }
    std::ofstream{m_root / "tri.gltf"} << R"({
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "TANGENT": 1}}]}],
        "buffers": [{"uri": "tri.bin", "byteLength": 84}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": 36},
            {"buffer": 0, "byteOffset": 36, "byteLength": 48}
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
             "min": [0, 0, 0], "max": [1, 1, 0]},
            {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC4"}
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

    for (const Vertex& v : vertices)
    {
        // The generated flat normal of a triangle in the XY plane is +-Z.
        ASSERT_NEAR(glm::length(v.normal), 1.0f, 1e-5f);
        EXPECT_FLOAT_EQ(v.tangent.x, 1.0f);
        const glm::vec3 want = glm::cross(v.normal, v.tangent) * -1.0f;
        EXPECT_NEAR(glm::length(v.bitangent), 1.0f, 1e-5f)
            << "bitangent was worked out before the normal existed";
        EXPECT_NEAR(v.bitangent.x, want.x, 1e-5f);
        EXPECT_NEAR(v.bitangent.y, want.y, 1e-5f);
        EXPECT_NEAR(v.bitangent.z, want.z, 1e-5f);
    }
}

}  // namespace Vestige::GltfTangents::Test
