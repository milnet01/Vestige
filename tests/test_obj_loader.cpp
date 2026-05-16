// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

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
    // Slice 18 Ts1: the size pin was redundant with `SharedVerticesAreDeduplicated
    // ViaIndices` (which tests size when dedup actually matters) — for a fully
    // unique input it tests only the test fixture, not the loader. Keep here
    // as `ASSERT_EQ` so the next vertex-data checks have valid indices.
    ASSERT_EQ(vertices.size(), 3u);
    ASSERT_EQ(indices.size(), 3u);

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

// Slice 18 Ts4: the three zero-output cases (empty file, comments
// only, vertices but no faces) all pass via the same root: parser
// returns false when zero faces produce zero output. Collapsed into a
// single table-driven test.
TEST(ObjLoaderTest, ZeroFaceInputsReturnFalseAndProduceNoOutput)
{
    struct Case { const char* data; const char* name; };
    const Case cases[] = {
        { "",                                                "empty"             },
        { "# This is a comment\n# Another comment\n",        "comments-only"     },
        { "v 0.0 0.0 0.0\nv 1.0 0.0 0.0\nv 0.0 1.0 0.0\n",   "vertices-no-faces" },
    };
    for (const Case& c : cases)
    {
        TempObjFile file(c.data);
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        EXPECT_FALSE(ObjLoader::load(file.path(), vertices, indices)) << c.name;
        EXPECT_TRUE(vertices.empty()) << c.name;
        EXPECT_TRUE(indices.empty()) << c.name;
    }
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

// Slice 18 Ts4: `VerticesOnlyNoFacesReturnsFalse` rolled into
// `ZeroFaceInputsReturnFalseAndProduceNoOutput` above.

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

// =============================================================================
// Wavefront OBJ spec, Appendix B: "If the value is negative it is an index
// relative to the last vertex in the vertex list. For example, -1 refers to
// the most recent vertex." Applies to v, vt, and vn index lists independently.
// =============================================================================

TEST(ObjLoaderTest, OBJSpec_AppendixB_NegativeIndicesResolveRelativeToCurrentListEnd)
{
    // Three positions; f -3 -2 -1 must resolve to positions 1, 2, 3
    // (i.e. the three most-recently-declared vertices, in order).
    const std::string objData =
        "v -1.0 0.0 0.0\n"
        "v  1.0 0.0 0.0\n"
        "v  0.0 1.0 0.0\n"
        "f -3 -2 -1\n";

    TempObjFile file(objData);
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    bool result = ObjLoader::load(file.path(), vertices, indices);

    ASSERT_TRUE(result);
    ASSERT_EQ(vertices.size(), 3u);
    ASSERT_EQ(indices.size(), 3u);

    // Indices are listed in face order; with no dedup conflict they should be 0,1,2.
    EXPECT_FLOAT_EQ(vertices[indices[0]].position.x, -1.0f);
    EXPECT_FLOAT_EQ(vertices[indices[0]].position.y,  0.0f);

    EXPECT_FLOAT_EQ(vertices[indices[1]].position.x,  1.0f);
    EXPECT_FLOAT_EQ(vertices[indices[1]].position.y,  0.0f);

    EXPECT_FLOAT_EQ(vertices[indices[2]].position.x,  0.0f);
    EXPECT_FLOAT_EQ(vertices[indices[2]].position.y,  1.0f);
}

TEST(ObjLoaderTest, OBJSpec_AppendixB_NegativeIndicesAreRelativeAtParseTime)
{
    // Negative indices must resolve against the *current* end of the list at
    // face-parse time, not the final size. Here we interleave v and f lines:
    // the second face's -1 must mean v #4, not v #6.
    const std::string objData =
        "v 0.0 0.0 0.0\n"   // v #1
        "v 1.0 0.0 0.0\n"   // v #2
        "v 0.0 1.0 0.0\n"   // v #3
        "f -3 -2 -1\n"      // triangle of v#1, v#2, v#3
        "v 2.0 0.0 0.0\n"   // v #4
        "v 2.0 1.0 0.0\n"   // v #5
        "v 1.5 0.5 0.0\n"   // v #6
        "f -3 -2 -1\n";     // triangle of v#4, v#5, v#6 (must NOT wrap to v#4..v#6 via final size)

    TempObjFile file(objData);
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    bool result = ObjLoader::load(file.path(), vertices, indices);

    ASSERT_TRUE(result);
    ASSERT_EQ(indices.size(), 6u);

    // First triangle — v#1, v#2, v#3
    EXPECT_FLOAT_EQ(vertices[indices[0]].position.x, 0.0f);
    EXPECT_FLOAT_EQ(vertices[indices[1]].position.x, 1.0f);
    EXPECT_FLOAT_EQ(vertices[indices[2]].position.y, 1.0f);

    // Second triangle — v#4, v#5, v#6
    EXPECT_FLOAT_EQ(vertices[indices[3]].position.x, 2.0f);
    EXPECT_FLOAT_EQ(vertices[indices[3]].position.y, 0.0f);
    EXPECT_FLOAT_EQ(vertices[indices[4]].position.x, 2.0f);
    EXPECT_FLOAT_EQ(vertices[indices[4]].position.y, 1.0f);
    EXPECT_FLOAT_EQ(vertices[indices[5]].position.x, 1.5f);
    EXPECT_FLOAT_EQ(vertices[indices[5]].position.y, 0.5f);
}

// =============================================================================
// Resource-exhaustion hardening: per-line read cap (CWE-400 "Uncontrolled
// Resource Consumption"). Malicious or corrupt OBJ files with pathological
// single-line lengths must be rejected, not read into unbounded memory.
// Cap is 1 MiB (= 1048576 bytes); matches the engine's standard parser budget.
// =============================================================================

TEST(ObjLoaderTest, CWE_400_OverLongSingleLineIsRejected)
{
    // Build a file with one line exceeding 1 MiB. Use a comment line so the
    // test is not checking any specific directive semantics — only the
    // per-line read limit. 1.5 MiB of 'x' characters after '#'.
    constexpr size_t kOneMiB = 1024u * 1024u;
    constexpr size_t kOverSize = kOneMiB + (kOneMiB / 2);

    std::string payload;
    payload.reserve(kOverSize + 64);
    payload.push_back('#');
    payload.append(kOverSize, 'x');
    payload.push_back('\n');
    // Append an otherwise-valid triangle; without the cap this OBJ would load.
    payload.append(
        "v 0.0 0.0 0.0\n"
        "v 1.0 0.0 0.0\n"
        "v 0.0 1.0 0.0\n"
        "f 1 2 3\n");

    TempObjFile file(payload);
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    bool result = ObjLoader::load(file.path(), vertices, indices);

    EXPECT_FALSE(result);
    EXPECT_TRUE(vertices.empty());
    EXPECT_TRUE(indices.empty());
}
