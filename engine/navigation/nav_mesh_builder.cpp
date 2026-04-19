// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file nav_mesh_builder.cpp
/// @brief NavMeshBuilder implementation — Recast navmesh generation.
#include "navigation/nav_mesh_builder.h"
#include "core/logger.h"
#include "scene/scene.h"
#include "scene/entity.h"
#include "scene/mesh_renderer.h"

#include <Recast.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>

#include <glad/gl.h>

#include <chrono>
#include <cstring>

namespace Vestige
{

NavMeshBuilder::NavMeshBuilder() = default;

NavMeshBuilder::~NavMeshBuilder()
{
    clear();
}

void NavMeshBuilder::clear()
{
    if (m_navMesh)
    {
        dtFreeNavMesh(m_navMesh);
        m_navMesh = nullptr;
    }
    m_polyCount = 0;
}

void NavMeshBuilder::collectSceneGeometry(Scene& scene,
                                           std::vector<float>& vertices,
                                           std::vector<int>& indices)
{
    // Iterate all entities in the scene via forEachEntity
    scene.forEachEntity([&](Entity& entity)
    {
        // Get MeshRenderer component
        auto* meshRenderer = entity.getComponent<MeshRenderer>();
        if (!meshRenderer)
        {
            return;
        }

        const auto& meshPtr = meshRenderer->getMesh();
        if (!meshPtr)
        {
            return;
        }

        uint32_t indexCount = meshPtr->getIndexCount();
        if (indexCount == 0)
        {
            return;
        }

        // Read vertex positions back from GPU (editor-time operation)
        GLuint vao = meshPtr->getVao();
        if (vao == 0)
        {
            return;
        }

        // Vertex struct: position(vec3), normal(vec3), texCoords(vec2), tangent(vec3), bitangent(vec3)
        // = 14 floats = 56 bytes per vertex

        glBindVertexArray(vao);

        // Read index buffer
        GLint eboSize = 0;
        glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &eboSize);
        size_t numIndices = static_cast<size_t>(eboSize) / sizeof(uint32_t);

        std::vector<uint32_t> meshIndices(numIndices);
        glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, eboSize, meshIndices.data());

        // Read vertex buffer — get VBO bound to VAO attribute 0
        GLint vboBinding = 0;
        glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &vboBinding);

        GLint vboSize = 0;
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(vboBinding));
        glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &vboSize);

        // Read raw VBO data
        std::vector<uint8_t> vboData(static_cast<size_t>(vboSize));
        glGetBufferSubData(GL_ARRAY_BUFFER, 0, vboSize, vboData.data());

        glBindVertexArray(0);

        // Extract positions (stride = 56 bytes for Vertex struct, position at offset 0)
        constexpr size_t VERTEX_STRIDE = 56;  // sizeof(Vertex) = 14 floats
        size_t numVertices = static_cast<size_t>(vboSize) / VERTEX_STRIDE;

        // Get entity world transform
        glm::mat4 worldMatrix = entity.getWorldMatrix();

        // Record base vertex index for offset
        int baseVertex = static_cast<int>(vertices.size() / 3);

        // Transform and add vertices. Copy the three position floats out of
        // the uint8_t buffer via memcpy rather than reinterpret_cast to the
        // raw bytes — the latter is a strict-aliasing violation and was
        // flagged by cppcheck (invalidPointerCast, portability). memcpy is
        // the standard-blessed way to reinterpret bytes as a different
        // trivially-copyable type and optimises to the same load on AMD64.
        for (size_t v = 0; v < numVertices; ++v)
        {
            float pos[3];
            std::memcpy(pos, vboData.data() + v * VERTEX_STRIDE, sizeof(pos));
            glm::vec4 worldPos = worldMatrix * glm::vec4(pos[0], pos[1], pos[2], 1.0f);
            vertices.push_back(worldPos.x);
            vertices.push_back(worldPos.y);
            vertices.push_back(worldPos.z);
        }

        // Add indices (offset by base vertex)
        for (size_t idx = 0; idx < numIndices; ++idx)
        {
            indices.push_back(baseVertex + static_cast<int>(meshIndices[idx]));
        }
    });
}

