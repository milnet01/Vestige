// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file procedural_mesh.cpp
/// @brief ProceduralMeshBuilder implementation — generates architectural geometry.
#include "utils/procedural_mesh.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace Vestige
{

// =============================================================================
// Helper: add a quad as two triangles
// =============================================================================

void ProceduralMeshBuilder::addQuad(
    std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
    const glm::vec3& p0, const glm::vec3& p1,
    const glm::vec3& p2, const glm::vec3& p3,
    const glm::vec3& normal,
    const glm::vec2& uv0, const glm::vec2& uv1,
    const glm::vec2& uv2, const glm::vec2& uv3)
{
    auto base = static_cast<uint32_t>(vertices.size());

    Vertex v{};
    v.normal = normal;
    v.color = glm::vec3(1.0f);

    v.position = p0; v.texCoord = uv0; vertices.push_back(v);
    v.position = p1; v.texCoord = uv1; vertices.push_back(v);
    v.position = p2; v.texCoord = uv2; vertices.push_back(v);
    v.position = p3; v.texCoord = uv3; vertices.push_back(v);

    // Two triangles: 0-1-2, 0-2-3
    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 0);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
}

void ProceduralMeshBuilder::addQuad(
    std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
    const glm::vec3& p0, const glm::vec3& p1,
    const glm::vec3& p2, const glm::vec3& p3,
    const glm::vec3& normal)
{
    // Auto-compute UVs: project onto the plane perpendicular to normal
    // Use position magnitudes as simple UV coordinates
    glm::vec3 edge1 = p1 - p0;
    glm::vec3 edge2 = p3 - p0;
    float u1 = glm::length(edge1);
    float v1 = glm::length(edge2);

    addQuad(vertices, indices, p0, p1, p2, p3, normal,
            glm::vec2(0.0f, 0.0f), glm::vec2(u1, 0.0f),
            glm::vec2(u1, v1), glm::vec2(0.0f, v1));
}

// =============================================================================
// generateWall — solid rectangular wall
// =============================================================================

ProceduralMeshData ProceduralMeshBuilder::generateWall(float width, float height, float thickness)
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    float hw = width * 0.5f;
    float ht = thickness * 0.5f;

    // Front face (+Z)
    addQuad(vertices, indices,
            glm::vec3(-hw, 0.0f, ht), glm::vec3(hw, 0.0f, ht),
            glm::vec3(hw, height, ht), glm::vec3(-hw, height, ht),
            glm::vec3(0.0f, 0.0f, 1.0f),
            glm::vec2(0.0f, 0.0f), glm::vec2(width, 0.0f),
            glm::vec2(width, height), glm::vec2(0.0f, height));

    // Back face (-Z)
    addQuad(vertices, indices,
            glm::vec3(hw, 0.0f, -ht), glm::vec3(-hw, 0.0f, -ht),
            glm::vec3(-hw, height, -ht), glm::vec3(hw, height, -ht),
            glm::vec3(0.0f, 0.0f, -1.0f),
            glm::vec2(0.0f, 0.0f), glm::vec2(width, 0.0f),
            glm::vec2(width, height), glm::vec2(0.0f, height));

    // Top face (+Y)
    addQuad(vertices, indices,
            glm::vec3(-hw, height, ht), glm::vec3(hw, height, ht),
            glm::vec3(hw, height, -ht), glm::vec3(-hw, height, -ht),
            glm::vec3(0.0f, 1.0f, 0.0f),
            glm::vec2(0.0f, 0.0f), glm::vec2(width, 0.0f),
            glm::vec2(width, thickness), glm::vec2(0.0f, thickness));

    // Bottom face (-Y)
    addQuad(vertices, indices,
            glm::vec3(-hw, 0.0f, -ht), glm::vec3(hw, 0.0f, -ht),
            glm::vec3(hw, 0.0f, ht), glm::vec3(-hw, 0.0f, ht),
            glm::vec3(0.0f, -1.0f, 0.0f),
            glm::vec2(0.0f, 0.0f), glm::vec2(width, 0.0f),
            glm::vec2(width, thickness), glm::vec2(0.0f, thickness));

    // Left face (-X)
    addQuad(vertices, indices,
            glm::vec3(-hw, 0.0f, -ht), glm::vec3(-hw, 0.0f, ht),
            glm::vec3(-hw, height, ht), glm::vec3(-hw, height, -ht),
            glm::vec3(-1.0f, 0.0f, 0.0f),
            glm::vec2(0.0f, 0.0f), glm::vec2(thickness, 0.0f),
            glm::vec2(thickness, height), glm::vec2(0.0f, height));

    // Right face (+X)
    addQuad(vertices, indices,
            glm::vec3(hw, 0.0f, ht), glm::vec3(hw, 0.0f, -ht),
            glm::vec3(hw, height, -ht), glm::vec3(hw, height, ht),
            glm::vec3(1.0f, 0.0f, 0.0f),
            glm::vec2(0.0f, 0.0f), glm::vec2(thickness, 0.0f),
            glm::vec2(thickness, height), glm::vec2(0.0f, height));

    calculateTangents(vertices, indices);
    return {std::move(vertices), std::move(indices)};
}

