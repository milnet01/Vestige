/// @file test_obj_loader.cpp
/// @brief Unit tests for the ObjLoader (Wavefront OBJ parsing).
#include "utils/obj_loader.h"

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace Vestige;

// =============================================================================
// Helper — write an OBJ string to a temporary file and return its path.
// =============================================================================

/// @brief RAII wrapper that creates a temp OBJ file and removes it on destruction.
class TempObjFile
{
public:
    explicit TempObjFile(const std::string& content)
    {
        // Use mkstemp for safe temp file creation
        char nameTemplate[] = "/tmp/vestige_test_XXXXXX.obj";
        int fd = mkstemps(nameTemplate, 4);  // 4 = length of ".obj"
        EXPECT_NE(fd, -1) << "Failed to create temp OBJ file";

        if (fd != -1)
        {
            m_path = nameTemplate;
            FILE* f = fdopen(fd, "w");
            if (f)
            {
                fwrite(content.c_str(), 1, content.size(), f);
                fclose(f);
            }
        }
    }

    ~TempObjFile()
    {
        if (!m_path.empty())
        {
            std::remove(m_path.c_str());
        }
    }

    const std::string& path() const { return m_path; }

private:
    std::string m_path;
};

// =============================================================================
// ObjLoaderTest — Triangle parsing
// =============================================================================

TEST(ObjLoaderTest, LoadSimpleTriangle)
{
    const std::string objData =
        "# Simple triangle\n"
        "v 0.0 0.0 0.0\n"
        "v 1.0 0.0 0.0\n"
        "v 0.0 1.0 0.0\n"
        "vn 0.0 0.0 1.0\n"
        "vt 0.0 0.0\n"
        "vt 1.0 0.0\n"
        "vt 0.0 1.0\n"
        "f 1/1/1 2/2/1 3/3/1\n";

    TempObjFile file(objData);
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    bool result = ObjLoader::load(file.path(), vertices, indices);

    ASSERT_TRUE(result);
    EXPECT_EQ(vertices.size(), 3u);
    EXPECT_EQ(indices.size(), 3u);

    // Verify positions
    EXPECT_FLOAT_EQ(vertices[0].position.x, 0.0f);
    EXPECT_FLOAT_EQ(vertices[0].position.y, 0.0f);
    EXPECT_FLOAT_EQ(vertices[0].position.z, 0.0f);

    EXPECT_FLOAT_EQ(vertices[1].position.x, 1.0f);
    EXPECT_FLOAT_EQ(vertices[1].position.y, 0.0f);

    EXPECT_FLOAT_EQ(vertices[2].position.x, 0.0f);
    EXPECT_FLOAT_EQ(vertices[2].position.y, 1.0f);

    // Verify normal was applied
    for (size_t i = 0; i < 3; i++)
    {
        EXPECT_FLOAT_EQ(vertices[i].normal.z, 1.0f);
    }

    // Verify tex coords
    EXPECT_FLOAT_EQ(vertices[0].texCoord.x, 0.0f);
    EXPECT_FLOAT_EQ(vertices[1].texCoord.x, 1.0f);
    EXPECT_FLOAT_EQ(vertices[2].texCoord.y, 1.0f);
}

// =============================================================================
// ObjLoaderTest — Quad parsing (triangulated to 2 triangles)
// =============================================================================

TEST(ObjLoaderTest, LoadQuadTriangulatesCorrectly)
{
    const std::string objData =
        "v 0.0 0.0 0.0\n"
        "v 1.0 0.0 0.0\n"
        "v 1.0 1.0 0.0\n"
        "v 0.0 1.0 0.0\n"
        "vn 0.0 0.0 1.0\n"
        "f 1//1 2//1 3//1 4//1\n";

    TempObjFile file(objData);
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    bool result = ObjLoader::load(file.path(), vertices, indices);

    ASSERT_TRUE(result);
    // A quad should be split into 2 triangles = 6 indices
    EXPECT_EQ(indices.size(), 6u);
    // 4 unique vertices (each corner has a unique pos/norm combination)
    EXPECT_EQ(vertices.size(), 4u);
}

// =============================================================================
// ObjLoaderTest — Missing normals / texcoords
// =============================================================================

TEST(ObjLoaderTest, HandlesPositionOnlyFaces)
{
    const std::string objData =
        "v -1.0 0.0 0.0\n"
        "v  1.0 0.0 0.0\n"
        "v  0.0 1.0 0.0\n"
        "f 1 2 3\n";

    TempObjFile file(objData);
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    bool result = ObjLoader::load(file.path(), vertices, indices);

    ASSERT_TRUE(result);
    EXPECT_EQ(vertices.size(), 3u);
    EXPECT_EQ(indices.size(), 3u);

    // Positions should still be correct
    EXPECT_FLOAT_EQ(vertices[0].position.x, -1.0f);
    EXPECT_FLOAT_EQ(vertices[1].position.x, 1.0f);
    EXPECT_FLOAT_EQ(vertices[2].position.y, 1.0f);
}

