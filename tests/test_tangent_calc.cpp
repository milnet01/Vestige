// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_tangent_calc.cpp
/// @brief Unit tests for tangent/bitangent calculation (normal mapping support).
#include "renderer/mesh.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/epsilon.hpp>

#include <cmath>

using namespace Vestige;

/// Helper: creates a simple right triangle in the XY plane with known UVs.
static void createSimpleTriangle(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
{
    vertices.clear();
    indices.clear();

    Vertex v0 = {};
    v0.position = glm::vec3(0.0f, 0.0f, 0.0f);
    v0.normal = glm::vec3(0.0f, 0.0f, 1.0f);
    v0.texCoord = glm::vec2(0.0f, 0.0f);

    Vertex v1 = {};
    v1.position = glm::vec3(1.0f, 0.0f, 0.0f);
    v1.normal = glm::vec3(0.0f, 0.0f, 1.0f);
    v1.texCoord = glm::vec2(1.0f, 0.0f);

    Vertex v2 = {};
    v2.position = glm::vec3(0.0f, 1.0f, 0.0f);
    v2.normal = glm::vec3(0.0f, 0.0f, 1.0f);
    v2.texCoord = glm::vec2(0.0f, 1.0f);

    vertices = {v0, v1, v2};
    indices = {0, 1, 2};
}

TEST(TangentCalcTest, TangentIsOrthogonalToNormal)
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    createSimpleTriangle(vertices, indices);

    calculateTangents(vertices, indices);

    for (const auto& v : vertices)
    {
        float dot = glm::dot(v.tangent, v.normal);
        EXPECT_NEAR(dot, 0.0f, 1e-4f) << "Tangent should be orthogonal to normal";
    }
}

TEST(TangentCalcTest, BitangentIsOrthogonalToNormal)
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    createSimpleTriangle(vertices, indices);

    calculateTangents(vertices, indices);

    for (const auto& v : vertices)
    {
        float dot = glm::dot(v.bitangent, v.normal);
        EXPECT_NEAR(dot, 0.0f, 1e-4f) << "Bitangent should be orthogonal to normal";
    }
}

TEST(TangentCalcTest, TangentIsOrthogonalToBitangent)
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    createSimpleTriangle(vertices, indices);

    calculateTangents(vertices, indices);

    for (const auto& v : vertices)
    {
        float dot = glm::dot(v.tangent, v.bitangent);
        EXPECT_NEAR(dot, 0.0f, 1e-4f) << "Tangent should be orthogonal to bitangent";
    }
}

TEST(TangentCalcTest, TangentsAreNormalized)
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    createSimpleTriangle(vertices, indices);

    calculateTangents(vertices, indices);

    for (const auto& v : vertices)
    {
        EXPECT_NEAR(glm::length(v.tangent), 1.0f, 1e-4f) << "Tangent should be unit length";
        EXPECT_NEAR(glm::length(v.bitangent), 1.0f, 1e-4f) << "Bitangent should be unit length";
    }
}

TEST(TangentCalcTest, CorrectDirection)
{
    // For a triangle in the XY plane with standard UV mapping,
    // tangent should align with +X (U direction) and bitangent with +Y (V direction)
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    createSimpleTriangle(vertices, indices);

    calculateTangents(vertices, indices);

    glm::vec3 expectedTangent(1.0f, 0.0f, 0.0f);
    glm::vec3 expectedBitangent(0.0f, 1.0f, 0.0f);

    for (const auto& v : vertices)
    {
        EXPECT_NEAR(glm::dot(v.tangent, expectedTangent), 1.0f, 1e-4f)
            << "Tangent should point along +X for standard UV mapping";
        EXPECT_NEAR(glm::dot(v.bitangent, expectedBitangent), 1.0f, 1e-4f)
            << "Bitangent should point along +Y for standard UV mapping";
    }
}

TEST(TangentCalcTest, DegenerateUVsFallback)
{
    // All vertices have the same UV — degenerate case
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    Vertex v0 = {};
    v0.position = glm::vec3(0.0f, 0.0f, 0.0f);
    v0.normal = glm::vec3(0.0f, 0.0f, 1.0f);
    v0.texCoord = glm::vec2(0.5f, 0.5f);

    Vertex v1 = v0;
    v1.position = glm::vec3(1.0f, 0.0f, 0.0f);

    Vertex v2 = v0;
    v2.position = glm::vec3(0.0f, 1.0f, 0.0f);

    vertices = {v0, v1, v2};
    indices = {0, 1, 2};

    calculateTangents(vertices, indices);

    // Should produce valid (non-zero, normalized) tangent/bitangent even with degenerate UVs
    for (const auto& v : vertices)
    {
        float tLen = glm::length(v.tangent);
        float bLen = glm::length(v.bitangent);
        EXPECT_GT(tLen, 0.5f) << "Tangent should be non-zero for degenerate UVs";
        EXPECT_GT(bLen, 0.5f) << "Bitangent should be non-zero for degenerate UVs";
    }
}

TEST(TangentCalcTest, EmptyMeshDoesNotCrash)
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    EXPECT_NO_THROW(calculateTangents(vertices, indices));
}