// =============================================================================
// createWallWithOpenings — wall with rectangular cutouts and reveal faces
// =============================================================================

ProceduralMeshData ProceduralMeshBuilder::generateWallWithOpenings(
    float width, float height, float thickness,
    const std::vector<WallOpening>& openings)
{
    if (openings.empty())
    {
        return generateWall(width, height, thickness);
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    float hw = width * 0.5f;
    float ht = thickness * 0.5f;

    // Sort openings left-to-right by xOffset
    std::vector<WallOpening> sorted = openings;
    std::sort(sorted.begin(), sorted.end(),
              [](const WallOpening& a, const WallOpening& b)
              { return a.xOffset < b.xOffset; });

    // Clamp openings to wall bounds
    for (auto& op : sorted)
    {
        op.xOffset = std::max(0.0f, std::min(op.xOffset, width));
        op.yOffset = std::max(0.0f, std::min(op.yOffset, height));
        if (op.xOffset + op.width > width) op.width = width - op.xOffset;
        if (op.yOffset + op.height > height) op.height = height - op.yOffset;
    }

    // For front and back faces, we subdivide the wall rectangle into
    // non-overlapping quads around the openings.
    // Strategy: for each opening, generate the wall segments around it.
    // We break the wall into horizontal strips:
    //   - Below all openings
    //   - At each opening level (left pillar, right pillar, header)
    //   - Above all openings

    // Helper lambda to add a front+back face quad pair
    auto addFaceQuadPair = [&](float x0, float y0, float x1, float y1)
    {
        if (x1 <= x0 || y1 <= y0) return;

        float lx0 = x0 - hw;
        float lx1 = x1 - hw;

        // Front face (+Z)
        addQuad(vertices, indices,
                glm::vec3(lx0, y0, ht), glm::vec3(lx1, y0, ht),
                glm::vec3(lx1, y1, ht), glm::vec3(lx0, y1, ht),
                glm::vec3(0.0f, 0.0f, 1.0f),
                glm::vec2(x0, y0), glm::vec2(x1, y0),
                glm::vec2(x1, y1), glm::vec2(x0, y1));

        // Back face (-Z)
        addQuad(vertices, indices,
                glm::vec3(lx1, y0, -ht), glm::vec3(lx0, y0, -ht),
                glm::vec3(lx0, y1, -ht), glm::vec3(lx1, y1, -ht),
                glm::vec3(0.0f, 0.0f, -1.0f),
                glm::vec2(x0, y0), glm::vec2(x1, y0),
                glm::vec2(x1, y1), glm::vec2(x0, y1));
    };

    // Collect unique Y breakpoints
    std::vector<float> yBreaks;
    yBreaks.push_back(0.0f);
    yBreaks.push_back(height);
    for (const auto& op : sorted)
    {
        yBreaks.push_back(op.yOffset);
        yBreaks.push_back(op.yOffset + op.height);
    }
    std::sort(yBreaks.begin(), yBreaks.end());
    yBreaks.erase(std::unique(yBreaks.begin(), yBreaks.end()), yBreaks.end());

    // For each horizontal strip between consecutive Y breakpoints
    for (size_t yi = 0; yi + 1 < yBreaks.size(); ++yi)
    {
        float y0 = yBreaks[yi];
        float y1 = yBreaks[yi + 1];

        // Collect X intervals that are blocked by openings in this strip
        std::vector<std::pair<float, float>> blocked;
        for (const auto& op : sorted)
        {
            if (op.yOffset < y1 && op.yOffset + op.height > y0)
            {
                blocked.emplace_back(op.xOffset, op.xOffset + op.width);
            }
        }
        std::sort(blocked.begin(), blocked.end());

        // Fill non-blocked X intervals
        float xCursor = 0.0f;
        for (const auto& [bx0, bx1] : blocked)
        {
            if (bx0 > xCursor)
            {
                addFaceQuadPair(xCursor, y0, bx0, y1);
            }
            xCursor = std::max(xCursor, bx1);
        }
        if (xCursor < width)
        {
            addFaceQuadPair(xCursor, y0, width, y1);
        }
    }

    // Top face (+Y)
    addQuad(vertices, indices,
            glm::vec3(-hw, height, ht), glm::vec3(hw, height, ht),
            glm::vec3(hw, height, -ht), glm::vec3(-hw, height, -ht),
            glm::vec3(0.0f, 1.0f, 0.0f),
            glm::vec2(0.0f, 0.0f), glm::vec2(width, 0.0f),
            glm::vec2(width, thickness), glm::vec2(0.0f, thickness));

    // Bottom face (-Y)
    addQuad(vertices, indices,
            glm::vec3(-hw, 0.0f, -ht), glm::vec3(hw, 0.0f, -ht),
            glm::vec3(hw, 0.0f, ht), glm::vec3(-hw, 0.0f, ht),
            glm::vec3(0.0f, -1.0f, 0.0f),
            glm::vec2(0.0f, 0.0f), glm::vec2(width, 0.0f),
            glm::vec2(width, thickness), glm::vec2(0.0f, thickness));

    // Left face (-X)
    addQuad(vertices, indices,
            glm::vec3(-hw, 0.0f, -ht), glm::vec3(-hw, 0.0f, ht),
            glm::vec3(-hw, height, ht), glm::vec3(-hw, height, -ht),
            glm::vec3(-1.0f, 0.0f, 0.0f),
            glm::vec2(0.0f, 0.0f), glm::vec2(thickness, 0.0f),
            glm::vec2(thickness, height), glm::vec2(0.0f, height));

    // Right face (+X)
    addQuad(vertices, indices,
            glm::vec3(hw, 0.0f, ht), glm::vec3(hw, 0.0f, -ht),
            glm::vec3(hw, height, -ht), glm::vec3(hw, height, ht),
            glm::vec3(1.0f, 0.0f, 0.0f),
            glm::vec2(0.0f, 0.0f), glm::vec2(thickness, 0.0f),
            glm::vec2(thickness, height), glm::vec2(0.0f, height));

    // Reveal faces (inner edges of each opening)
    for (const auto& op : sorted)
    {
        float lx0 = op.xOffset - hw;
        float lx1 = op.xOffset + op.width - hw;
        float by = op.yOffset;
        float ty = op.yOffset + op.height;

        // Left reveal (facing +X inside the opening)
        addQuad(vertices, indices,
                glm::vec3(lx0, by, -ht), glm::vec3(lx0, by, ht),
                glm::vec3(lx0, ty, ht), glm::vec3(lx0, ty, -ht),
                glm::vec3(-1.0f, 0.0f, 0.0f),
                glm::vec2(0.0f, 0.0f), glm::vec2(thickness, 0.0f),
                glm::vec2(thickness, op.height), glm::vec2(0.0f, op.height));

        // Right reveal (facing -X inside the opening)
        addQuad(vertices, indices,
                glm::vec3(lx1, by, ht), glm::vec3(lx1, by, -ht),
                glm::vec3(lx1, ty, -ht), glm::vec3(lx1, ty, ht),
                glm::vec3(1.0f, 0.0f, 0.0f),
                glm::vec2(0.0f, 0.0f), glm::vec2(thickness, 0.0f),
                glm::vec2(thickness, op.height), glm::vec2(0.0f, op.height));

        // Top reveal (header, facing -Y inside the opening)
        addQuad(vertices, indices,
                glm::vec3(lx0, ty, ht), glm::vec3(lx1, ty, ht),
                glm::vec3(lx1, ty, -ht), glm::vec3(lx0, ty, -ht),
                glm::vec3(0.0f, -1.0f, 0.0f),
                glm::vec2(0.0f, 0.0f), glm::vec2(op.width, 0.0f),
                glm::vec2(op.width, thickness), glm::vec2(0.0f, thickness));

        // Bottom reveal (sill, facing +Y inside the opening) — only if not a door (yOffset > 0)
        if (op.yOffset > 0.001f)
        {
            addQuad(vertices, indices,
                    glm::vec3(lx0, by, -ht), glm::vec3(lx1, by, -ht),
                    glm::vec3(lx1, by, ht), glm::vec3(lx0, by, ht),
                    glm::vec3(0.0f, 1.0f, 0.0f),
                    glm::vec2(0.0f, 0.0f), glm::vec2(op.width, 0.0f),
                    glm::vec2(op.width, thickness), glm::vec2(0.0f, thickness));
        }
    }

    calculateTangents(vertices, indices);
    return {std::move(vertices), std::move(indices)};
}

// =============================================================================
// createFloor — flat slab
// =============================================================================

ProceduralMeshData ProceduralMeshBuilder::generateFloor(float width, float depth, float thickness)
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    float hw = width * 0.5f;
    float hd = depth * 0.5f;

    // Top face (Y=0)
    addQuad(vertices, indices,
            glm::vec3(-hw, 0.0f, hd), glm::vec3(hw, 0.0f, hd),
            glm::vec3(hw, 0.0f, -hd), glm::vec3(-hw, 0.0f, -hd),
            glm::vec3(0.0f, 1.0f, 0.0f),
            glm::vec2(0.0f, 0.0f), glm::vec2(width, 0.0f),
            glm::vec2(width, depth), glm::vec2(0.0f, depth));

    // Bottom face (Y=-thickness)
    addQuad(vertices, indices,
            glm::vec3(-hw, -thickness, -hd), glm::vec3(hw, -thickness, -hd),
            glm::vec3(hw, -thickness, hd), glm::vec3(-hw, -thickness, hd),
            glm::vec3(0.0f, -1.0f, 0.0f),
            glm::vec2(0.0f, 0.0f), glm::vec2(width, 0.0f),
            glm::vec2(width, depth), glm::vec2(0.0f, depth));

    // Front (+Z)
    addQuad(vertices, indices,
            glm::vec3(-hw, -thickness, hd), glm::vec3(hw, -thickness, hd),
            glm::vec3(hw, 0.0f, hd), glm::vec3(-hw, 0.0f, hd),
            glm::vec3(0.0f, 0.0f, 1.0f));

    // Back (-Z)
    addQuad(vertices, indices,
            glm::vec3(hw, -thickness, -hd), glm::vec3(-hw, -thickness, -hd),
            glm::vec3(-hw, 0.0f, -hd), glm::vec3(hw, 0.0f, -hd),
            glm::vec3(0.0f, 0.0f, -1.0f));

    // Left (-X)
    addQuad(vertices, indices,
            glm::vec3(-hw, -thickness, -hd), glm::vec3(-hw, -thickness, hd),
            glm::vec3(-hw, 0.0f, hd), glm::vec3(-hw, 0.0f, -hd),
            glm::vec3(-1.0f, 0.0f, 0.0f));

    // Right (+X)
    addQuad(vertices, indices,
            glm::vec3(hw, -thickness, hd), glm::vec3(hw, -thickness, -hd),
            glm::vec3(hw, 0.0f, -hd), glm::vec3(hw, 0.0f, hd),
            glm::vec3(1.0f, 0.0f, 0.0f));

    calculateTangents(vertices, indices);
    return {std::move(vertices), std::move(indices)};
}

