// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file grass_renderer.cpp
/// @brief GPU procedural grass renderer — G2: CPU per-chunk placement + PCG gating +
///        clumping (design docs/phases/phase_10_meadow_gpu_grass_design.md §5.2/§5.5/§5.6).

#include "renderer/grass_renderer.h"

#include "core/logger.h"
#include "environment/grass_placement.h"   // predicates + makeGrassBlade (+ grass_clump hashes)
#include "environment/terrain.h"
#include "utils/frustum.h"                  // extractFrustumPlanes + isAabbInFrustum (reuse)

#include <algorithm>
#include <cmath>

namespace Vestige
{

GrassRenderer::~GrassRenderer()
{
    shutdown();
}

bool GrassRenderer::init(const std::string& assetPath)
{
    if (m_initialized)
    {
        return true;
    }

    if (!m_shader.loadFromFiles(assetPath + "/shaders/grass.vert.glsl",
                                assetPath + "/shaders/grass.frag.glsl"))
    {
        Logger::error("GrassRenderer: failed to load grass shaders");
        return false;
    }

    // An attribute-less draw (geometry from gl_VertexID/gl_InstanceID + SSBO) still needs
    // a bound non-zero VAO — glDrawArrays* with VAO 0 is GL_INVALID_OPERATION in a core
    // profile (design §5.1; mirrors particle_renderer's m_gpuVao).
    glCreateVertexArrays(1, &m_vao);

    m_initialized = true;
    Logger::info("GrassRenderer initialized (G2)");
    return true;
}

void GrassRenderer::buildField(const Terrain& terrain, const GrassConfig& cfg)
{
    if (!m_initialized)
    {
        return;
    }
    m_config = cfg;
    m_chunks.clear();

    const TerrainConfig& tc = terrain.getConfig();
    const int W = terrain.getWidth();
    const int D = terrain.getDepth();

    // Meadow interior bounds, inset by the edge margin: getSplatWeight returns grass=1 out
    // of bounds (terrain.cpp:548), so an unclamped border candidate would over-spawn (§5.2).
    const float x0 = tc.origin.x + cfg.edgeMargin;
    const float z0 = tc.origin.z + cfg.edgeMargin;
    const float x1 = tc.origin.x + static_cast<float>(W - 1) * tc.spacingX - cfg.edgeMargin;
    const float z1 = tc.origin.z + static_cast<float>(D - 1) * tc.spacingZ - cfg.edgeMargin;

    // Candidate grid spacing from the target near-density (each candidate jittered in-cell).
    const float cellSpacing = 1.0f / std::sqrt(std::max(1.0f, cfg.bladesPerSqM));

    // Clump-max reach for the AABB pad (§5.2a): height *= mix(1, clumpHeight≤1.6, strength),
    // and lean is raised by clumpBend·strength — so the drawn geometry exceeds the roots.
    const float hMul = 1.0f + 0.6f * cfg.clumpStrength;          // clump-max height multiplier
    const float maxBladeH = cfg.maxHeight * hMul;
    const float maxLeanReach = maxBladeH * cfg.maxLean * (1.0f + cfg.clumpStrength);

    std::vector<GrassBlade> allBlades;

    const int chunksX = std::max(1, static_cast<int>(std::ceil((x1 - x0) / cfg.chunkSize)));
    const int chunksZ = std::max(1, static_cast<int>(std::ceil((z1 - z0) / cfg.chunkSize)));

    for (int cz = 0; cz < chunksZ; ++cz)
    {
        for (int cx = 0; cx < chunksX; ++cx)
        {
            const float cxMin = x0 + static_cast<float>(cx) * cfg.chunkSize;
            const float czMin = z0 + static_cast<float>(cz) * cfg.chunkSize;
            const float cxMax = std::min(x1, cxMin + cfg.chunkSize);
            const float czMax = std::min(z1, czMin + cfg.chunkSize);

            const std::uint32_t chunkId = grassCellHash(cx, cz, 0x9AB10001u);

            std::vector<GrassBlade> chunkBlades;
            float minY = 1.0e30f, maxY = -1.0e30f;

            std::uint32_t idx = 0;
            for (float gz = czMin; gz <= czMax; gz += cellSpacing)
            {
                for (float gx = cxMin; gx <= cxMax; gx += cellSpacing)
                {
                    const std::uint32_t key = grassHashU32(chunkId + idx * 0x9E3779B1u);
                    ++idx;

                    // Jitter the candidate within its cell (deterministic).
                    const float jx = grassU32ToUnit(grassHashU32(key ^ 0x00000011u));
                    const float jz = grassU32ToUnit(grassHashU32(key ^ 0x00000022u));
                    float wx = gx + (jx - 0.5f) * cellSpacing;
                    float wz = gz + (jz - 0.5f) * cellSpacing;
                    wx = glm::clamp(wx, x0, x1);   // stay in the interior (OOB reads grass=1)
                    wz = glm::clamp(wz, z0, z1);

                    if (grassInExclusionDisc(wx, wz, cfg.exclusionCenter, cfg.exclusionRadius))
                    {
                        continue;   // inside the pond
                    }

                    const glm::vec3 n = terrain.getNormal(wx, wz);
                    const int tx = static_cast<int>(std::lround((wx - tc.origin.x) / tc.spacingX));
                    const int tz = static_cast<int>(std::lround((wz - tc.origin.z) / tc.spacingZ));
                    const float grassW = terrain.getSplatWeight(tx, tz).r;
                    const float roll = grassU32ToUnit(grassHashU32(key ^ 0x00000033u));
                    if (!grassCandidateAccepted(n.y, grassW, roll, cfg))
                    {
                        continue;   // too steep, or thinned out over dirt/rock
                    }

                    const float wy = terrain.getHeight(wx, wz);
                    chunkBlades.push_back(makeGrassBlade(glm::vec3(wx, wy, wz), key, cfg));
                    minY = std::min(minY, wy);
                    maxY = std::max(maxY, wy);
                }
            }

            if (chunkBlades.empty())
            {
                continue;
            }

            // Shuffle within the chunk so any prefix [0, k) is a spatially-uniform subset —
            // the property G3's per-blade distance-LOD prefix-thinning relies on (§5.2).
            // Deterministic: order by a re-hash of each blade's hash.
            std::sort(chunkBlades.begin(), chunkBlades.end(),
                      [](const GrassBlade& a, const GrassBlade& b)
                      {
                          return grassHashU32(a.hash) < grassHashU32(b.hash);
                      });

            GrassChunk desc;
            desc.baseOffset = static_cast<std::uint32_t>(allBlades.size());
            desc.count = static_cast<std::uint32_t>(chunkBlades.size());
            // Pad the AABB by the clump-max height + lean/bend reach (§5.2a): the drawn
            // tussocks are taller and lean past their roots, so an un-padded AABB would
            // false-cull them at the frustum edge in G3.
            desc.aabbMin = glm::vec3(cxMin - maxLeanReach, minY, czMin - maxLeanReach);
            desc.aabbMax = glm::vec3(cxMax + maxLeanReach, maxY + maxBladeH, czMax + maxLeanReach);
            m_chunks.push_back(desc);

            allBlades.insert(allBlades.end(), chunkBlades.begin(), chunkBlades.end());
        }
    }

    uploadBlades(allBlades);
    Logger::info("GrassRenderer: built field — " + std::to_string(m_bladeCount)
                 + " blades across " + std::to_string(m_chunks.size()) + " chunks");
}

void GrassRenderer::uploadBlades(const std::vector<GrassBlade>& blades)
{
    m_bladeCount = static_cast<GLsizei>(blades.size());
    if (m_bladeCount == 0)
    {
        return;
    }

    if (m_bladeSSBO == 0)
    {
        glCreateBuffers(1, &m_bladeSSBO);
    }
    // Mutable storage (glNamedBufferData): rebuilt when the terrain changes, so the buffer
    // must stay reallocatable.
    glNamedBufferData(m_bladeSSBO,
                      static_cast<GLsizeiptr>(blades.size() * sizeof(GrassBlade)),
                      blades.data(), GL_STATIC_DRAW);
}

void GrassRenderer::render(const glm::mat4& viewProjection, const glm::vec3& cameraPos)
{
    m_drawnChunks = 0;
    if (!m_initialized || m_bladeCount == 0 || m_shader.getId() == 0)
    {
        return;
    }

    // Opaque, two-sided blades (design §5.1): depth-tested + written, blend off, back-face
    // cull off. Set explicitly so prior passes' state can't leak in.
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    m_shader.use();
    m_shader.setMat4("u_viewProjection", viewProjection);
    m_shader.setVec3("u_cameraPos", cameraPos);
    m_shader.setFloat("u_clumpCellSize", m_config.clumpScale);
    m_shader.setFloat("u_clumpStrength", m_config.clumpStrength);
    // LOD schedule — constant across chunks; the shader fades each blade per its distance.
    m_shader.setFloat("u_lodNearMid", m_lodBands.nearMid);
    m_shader.setFloat("u_lodMidFar", m_lodBands.midFar);
    m_shader.setFloat("u_lodBandWidth", m_lodBands.bandWidth);
    m_shader.setFloat("u_lodNearFrac", m_lodBands.nearFraction);
    m_shader.setFloat("u_lodMidFrac", m_lodBands.midFraction);
    m_shader.setFloat("u_lodFarFrac", m_lodBands.farFraction);
    m_shader.setFloat("u_lodRankBand", m_lodBands.rankBand);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_bladeSSBO);
    glBindVertexArray(m_vao);