TEST(ObjLoaderTest, HandlesMissingTexCoordWithNormal)
{
    // Format: v//vn (texcoord skipped)
    const std::string objData =
        "v 0.0 0.0 0.0\n"
        "v 1.0 0.0 0.0\n"
        "v 0.0 1.0 0.0\n"
        "vn 0.0 0.0 1.0\n"
        "f 1//1 2//1 3//1\n";

    TempObjFile file(objData);
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    bool result = ObjLoader::load(file.path(), vertices, indices);

    ASSERT_TRUE(result);
    EXPECT_EQ(vertices.size(), 3u);

    // Normal should be set
    for (size_t i = 0; i < 3; i++)
    {
        EXPECT_FLOAT_EQ(vertices[i].normal.z, 1.0f);
    }

    // TexCoord should default to (0,0) since none were specified
    for (size_t i = 0; i < 3; i++)
    {
        EXPECT_FLOAT_EQ(vertices[i].texCoord.x, 0.0f);
        EXPECT_FLOAT_EQ(vertices[i].texCoord.y, 0.0f);
    }
}

// =============================================================================
// ObjLoaderTest — Empty and malformed input
// =============================================================================

TEST(ObjLoaderTest, EmptyFileReturnsFalse)
{
    const std::string objData = "";

    TempObjFile file(objData);
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    bool result = ObjLoader::load(file.path(), vertices, indices);

    EXPECT_FALSE(result);
    EXPECT_TRUE(vertices.empty());
    EXPECT_TRUE(indices.empty());
}

TEST(ObjLoaderTest, CommentsOnlyReturnsFalse)
{
    const std::string objData =
        "# This is a comment\n"
        "# Another comment\n";

    TempObjFile file(objData);
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    bool result = ObjLoader::load(file.path(), vertices, indices);

    EXPECT_FALSE(result);
}

TEST(ObjLoaderTest, NonExistentFileReturnsFalse)
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    bool result = ObjLoader::load("/tmp/does_not_exist_12345.obj", vertices, indices);

    EXPECT_FALSE(result);
}

TEST(ObjLoaderTest, FaceWithTooFewVerticesIsSkipped)
{
    // A face line with only 2 vertices should be skipped.
    // Include a valid triangle so the file is not completely empty.
    const std::string objData =
        "v 0.0 0.0 0.0\n"
        "v 1.0 0.0 0.0\n"
        "v 0.0 1.0 0.0\n"
        "f 1 2\n"
        "f 1 2 3\n";

    TempObjFile file(objData);
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    bool result = ObjLoader::load(file.path(), vertices, indices);

    ASSERT_TRUE(result);
    // Only the valid triangle should produce geometry
    EXPECT_EQ(vertices.size(), 3u);
    EXPECT_EQ(indices.size(), 3u);
}

TEST(ObjLoaderTest, VerticesOnlyNoFacesReturnsFalse)
{
    const std::string objData =
        "v 0.0 0.0 0.0\n"
        "v 1.0 0.0 0.0\n"
        "v 0.0 1.0 0.0\n";

    TempObjFile file(objData);
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    bool result = ObjLoader::load(file.path(), vertices, indices);

    // No faces => no output vertices => returns false
    EXPECT_FALSE(result);
}

// =============================================================================
// ObjLoaderTest — Vertex deduplication
// =============================================================================

TEST(ObjLoaderTest, SharedVerticesAreDeduplicatedViaIndices)
{
    // Two triangles sharing an edge (vertices 1 and 2).
    // With identical tex/norm, shared vertices should be deduplicated.
    const std::string objData =
        "v 0.0 0.0 0.0\n"
        "v 1.0 0.0 0.0\n"
        "v 0.0 1.0 0.0\n"
        "v 1.0 1.0 0.0\n"
        "vn 0.0 0.0 1.0\n"
        "f 1//1 2//1 3//1\n"
        "f 2//1 4//1 3//1\n";

    TempObjFile file(objData);
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    bool result = ObjLoader::load(file.path(), vertices, indices);

    ASSERT_TRUE(result);
    // 6 indices for 2 triangles
    EXPECT_EQ(indices.size(), 6u);
    // Vertices 2 and 3 appear in both faces with the same norm,
    // so they should be deduplicated: 4 unique vertices
    EXPECT_EQ(vertices.size(), 4u);
}

// =============================================================================
// ObjLoaderTest — Pentagon fan triangulation
// =============================================================================

TEST(ObjLoaderTest, PentagonTriangulatesIntoThreeTriangles)
{
    const std::string objData =
        "v 0.0 0.0 0.0\n"
        "v 1.0 0.0 0.0\n"
        "v 1.5 1.0 0.0\n"
        "v 0.5 1.5 0.0\n"
        "v -0.5 1.0 0.0\n"
        "f 1 2 3 4 5\n";

    TempObjFile file(objData);
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    bool result = ObjLoader::load(file.path(), vertices, indices);

    ASSERT_TRUE(result);
    // A 5-sided polygon should fan-triangulate into 3 triangles = 9 indices
    EXPECT_EQ(indices.size(), 9u);
    EXPECT_EQ(vertices.size(), 5u);
}

// =============================================================================
// ObjLoaderTest — Miscellaneous OBJ directives are ignored
// =============================================================================

TEST(ObjLoaderTest, IgnoresUnknownDirectives)
{
    const std::string objData =
        "mtllib material.mtl\n"
        "o MyObject\n"
        "g group1\n"
        "s 1\n"
        "usemtl SomeMaterial\n"
        "v 0.0 0.0 0.0\n"
        "v 1.0 0.0 0.0\n"
        "v 0.0 1.0 0.0\n"
        "f 1 2 3\n";

    TempObjFile file(objData);
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    bool result = ObjLoader::load(file.path(), vertices, indices);

    ASSERT_TRUE(result);
    EXPECT_EQ(vertices.size(), 3u);
    EXPECT_EQ(indices.size(), 3u);
}