// =============================================================================
// createRoof
// =============================================================================

ProceduralMeshData ProceduralMeshBuilder::generateRoof(RoofType type, float width, float depth,
                                                        float peakHeight, float overhang)
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    float hw = (width + overhang * 2.0f) * 0.5f;
    float hd = (depth + overhang * 2.0f) * 0.5f;

    if (type == RoofType::FLAT)
    {
        float slabThickness = 0.15f;
        // Just a flat slab with overhang
        // Top
        addQuad(vertices, indices,
                glm::vec3(-hw, peakHeight, hd), glm::vec3(hw, peakHeight, hd),
                glm::vec3(hw, peakHeight, -hd), glm::vec3(-hw, peakHeight, -hd),
                glm::vec3(0.0f, 1.0f, 0.0f),
                glm::vec2(0.0f, 0.0f), glm::vec2(width + overhang * 2.0f, 0.0f),
                glm::vec2(width + overhang * 2.0f, depth + overhang * 2.0f),
                glm::vec2(0.0f, depth + overhang * 2.0f));

        float bottomY = peakHeight - slabThickness;
        // Bottom
        addQuad(vertices, indices,
                glm::vec3(-hw, bottomY, -hd), glm::vec3(hw, bottomY, -hd),
                glm::vec3(hw, bottomY, hd), glm::vec3(-hw, bottomY, hd),
                glm::vec3(0.0f, -1.0f, 0.0f));

        // Side faces
        addQuad(vertices, indices,
                glm::vec3(-hw, bottomY, hd), glm::vec3(hw, bottomY, hd),
                glm::vec3(hw, peakHeight, hd), glm::vec3(-hw, peakHeight, hd),
                glm::vec3(0.0f, 0.0f, 1.0f));
        addQuad(vertices, indices,
                glm::vec3(hw, bottomY, -hd), glm::vec3(-hw, bottomY, -hd),
                glm::vec3(-hw, peakHeight, -hd), glm::vec3(hw, peakHeight, -hd),
                glm::vec3(0.0f, 0.0f, -1.0f));
        addQuad(vertices, indices,
                glm::vec3(-hw, bottomY, -hd), glm::vec3(-hw, bottomY, hd),
                glm::vec3(-hw, peakHeight, hd), glm::vec3(-hw, peakHeight, -hd),
                glm::vec3(-1.0f, 0.0f, 0.0f));
        addQuad(vertices, indices,
                glm::vec3(hw, bottomY, hd), glm::vec3(hw, bottomY, -hd),
                glm::vec3(hw, peakHeight, -hd), glm::vec3(hw, peakHeight, hd),
                glm::vec3(1.0f, 0.0f, 0.0f));
    }
    else if (type == RoofType::GABLE)
    {
        // Two sloped faces meeting at a ridge along Z axis
        // Ridge is at Y = peakHeight, centered at X = 0
        glm::vec3 ridgeFront(0.0f, peakHeight, hd);
        glm::vec3 ridgeBack(0.0f, peakHeight, -hd);

        // Left slope normal
        glm::vec3 leftEdge = glm::vec3(-hw, 0.0f, 0.0f) - glm::vec3(0.0f, peakHeight, 0.0f);
        glm::vec3 leftNormal = glm::normalize(glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), leftEdge));

        // Right slope normal
        glm::vec3 rightEdge = glm::vec3(hw, 0.0f, 0.0f) - glm::vec3(0.0f, peakHeight, 0.0f);
        glm::vec3 rightNormal = glm::normalize(glm::cross(rightEdge, glm::vec3(0.0f, 0.0f, 1.0f)));

        float slopeLen = std::sqrt(hw * hw + peakHeight * peakHeight);
        float roofDepth = hd * 2.0f;

        // Left slope
        addQuad(vertices, indices,
                glm::vec3(-hw, 0.0f, hd), ridgeFront,
                ridgeBack, glm::vec3(-hw, 0.0f, -hd),
                leftNormal,
                glm::vec2(0.0f, 0.0f), glm::vec2(slopeLen, 0.0f),
                glm::vec2(slopeLen, roofDepth), glm::vec2(0.0f, roofDepth));

        // Right slope
        addQuad(vertices, indices,
                ridgeFront, glm::vec3(hw, 0.0f, hd),
                glm::vec3(hw, 0.0f, -hd), ridgeBack,
                rightNormal,
                glm::vec2(0.0f, 0.0f), glm::vec2(slopeLen, 0.0f),
                glm::vec2(slopeLen, roofDepth), glm::vec2(0.0f, roofDepth));

        // Front gable triangle
        {
            auto base = static_cast<uint32_t>(vertices.size());
            Vertex v{};
            v.normal = glm::vec3(0.0f, 0.0f, 1.0f);
            v.color = glm::vec3(1.0f);

            v.position = glm::vec3(-hw, 0.0f, hd);
            v.texCoord = glm::vec2(0.0f, 0.0f);
            vertices.push_back(v);

            v.position = glm::vec3(hw, 0.0f, hd);
            v.texCoord = glm::vec2(hw * 2.0f, 0.0f);
            vertices.push_back(v);

            v.position = ridgeFront;
            v.texCoord = glm::vec2(hw, peakHeight);
            vertices.push_back(v);

            indices.push_back(base);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
        }

        // Back gable triangle
        {
            auto base = static_cast<uint32_t>(vertices.size());
            Vertex v{};
            v.normal = glm::vec3(0.0f, 0.0f, -1.0f);
            v.color = glm::vec3(1.0f);

            v.position = glm::vec3(hw, 0.0f, -hd);
            v.texCoord = glm::vec2(0.0f, 0.0f);
            vertices.push_back(v);

            v.position = glm::vec3(-hw, 0.0f, -hd);
            v.texCoord = glm::vec2(hw * 2.0f, 0.0f);
            vertices.push_back(v);

            v.position = ridgeBack;
            v.texCoord = glm::vec2(hw, peakHeight);
            vertices.push_back(v);

            indices.push_back(base);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
        }
    }
    else if (type == RoofType::SHED)
    {
        // Single slope: high edge at -X, low edge at +X
        // High edge at Y = peakHeight, low edge at Y = 0
        glm::vec3 slopeDir = glm::normalize(
            glm::vec3(hw, 0.0f, 0.0f) - glm::vec3(-hw, peakHeight, 0.0f));
        glm::vec3 slopeNormal = glm::normalize(
            glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), slopeDir));

        float slopeLen = std::sqrt(4.0f * hw * hw + peakHeight * peakHeight);
        float roofDepth = hd * 2.0f;

        // Top slope face
        addQuad(vertices, indices,
                glm::vec3(-hw, peakHeight, hd), glm::vec3(hw, 0.0f, hd),
                glm::vec3(hw, 0.0f, -hd), glm::vec3(-hw, peakHeight, -hd),
                slopeNormal,
                glm::vec2(0.0f, 0.0f), glm::vec2(slopeLen, 0.0f),
                glm::vec2(slopeLen, roofDepth), glm::vec2(0.0f, roofDepth));

        // Bottom face
        addQuad(vertices, indices,
                glm::vec3(-hw, 0.0f, -hd), glm::vec3(hw, 0.0f, -hd),
                glm::vec3(hw, 0.0f, hd), glm::vec3(-hw, 0.0f, hd),
                glm::vec3(0.0f, -1.0f, 0.0f));

        // Left side triangle (high end)
        {
            auto base = static_cast<uint32_t>(vertices.size());
            Vertex v{};
            v.normal = glm::vec3(-1.0f, 0.0f, 0.0f);
            v.color = glm::vec3(1.0f);

            v.position = glm::vec3(-hw, 0.0f, hd);
            v.texCoord = glm::vec2(0.0f, 0.0f);
            vertices.push_back(v);

            v.position = glm::vec3(-hw, 0.0f, -hd);
            v.texCoord = glm::vec2(roofDepth, 0.0f);
            vertices.push_back(v);

            v.position = glm::vec3(-hw, peakHeight, -hd);
            v.texCoord = glm::vec2(roofDepth, peakHeight);
            vertices.push_back(v);

            v.position = glm::vec3(-hw, peakHeight, hd);
            v.texCoord = glm::vec2(0.0f, peakHeight);
            vertices.push_back(v);

            indices.push_back(base);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
            indices.push_back(base);
            indices.push_back(base + 2);
            indices.push_back(base + 3);
        }

        // Front face (+Z) — trapezoid
        {
            auto base = static_cast<uint32_t>(vertices.size());
            Vertex v{};
            v.normal = glm::vec3(0.0f, 0.0f, 1.0f);
            v.color = glm::vec3(1.0f);

            v.position = glm::vec3(-hw, 0.0f, hd);
            v.texCoord = glm::vec2(0.0f, 0.0f);
            vertices.push_back(v);

            v.position = glm::vec3(-hw, peakHeight, hd);
            v.texCoord = glm::vec2(0.0f, peakHeight);
            vertices.push_back(v);

            v.position = glm::vec3(hw, 0.0f, hd);
            v.texCoord = glm::vec2(hw * 2.0f, 0.0f);
            vertices.push_back(v);

            indices.push_back(base);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
        }

        // Back face (-Z) — trapezoid
        {
            auto base = static_cast<uint32_t>(vertices.size());
            Vertex v{};
            v.normal = glm::vec3(0.0f, 0.0f, -1.0f);
            v.color = glm::vec3(1.0f);

            v.position = glm::vec3(hw, 0.0f, -hd);
            v.texCoord = glm::vec2(0.0f, 0.0f);
            vertices.push_back(v);

            v.position = glm::vec3(-hw, peakHeight, -hd);
            v.texCoord = glm::vec2(hw * 2.0f, peakHeight);
            vertices.push_back(v);

            v.position = glm::vec3(-hw, 0.0f, -hd);
            v.texCoord = glm::vec2(hw * 2.0f, 0.0f);
            vertices.push_back(v);

            indices.push_back(base);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
        }
    }

    calculateTangents(vertices, indices);
    return {std::move(vertices), std::move(indices)};
}