    // Per-chunk frustum cull + distance LOD (§5.3). The segment tier (u_segments) and the
    // submitted blade fraction (instanceCount) key on the chunk's NEAREST point so a chunk's
    // near-edge blades finish fading before it drops instanceCount; the shader tapers each
    // blade over the same grassKeptFraction curve, so nothing pops. One draw per visible
    // chunk (tens), indexed via u_baseOffset into the shared buffer (§5.5).
    const FrustumPlanes planes = extractFrustumPlanes(viewProjection);
    for (const GrassChunk& c : m_chunks)
    {
        const AABB box{c.aabbMin, c.aabbMax};
        if (!isAabbInFrustum(box, planes))
        {
            continue;   // outside the view frustum
        }

        // Nearest point of the AABB to the camera → the LOD distance (§5.3 precondition).
        const glm::vec3 nearest = glm::clamp(cameraPos, box.min, box.max);
        const float dNear = glm::distance(cameraPos, nearest);

        const GrassLod lod = grassLodForDistance(dNear, m_lodBands);
        if (!lod.draw)
        {
            continue;   // beyond the cull distance
        }

        // ceil so a partially-faded boundary blade is still submitted (rank < instanceFraction).
        const GLsizei instanceCount = std::min<GLsizei>(
            static_cast<GLsizei>(c.count),
            static_cast<GLsizei>(std::ceil(lod.instanceFraction * static_cast<float>(c.count))));
        if (instanceCount <= 0)
        {
            continue;
        }

        m_shader.setUInt("u_baseOffset", c.baseOffset);
        m_shader.setInt("u_segments", lod.segments);
        m_shader.setFloat("u_chunkCount", static_cast<float>(c.count));
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 2 * lod.segments + 1, instanceCount);
        ++m_drawnChunks;
    }

    glBindVertexArray(0);
}

void GrassRenderer::shutdown()
{
    if (m_vao != 0)
    {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_bladeSSBO != 0)
    {
        glDeleteBuffers(1, &m_bladeSSBO);
        m_bladeSSBO = 0;
    }
    m_shader.destroy();
    m_bladeCount = 0;
    m_chunks.clear();
    m_initialized = false;
}

} // namespace Vestige