bool NavMeshBuilder::buildFromScene(Scene& scene, const NavMeshBuildConfig& config)
{
    auto startTime = std::chrono::high_resolution_clock::now();

    // Clear previous navmesh
    clear();

    // Collect geometry
    std::vector<float> vertices;
    std::vector<int> indices;
    collectSceneGeometry(scene, vertices, indices);

    if (vertices.empty() || indices.empty())
    {
        Logger::warning("[NavMeshBuilder] No geometry found in scene");
        return false;
    }

    int nverts = static_cast<int>(vertices.size() / 3);
    int ntris = static_cast<int>(indices.size() / 3);

    Logger::info("[NavMeshBuilder] Building navmesh from " +
                 std::to_string(nverts) + " vertices, " +
                 std::to_string(ntris) + " triangles");

    // Compute bounding box
    float bmin[3] = { vertices[0], vertices[1], vertices[2] };
    float bmax[3] = { vertices[0], vertices[1], vertices[2] };
    for (int i = 1; i < nverts; ++i)
    {
        int idx = i * 3;
        for (int j = 0; j < 3; ++j)
        {
            bmin[j] = std::min(bmin[j], vertices[static_cast<size_t>(idx + j)]);
            bmax[j] = std::max(bmax[j], vertices[static_cast<size_t>(idx + j)]);
        }
    }

    // --- Recast build pipeline ---
    rcConfig cfg = {};
    cfg.cs = config.cellSize;
    cfg.ch = config.cellHeight;
    cfg.walkableSlopeAngle = config.agentMaxSlope;
    cfg.walkableHeight = static_cast<int>(ceilf(config.agentHeight / config.cellHeight));
    cfg.walkableClimb = static_cast<int>(floorf(config.agentMaxClimb / config.cellHeight));
    cfg.walkableRadius = static_cast<int>(ceilf(config.agentRadius / config.cellSize));
    cfg.maxEdgeLen = static_cast<int>(config.edgeMaxLen / config.cellSize);
    cfg.maxSimplificationError = config.edgeMaxError;
    cfg.minRegionArea = static_cast<int>(config.regionMinSize * config.regionMinSize);
    cfg.mergeRegionArea = static_cast<int>(config.regionMergeSize * config.regionMergeSize);
    cfg.maxVertsPerPoly = config.vertsPerPoly;
    cfg.detailSampleDist = config.detailSampleDist < 0.9f ? 0 : config.cellSize * config.detailSampleDist;
    cfg.detailSampleMaxError = config.cellHeight * config.detailSampleMaxError;
    rcVcopy(cfg.bmin, bmin);
    rcVcopy(cfg.bmax, bmax);
    rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

    rcContext ctx;

    // Step 1: Create heightfield
    rcHeightfield* solid = rcAllocHeightfield();
    if (!solid || !rcCreateHeightfield(&ctx, *solid, cfg.width, cfg.height,
                                        cfg.bmin, cfg.bmax, cfg.cs, cfg.ch))
    {
        Logger::error("[NavMeshBuilder] Failed to create heightfield");
        rcFreeHeightField(solid);
        return false;
    }

    // Step 2: Rasterize triangles
    std::vector<unsigned char> triAreas(static_cast<size_t>(ntris), 0);
    rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle,
                             vertices.data(), nverts,
                             indices.data(), ntris,
                             triAreas.data());
    if (!rcRasterizeTriangles(&ctx, vertices.data(), nverts,
                               indices.data(), triAreas.data(), ntris,
                               *solid, cfg.walkableClimb))
    {
        Logger::error("[NavMeshBuilder] Failed to rasterize triangles");
        rcFreeHeightField(solid);
        return false;
    }

    // Step 3: Filter walkable surfaces
    rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *solid);
    rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid);
    rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *solid);

    // Step 4: Build compact heightfield
    rcCompactHeightfield* chf = rcAllocCompactHeightfield();
    if (!chf || !rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb,
                                            *solid, *chf))
    {
        Logger::error("[NavMeshBuilder] Failed to build compact heightfield");
        rcFreeHeightField(solid);
        rcFreeCompactHeightfield(chf);
        return false;
    }
    rcFreeHeightField(solid);

    // Step 5: Erode walkable area
    if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf))
    {
        Logger::error("[NavMeshBuilder] Failed to erode walkable area");
        rcFreeCompactHeightfield(chf);
        return false;
    }

    // Step 6: Build distance field and regions
    if (!rcBuildDistanceField(&ctx, *chf))
    {
        Logger::error("[NavMeshBuilder] Failed to build distance field");
        rcFreeCompactHeightfield(chf);
        return false;
    }
    if (!rcBuildRegions(&ctx, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea))
    {
        Logger::error("[NavMeshBuilder] Failed to build regions");
        rcFreeCompactHeightfield(chf);
        return false;
    }

    // Step 7: Build contours
    rcContourSet* cset = rcAllocContourSet();
    if (!cset || !rcBuildContours(&ctx, *chf, cfg.maxSimplificationError,
                                   cfg.maxEdgeLen, *cset))
    {
        Logger::error("[NavMeshBuilder] Failed to build contours");
        rcFreeCompactHeightfield(chf);
        rcFreeContourSet(cset);
        return false;
    }

    // Step 8: Build poly mesh
    rcPolyMesh* pmesh = rcAllocPolyMesh();
    if (!pmesh || !rcBuildPolyMesh(&ctx, *cset, cfg.maxVertsPerPoly, *pmesh))
    {
        Logger::error("[NavMeshBuilder] Failed to build poly mesh");
        rcFreeCompactHeightfield(chf);
        rcFreeContourSet(cset);
        rcFreePolyMesh(pmesh);
        return false;
    }

    // Step 9: Build detail mesh
    rcPolyMeshDetail* dmesh = rcAllocPolyMeshDetail();
    if (!dmesh || !rcBuildPolyMeshDetail(&ctx, *pmesh, *chf,
                                          cfg.detailSampleDist,
                                          cfg.detailSampleMaxError, *dmesh))
    {
        Logger::error("[NavMeshBuilder] Failed to build detail mesh");
        rcFreeCompactHeightfield(chf);
        rcFreeContourSet(cset);
        rcFreePolyMesh(pmesh);
        rcFreePolyMeshDetail(dmesh);
        return false;
    }

    rcFreeCompactHeightfield(chf);
    rcFreeContourSet(cset);

    // Step 10: Create Detour navmesh
    for (int i = 0; i < pmesh->npolys; ++i)
    {
        pmesh->flags[i] = 1;  // All polygons walkable
    }

    dtNavMeshCreateParams params = {};
    params.verts = pmesh->verts;
    params.vertCount = pmesh->nverts;
    params.polys = pmesh->polys;
    params.polyAreas = pmesh->areas;
    params.polyFlags = pmesh->flags;
    params.polyCount = pmesh->npolys;
    params.nvp = pmesh->nvp;
    params.detailMeshes = dmesh->meshes;
    params.detailVerts = dmesh->verts;
    params.detailVertsCount = dmesh->nverts;
    params.detailTris = dmesh->tris;
    params.detailTriCount = dmesh->ntris;
    params.walkableHeight = config.agentHeight;
    params.walkableRadius = config.agentRadius;
    params.walkableClimb = config.agentMaxClimb;
    rcVcopy(params.bmin, pmesh->bmin);
    rcVcopy(params.bmax, pmesh->bmax);
    params.cs = cfg.cs;
    params.ch = cfg.ch;
    params.buildBvTree = true;

    unsigned char* navData = nullptr;
    int navDataSize = 0;
    if (!dtCreateNavMeshData(&params, &navData, &navDataSize))
    {
        Logger::error("[NavMeshBuilder] Failed to create Detour navmesh data");
        rcFreePolyMesh(pmesh);
        rcFreePolyMeshDetail(dmesh);
        return false;
    }

    m_navMesh = dtAllocNavMesh();
    if (!m_navMesh)
    {
        Logger::error("[NavMeshBuilder] Failed to allocate Detour navmesh");
        dtFree(navData);
        rcFreePolyMesh(pmesh);
        rcFreePolyMeshDetail(dmesh);
        return false;
    }

    dtStatus status = m_navMesh->init(navData, navDataSize, DT_TILE_FREE_DATA);
    if (dtStatusFailed(status))
    {
        Logger::error("[NavMeshBuilder] Failed to initialize Detour navmesh");
        dtFreeNavMesh(m_navMesh);
        m_navMesh = nullptr;
        rcFreePolyMesh(pmesh);
        rcFreePolyMeshDetail(dmesh);
        return false;
    }

    m_polyCount = pmesh->npolys;

    rcFreePolyMesh(pmesh);
    rcFreePolyMeshDetail(dmesh);

    auto endTime = std::chrono::high_resolution_clock::now();
    m_lastBuildTimeMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();

    Logger::info("[NavMeshBuilder] Built navmesh: " +
                 std::to_string(m_polyCount) + " polygons in " +
                 std::to_string(m_lastBuildTimeMs) + " ms");
    return true;
}

} // namespace Vestige