// =============================================================================
// createStraightStairs
// =============================================================================

ProceduralMeshData ProceduralMeshBuilder::generateStraightStairs(
    float totalHeight, float riseHeight, float treadDepth, float width)
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // Clamp riseHeight to sensible bounds
    riseHeight = std::max(0.05f, std::min(riseHeight, totalHeight));
    int stepCount = std::max(1, static_cast<int>(std::round(totalHeight / riseHeight)));
    float actualRise = totalHeight / static_cast<float>(stepCount);
    float hw = width * 0.5f;

    for (int i = 0; i < stepCount; ++i)
    {
        float fi = static_cast<float>(i);
        float stepY = fi * actualRise;
        float stepZ = fi * treadDepth;
        float topY = stepY + actualRise;
        float frontZ = stepZ;
        float backZ = stepZ + treadDepth;

        // Tread (top of step) — horizontal face
        addQuad(vertices, indices,
                glm::vec3(-hw, topY, frontZ), glm::vec3(hw, topY, frontZ),
                glm::vec3(hw, topY, backZ), glm::vec3(-hw, topY, backZ),
                glm::vec3(0.0f, 1.0f, 0.0f),
                glm::vec2(0.0f, 0.0f), glm::vec2(width, 0.0f),
                glm::vec2(width, treadDepth), glm::vec2(0.0f, treadDepth));

        // Riser (vertical front face of step)
        addQuad(vertices, indices,
                glm::vec3(-hw, stepY, frontZ), glm::vec3(hw, stepY, frontZ),
                glm::vec3(hw, topY, frontZ), glm::vec3(-hw, topY, frontZ),
                glm::vec3(0.0f, 0.0f, -1.0f),
                glm::vec2(0.0f, 0.0f), glm::vec2(width, 0.0f),
                glm::vec2(width, actualRise), glm::vec2(0.0f, actualRise));

        // Bottom of tread (underside)
        addQuad(vertices, indices,
                glm::vec3(-hw, stepY, backZ), glm::vec3(hw, stepY, backZ),
                glm::vec3(hw, stepY, frontZ), glm::vec3(-hw, stepY, frontZ),
                glm::vec3(0.0f, -1.0f, 0.0f));

        // Left side of step
        {
            auto base = static_cast<uint32_t>(vertices.size());
            Vertex v{};
            v.normal = glm::vec3(-1.0f, 0.0f, 0.0f);
            v.color = glm::vec3(1.0f);

            v.position = glm::vec3(-hw, stepY, frontZ);
            v.texCoord = glm::vec2(0.0f, 0.0f);
            vertices.push_back(v);

            v.position = glm::vec3(-hw, stepY, backZ);
            v.texCoord = glm::vec2(treadDepth, 0.0f);
            vertices.push_back(v);

            v.position = glm::vec3(-hw, topY, backZ);
            v.texCoord = glm::vec2(treadDepth, actualRise);
            vertices.push_back(v);

            v.position = glm::vec3(-hw, topY, frontZ);
            v.texCoord = glm::vec2(0.0f, actualRise);
            vertices.push_back(v);

            indices.push_back(base);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
            indices.push_back(base);
            indices.push_back(base + 2);
            indices.push_back(base + 3);
        }

        // Right side of step
        {
            auto base = static_cast<uint32_t>(vertices.size());
            Vertex v{};
            v.normal = glm::vec3(1.0f, 0.0f, 0.0f);
            v.color = glm::vec3(1.0f);

            v.position = glm::vec3(hw, stepY, backZ);
            v.texCoord = glm::vec2(0.0f, 0.0f);
            vertices.push_back(v);

            v.position = glm::vec3(hw, stepY, frontZ);
            v.texCoord = glm::vec2(treadDepth, 0.0f);
            vertices.push_back(v);

            v.position = glm::vec3(hw, topY, frontZ);
            v.texCoord = glm::vec2(treadDepth, actualRise);
            vertices.push_back(v);

            v.position = glm::vec3(hw, topY, backZ);
            v.texCoord = glm::vec2(0.0f, actualRise);
            vertices.push_back(v);

            indices.push_back(base);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
            indices.push_back(base);
            indices.push_back(base + 2);
            indices.push_back(base + 3);
        }
    }

    // Back riser of the last step (rear wall)
    float lastZ = static_cast<float>(stepCount) * treadDepth;
    addQuad(vertices, indices,
            glm::vec3(hw, 0.0f, lastZ), glm::vec3(-hw, 0.0f, lastZ),
            glm::vec3(-hw, totalHeight, lastZ), glm::vec3(hw, totalHeight, lastZ),
            glm::vec3(0.0f, 0.0f, 1.0f),
            glm::vec2(0.0f, 0.0f), glm::vec2(width, 0.0f),
            glm::vec2(width, totalHeight), glm::vec2(0.0f, totalHeight));

    calculateTangents(vertices, indices);
    return {std::move(vertices), std::move(indices)};
}

// =============================================================================
// createSpiralStairs
// =============================================================================

ProceduralMeshData ProceduralMeshBuilder::generateSpiralStairs(
    float totalHeight, float riseHeight, float innerRadius, float outerRadius,
    float totalAngle)
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    riseHeight = std::max(0.05f, std::min(riseHeight, totalHeight));
    int stepCount = std::max(1, static_cast<int>(std::round(totalHeight / riseHeight)));
    float actualRise = totalHeight / static_cast<float>(stepCount);
    float anglePerStep = glm::radians(totalAngle) / static_cast<float>(stepCount);

    for (int i = 0; i < stepCount; ++i)
    {
        float fi = static_cast<float>(i);
        float angle0 = fi * anglePerStep;
        float angle1 = (fi + 1.0f) * anglePerStep;
        float stepY = fi * actualRise;
        float topY = stepY + actualRise;

        float cos0 = std::cos(angle0), sin0 = std::sin(angle0);
        float cos1 = std::cos(angle1), sin1 = std::sin(angle1);

        // Four corners of the tread wedge at topY
        glm::vec3 innerStart(innerRadius * cos0, topY, innerRadius * sin0);
        glm::vec3 outerStart(outerRadius * cos0, topY, outerRadius * sin0);
        glm::vec3 innerEnd(innerRadius * cos1, topY, innerRadius * sin1);
        glm::vec3 outerEnd(outerRadius * cos1, topY, outerRadius * sin1);

        // Tread (top face)
        addQuad(vertices, indices,
                innerStart, outerStart, outerEnd, innerEnd,
                glm::vec3(0.0f, 1.0f, 0.0f));

        // Riser (front face at angle0)
        glm::vec3 innerStartBottom(innerRadius * cos0, stepY, innerRadius * sin0);
        glm::vec3 outerStartBottom(outerRadius * cos0, stepY, outerRadius * sin0);

        glm::vec3 riserNormal = glm::normalize(
            glm::cross(outerStart - innerStart, innerStartBottom - innerStart));
        addQuad(vertices, indices,
                innerStartBottom, outerStartBottom, outerStart, innerStart,
                riserNormal);

        // Bottom of tread
        glm::vec3 innerEndBottom(innerRadius * cos1, stepY, innerRadius * sin1);
        glm::vec3 outerEndBottom(outerRadius * cos1, stepY, outerRadius * sin1);
        addQuad(vertices, indices,
                innerEnd, outerEnd, outerEndBottom, innerEndBottom,
                glm::vec3(0.0f, -1.0f, 0.0f));

        // Inner edge (facing center)
        glm::vec3 innerNormal = glm::normalize(
            glm::vec3(-cos0 - cos1, 0.0f, -sin0 - sin1));
        addQuad(vertices, indices,
                innerStartBottom, innerStart, innerEnd, innerEndBottom,
                innerNormal);

        // Outer edge (facing outward)
        glm::vec3 outerNormal = glm::normalize(
            glm::vec3(cos0 + cos1, 0.0f, sin0 + sin1));
        addQuad(vertices, indices,
                outerStart, outerStartBottom, outerEndBottom, outerEnd,
                outerNormal);
    }

    calculateTangents(vertices, indices);
    return {std::move(vertices), std::move(indices)};
}

// =============================================================================
// Convenience wrappers: generate + upload to Mesh (requires GL context)
// =============================================================================

Mesh ProceduralMeshBuilder::createWall(float width, float height, float thickness)
{
    auto data = generateWall(width, height, thickness);
    Mesh mesh;
    mesh.upload(data.vertices, data.indices);
    return mesh;
}

Mesh ProceduralMeshBuilder::createWallWithOpenings(float width, float height, float thickness,
                                                    const std::vector<WallOpening>& openings)
{
    auto data = generateWallWithOpenings(width, height, thickness, openings);
    Mesh mesh;
    mesh.upload(data.vertices, data.indices);
    return mesh;
}

Mesh ProceduralMeshBuilder::createFloor(float width, float depth, float thickness)
{
    auto data = generateFloor(width, depth, thickness);
    Mesh mesh;
    mesh.upload(data.vertices, data.indices);
    return mesh;
}

Mesh ProceduralMeshBuilder::createRoof(RoofType type, float width, float depth,
                                        float peakHeight, float overhang)
{
    auto data = generateRoof(type, width, depth, peakHeight, overhang);
    Mesh mesh;
    mesh.upload(data.vertices, data.indices);
    return mesh;
}

Mesh ProceduralMeshBuilder::createStraightStairs(float totalHeight, float riseHeight,
                                                  float treadDepth, float width)
{
    auto data = generateStraightStairs(totalHeight, riseHeight, treadDepth, width);
    Mesh mesh;
    mesh.upload(data.vertices, data.indices);
    return mesh;
}

Mesh ProceduralMeshBuilder::createSpiralStairs(float totalHeight, float riseHeight,
                                                float innerRadius, float outerRadius,
                                                float totalAngle)
{
    auto data = generateSpiralStairs(totalHeight, riseHeight, innerRadius, outerRadius, totalAngle);
    Mesh mesh;
    mesh.upload(data.vertices, data.indices);
    return mesh;
}

} // namespace Vestige
