/// @file terrain.cpp
/// @brief Heightmap-based terrain with CDLOD quadtree LOD system.

#include "environment/terrain.h"
#include "core/logger.h"

#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>

namespace Vestige
{

Terrain::Terrain() = default;

Terrain::~Terrain()
{
    shutdown();
}

bool Terrain::initialize(const TerrainConfig& config)
{
    if (m_initialized)
    {
        shutdown();
    }

    m_config = config;

    // Validate dimensions (should be power-of-two + 1)
    if (m_config.width < 3 || m_config.depth < 3)
    {
        Logger::error("Terrain: width and depth must be >= 3");
        return false;
    }

    // Grid resolution must be odd so boundary vertices (index 0 and index res-1)
    // are both even-indexed "anchor" vertices in the CDLOD morph scheme.
    // Even resolution causes seams: the right/top boundary vertex becomes an odd
    // "morph target" in one node but an even "anchor" in the adjacent node.
    if (m_config.gridResolution % 2 == 0)
    {
        Logger::warning("Terrain: gridResolution must be odd for seamless LOD morphing, "
                     "adjusting " + std::to_string(m_config.gridResolution)
                     + " -> " + std::to_string(m_config.gridResolution + 1));
        m_config.gridResolution += 1;
    }

    int totalTexels = m_config.width * m_config.depth;

    // Initialize flat heightmap (all zeros)
    m_heightData.resize(static_cast<size_t>(totalTexels), 0.0f);

    // Initialize normals (all pointing up)
    m_normalData.resize(static_cast<size_t>(totalTexels), glm::vec3(0.0f, 1.0f, 0.0f));

    // Initialize splatmap (layer 0 = 100%, rest = 0%)
    m_splatData.resize(static_cast<size_t>(totalTexels), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));

    // Create GPU textures
    createGpuTextures();

    // Compute LOD ranges (geometric progression)
    m_lodRanges.resize(static_cast<size_t>(m_config.maxLodLevels));
    for (int i = 0; i < m_config.maxLodLevels; ++i)
    {
        m_lodRanges[static_cast<size_t>(i)] =
            m_config.baseLodDistance * std::pow(2.0f, static_cast<float>(i));
    }

    // Build the CDLOD quadtree
    buildQuadtree();

    m_initialized = true;
    Logger::info("Terrain initialized: " + std::to_string(m_config.width) + "x"
                 + std::to_string(m_config.depth) + " ("
                 + std::to_string(getWorldWidth()) + "m x "
                 + std::to_string(getWorldDepth()) + "m)");
    return true;
}

void Terrain::shutdown()
{
    if (m_heightmapTex)
    {
        glDeleteTextures(1, &m_heightmapTex);
        m_heightmapTex = 0;
    }
    if (m_normalMapTex)
    {
        glDeleteTextures(1, &m_normalMapTex);
        m_normalMapTex = 0;
    }
    if (m_splatmapTex)
    {
        glDeleteTextures(1, &m_splatmapTex);
        m_splatmapTex = 0;
    }

    m_heightData.clear();
    m_normalData.clear();
    m_splatData.clear();
    m_nodes.clear();
    m_lodRanges.clear();
    m_rootNode = -1;
    m_initialized = false;
}

// ---------------------------------------------------------------------------
// Height queries
// ---------------------------------------------------------------------------

float Terrain::getHeight(float worldX, float worldZ) const
{
    float tx = 0.0f;
    float tz = 0.0f;
    worldToTexel(worldX, worldZ, tx, tz);

    int ix = static_cast<int>(std::floor(tx));
    int iz = static_cast<int>(std::floor(tz));
    float fx = tx - static_cast<float>(ix);
    float fz = tz - static_cast<float>(iz);

    ix = std::clamp(ix, 0, m_config.width - 2);
    iz = std::clamp(iz, 0, m_config.depth - 2);

    int w = m_config.width;
    float h00 = m_heightData[static_cast<size_t>(iz * w + ix)];
    float h10 = m_heightData[static_cast<size_t>(iz * w + ix + 1)];
    float h01 = m_heightData[static_cast<size_t>((iz + 1) * w + ix)];
    float h11 = m_heightData[static_cast<size_t>((iz + 1) * w + ix + 1)];

    float h0 = h00 * (1.0f - fx) + h10 * fx;
    float h1 = h01 * (1.0f - fx) + h11 * fx;
    float h = h0 * (1.0f - fz) + h1 * fz;

    return h * m_config.heightScale;
}

glm::vec3 Terrain::getNormal(float worldX, float worldZ) const
{
    float tx = 0.0f;
    float tz = 0.0f;
    worldToTexel(worldX, worldZ, tx, tz);

    int ix = static_cast<int>(std::floor(tx));
    int iz = static_cast<int>(std::floor(tz));
    float fx = tx - static_cast<float>(ix);
    float fz = tz - static_cast<float>(iz);

    ix = std::clamp(ix, 0, m_config.width - 2);
    iz = std::clamp(iz, 0, m_config.depth - 2);

    int w = m_config.width;
    const auto& n00 = m_normalData[static_cast<size_t>(iz * w + ix)];
    const auto& n10 = m_normalData[static_cast<size_t>(iz * w + ix + 1)];
    const auto& n01 = m_normalData[static_cast<size_t>((iz + 1) * w + ix)];
    const auto& n11 = m_normalData[static_cast<size_t>((iz + 1) * w + ix + 1)];

    glm::vec3 n0 = n00 * (1.0f - fx) + n10 * fx;
    glm::vec3 n1 = n01 * (1.0f - fx) + n11 * fx;
    return glm::normalize(n0 * (1.0f - fz) + n1 * fz);
}

float Terrain::getRawHeight(int x, int z) const
{
    if (x < 0 || x >= m_config.width || z < 0 || z >= m_config.depth)
    {
        return 0.0f;
    }
    return m_heightData[static_cast<size_t>(z * m_config.width + x)];
}

void Terrain::setRawHeight(int x, int z, float height)
{
    if (x < 0 || x >= m_config.width || z < 0 || z >= m_config.depth)
    {
        return;
    }
    m_heightData[static_cast<size_t>(z * m_config.width + x)] = height;
}

// ---------------------------------------------------------------------------
// CDLOD Quadtree
// ---------------------------------------------------------------------------

void Terrain::buildQuadtree()
{
    m_nodes.clear();

    // The root node covers the entire terrain
    float halfW = getWorldWidth() * 0.5f;
    float halfD = getWorldDepth() * 0.5f;
    float halfSize = std::max(halfW, halfD);

    float cx = m_config.origin.x + halfW;
    float cz = m_config.origin.z + halfD;

    int maxLod = m_config.maxLodLevels - 1;
    m_rootNode = buildNode(cx, cz, halfSize, maxLod);
}

int Terrain::buildNode(float cx, float cz, float halfSize, int lodLevel)
{
    // Determine min/max height in this node's region
    float worldMinX = cx - halfSize;
    float worldMaxX = cx + halfSize;
    float worldMinZ = cz - halfSize;
    float worldMaxZ = cz + halfSize;

    // Convert to texel range
    float txMin = 0.0f;
    float tzMin = 0.0f;
    float txMax = 0.0f;
    float tzMax = 0.0f;
    worldToTexel(worldMinX, worldMinZ, txMin, tzMin);
    worldToTexel(worldMaxX, worldMaxZ, txMax, tzMax);

    int ixMin = std::clamp(static_cast<int>(std::floor(txMin)), 0, m_config.width - 1);
    int izMin = std::clamp(static_cast<int>(std::floor(tzMin)), 0, m_config.depth - 1);
    int ixMax = std::clamp(static_cast<int>(std::ceil(txMax)), 0, m_config.width - 1);
    int izMax = std::clamp(static_cast<int>(std::ceil(tzMax)), 0, m_config.depth - 1);

    float minH = std::numeric_limits<float>::max();
    float maxH = std::numeric_limits<float>::lowest();
    int w = m_config.width;

    for (int z = izMin; z <= izMax; ++z)
    {
        for (int x = ixMin; x <= ixMax; ++x)
        {
            float h = m_heightData[static_cast<size_t>(z * w + x)];
            minH = std::min(minH, h);
            maxH = std::max(maxH, h);
        }
    }

    // Scale to world units
    minH *= m_config.heightScale;
    maxH *= m_config.heightScale;

    CDLODNode node;
    node.center = glm::vec2(cx, cz);
    node.size = halfSize;
    node.minHeight = minH;
    node.maxHeight = maxH;
    node.lodLevel = lodLevel;

    int nodeIdx = static_cast<int>(m_nodes.size());
    m_nodes.push_back(node);

    // Recurse into children if not at finest level
    if (lodLevel > 0)
    {
        float childHalf = halfSize * 0.5f;
        // Only create children if the node is large enough to benefit from subdivision
        // (at least a few texels across)
        float texelsAcross = (halfSize * 2.0f) / m_config.spacingX;
        if (texelsAcross > static_cast<float>(m_config.gridResolution))
        {
            m_nodes[static_cast<size_t>(nodeIdx)].children[0] =
                buildNode(cx - childHalf, cz - childHalf, childHalf, lodLevel - 1);
            m_nodes[static_cast<size_t>(nodeIdx)].children[1] =
                buildNode(cx + childHalf, cz - childHalf, childHalf, lodLevel - 1);
            m_nodes[static_cast<size_t>(nodeIdx)].children[2] =
                buildNode(cx - childHalf, cz + childHalf, childHalf, lodLevel - 1);
            m_nodes[static_cast<size_t>(nodeIdx)].children[3] =
                buildNode(cx + childHalf, cz + childHalf, childHalf, lodLevel - 1);
        }
    }

    return nodeIdx;
}

void Terrain::selectNodes(const Camera& camera, float aspectRatio,
                          std::vector<TerrainDrawNode>& outNodes) const
{
    if (m_rootNode < 0 || m_nodes.empty())
    {
        return;
    }

    glm::mat4 cullingVP = camera.getCullingProjectionMatrix(aspectRatio)
                        * camera.getViewMatrix();
    FrustumPlanes frustum = extractFrustumPlanes(cullingVP);

    selectNode(m_rootNode, camera.getPosition(), frustum, outNodes);
}

void Terrain::selectNode(int nodeIdx, const glm::vec3& cameraPos,
                         const FrustumPlanes& frustum,
                         std::vector<TerrainDrawNode>& outNodes) const
{
    const CDLODNode& node = m_nodes[static_cast<size_t>(nodeIdx)];

    // Build AABB for frustum culling
    AABB nodeBox;
    nodeBox.min = glm::vec3(node.center.x - node.size,
                            node.minHeight,
                            node.center.y - node.size);
    nodeBox.max = glm::vec3(node.center.x + node.size,
                            node.maxHeight,
                            node.center.y + node.size);

    // Frustum cull
    if (!isAabbInFrustum(nodeBox, frustum))
    {
        return;
    }

    // Distance from camera to closest point on node (not center) for better LOD selection
    float closestX = std::clamp(cameraPos.x, node.center.x - node.size,
                                              node.center.x + node.size);
    float closestZ = std::clamp(cameraPos.z, node.center.y - node.size,
                                              node.center.y + node.size);
    float dist = glm::length(glm::vec2(cameraPos.x - closestX, cameraPos.z - closestZ));

    // Check if this node's LOD range covers the distance
    int lod = node.lodLevel;
    float lodRange = m_lodRanges[static_cast<size_t>(lod)];

    // If this node has children and the camera is close enough to warrant finer detail
    bool hasChildren = (node.children[0] >= 0);
    if (hasChildren && dist < lodRange)
    {
        // Recurse into children for finer detail
        for (int i = 0; i < 4; ++i)
        {
            if (node.children[i] >= 0)
            {
                selectNode(node.children[i], cameraPos, frustum, outNodes);
            }
        }
        return;
    }

    // Select this node for rendering
    TerrainDrawNode drawNode;
    drawNode.worldOffset = glm::vec2(node.center.x - node.size,
                                     node.center.y - node.size);
    drawNode.scale = node.size * 2.0f;
    drawNode.lodLevel = lod;

    // Compute morph factor based on distance within this LOD's range
    // Morph ramps from 0 to 1 as the node approaches the far edge of its LOD range.
    // This smoothly blends fine vertices toward coarse positions, preventing cracks.
    float prevRange = (lod > 0) ? m_lodRanges[static_cast<size_t>(lod - 1)] : 0.0f;
    float distFromCenter = glm::length(glm::vec2(cameraPos.x, cameraPos.z) - node.center);
    float rangeSpan = lodRange - prevRange;
    if (rangeSpan > 0.0f)
    {
        float t = (distFromCenter - prevRange) / rangeSpan;
        // Morph activates in the upper half of the range
        drawNode.morphFactor = std::clamp(t * 2.0f - 1.0f, 0.0f, 1.0f);
    }
    else
    {
        drawNode.morphFactor = 0.0f;
    }

    outNodes.push_back(drawNode);
}

// ---------------------------------------------------------------------------
// GPU Textures
// ---------------------------------------------------------------------------

void Terrain::createGpuTextures()
{
    int w = m_config.width;
    int d = m_config.depth;

    // Heightmap: R32F
    glGenTextures(1, &m_heightmapTex);
    glBindTexture(GL_TEXTURE_2D, m_heightmapTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, w, d, 0,
                 GL_RED, GL_FLOAT, m_heightData.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Normal map: RGB8 (encoded as n * 0.5 + 0.5)
    std::vector<uint8_t> normalBytes(static_cast<size_t>(w * d * 3));
    for (size_t i = 0; i < m_normalData.size(); ++i)
    {
        const auto& n = m_normalData[i];
        normalBytes[i * 3 + 0] = static_cast<uint8_t>((n.x * 0.5f + 0.5f) * 255.0f);
        normalBytes[i * 3 + 1] = static_cast<uint8_t>((n.y * 0.5f + 0.5f) * 255.0f);
        normalBytes[i * 3 + 2] = static_cast<uint8_t>((n.z * 0.5f + 0.5f) * 255.0f);
    }

    glGenTextures(1, &m_normalMapTex);
    glBindTexture(GL_TEXTURE_2D, m_normalMapTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, d, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, normalBytes.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Splatmap: RGBA8
    std::vector<uint8_t> splatBytes(static_cast<size_t>(w * d * 4));
    for (size_t i = 0; i < m_splatData.size(); ++i)
    {
        const auto& s = m_splatData[i];
        splatBytes[i * 4 + 0] = static_cast<uint8_t>(s.r * 255.0f);
        splatBytes[i * 4 + 1] = static_cast<uint8_t>(s.g * 255.0f);
        splatBytes[i * 4 + 2] = static_cast<uint8_t>(s.b * 255.0f);
        splatBytes[i * 4 + 3] = static_cast<uint8_t>(s.a * 255.0f);
    }

    glGenTextures(1, &m_splatmapTex);
    glBindTexture(GL_TEXTURE_2D, m_splatmapTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, d, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, splatBytes.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);
}

// ---------------------------------------------------------------------------
// Partial GPU updates
// ---------------------------------------------------------------------------

void Terrain::updateHeightmapRegion(int x, int z, int w, int h)
{
    if (!m_heightmapTex) return;

    // Extract the sub-rectangle from the CPU data
    std::vector<float> region(static_cast<size_t>(w * h));
    for (int rz = 0; rz < h; ++rz)
    {
        int srcZ = z + rz;
        if (srcZ < 0 || srcZ >= m_config.depth) continue;
        for (int rx = 0; rx < w; ++rx)
        {
            int srcX = x + rx;
            if (srcX < 0 || srcX >= m_config.width) continue;
            region[static_cast<size_t>(rz * w + rx)] =
                m_heightData[static_cast<size_t>(srcZ * m_config.width + srcX)];
        }
    }

    glBindTexture(GL_TEXTURE_2D, m_heightmapTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, z, w, h,
                    GL_RED, GL_FLOAT, region.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Terrain::updateNormalMapRegion(int x, int z, int w, int h)
{
    // Recompute normals in the region (with 1-texel border for correct gradients)
    int x0 = std::max(0, x - 1);
    int z0 = std::max(0, z - 1);
    int x1 = std::min(m_config.width - 1, x + w);
    int z1 = std::min(m_config.depth - 1, z + h);

    for (int nz = z0; nz <= z1; ++nz)
    {
        for (int nx = x0; nx <= x1; ++nx)
        {
            computeNormalAt(nx, nz);
        }
    }

    // Upload the updated region
    int uploadW = x1 - x0 + 1;
    int uploadH = z1 - z0 + 1;
    std::vector<uint8_t> normalBytes(static_cast<size_t>(uploadW * uploadH * 3));
    for (int rz = 0; rz < uploadH; ++rz)
    {
        for (int rx = 0; rx < uploadW; ++rx)
        {
            int srcIdx = (z0 + rz) * m_config.width + (x0 + rx);
            const auto& n = m_normalData[static_cast<size_t>(srcIdx)];
            size_t dstIdx = static_cast<size_t>(rz * uploadW + rx) * 3;
            normalBytes[dstIdx + 0] = static_cast<uint8_t>((n.x * 0.5f + 0.5f) * 255.0f);
            normalBytes[dstIdx + 1] = static_cast<uint8_t>((n.y * 0.5f + 0.5f) * 255.0f);
            normalBytes[dstIdx + 2] = static_cast<uint8_t>((n.z * 0.5f + 0.5f) * 255.0f);
        }
    }

    glBindTexture(GL_TEXTURE_2D, m_normalMapTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x0, z0, uploadW, uploadH,
                    GL_RGB, GL_UNSIGNED_BYTE, normalBytes.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Terrain::updateSplatmapRegion(int x, int z, int w, int h)
{
    if (!m_splatmapTex) return;

    std::vector<uint8_t> splatBytes(static_cast<size_t>(w * h * 4));
    for (int rz = 0; rz < h; ++rz)
    {
        int srcZ = z + rz;
        if (srcZ < 0 || srcZ >= m_config.depth) continue;
        for (int rx = 0; rx < w; ++rx)
        {
            int srcX = x + rx;
            if (srcX < 0 || srcX >= m_config.width) continue;
            const auto& s = m_splatData[static_cast<size_t>(srcZ * m_config.width + srcX)];
            size_t dstIdx = static_cast<size_t>(rz * w + rx) * 4;
            splatBytes[dstIdx + 0] = static_cast<uint8_t>(s.r * 255.0f);
            splatBytes[dstIdx + 1] = static_cast<uint8_t>(s.g * 255.0f);
            splatBytes[dstIdx + 2] = static_cast<uint8_t>(s.b * 255.0f);
            splatBytes[dstIdx + 3] = static_cast<uint8_t>(s.a * 255.0f);
        }
    }

    glBindTexture(GL_TEXTURE_2D, m_splatmapTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, z, w, h,
                    GL_RGBA, GL_UNSIGNED_BYTE, splatBytes.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ---------------------------------------------------------------------------
// Splatmap access
// ---------------------------------------------------------------------------

void Terrain::setSplatWeight(int x, int z, int channel, float weight)
{
    if (x < 0 || x >= m_config.width || z < 0 || z >= m_config.depth) return;
    if (channel < 0 || channel > 3) return;

    auto& s = m_splatData[static_cast<size_t>(z * m_config.width + x)];
    s[channel] = weight;
}

glm::vec4 Terrain::getSplatWeight(int x, int z) const
{
    if (x < 0 || x >= m_config.width || z < 0 || z >= m_config.depth)
    {
        return glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    }
    return m_splatData[static_cast<size_t>(z * m_config.width + x)];
}

// ---------------------------------------------------------------------------
// Raycast
// ---------------------------------------------------------------------------

bool Terrain::raycast(const Ray& ray, float maxDist, glm::vec3& outHit) const
{
    float stepSize = m_config.spacingX;  // Step by one texel width

    for (float t = 0.0f; t < maxDist; t += stepSize)
    {
        glm::vec3 p = ray.origin + ray.direction * t;
        float terrainH = getHeight(p.x, p.z);

        if (p.y < terrainH)
        {
            // Binary search refinement
            float lo = t - stepSize;
            float hi = t;
            for (int i = 0; i < 8; ++i)
            {
                float mid = (lo + hi) * 0.5f;
                glm::vec3 mp = ray.origin + ray.direction * mid;
                if (mp.y < getHeight(mp.x, mp.z))
                {
                    hi = mid;
                }
                else
                {
                    lo = mid;
                }
            }
            outHit = ray.origin + ray.direction * ((lo + hi) * 0.5f);
            return true;
        }
    }

    return false;
}

// ---------------------------------------------------------------------------
// Normal computation
// ---------------------------------------------------------------------------

void Terrain::computeAllNormals()
{
    for (int z = 0; z < m_config.depth; ++z)
    {
        for (int x = 0; x < m_config.width; ++x)
        {
            computeNormalAt(x, z);
        }
    }
}

void Terrain::computeNormalAt(int x, int z)
{
    // Central differences: sample 4 cardinal neighbors
    float hL = getRawHeight(x - 1, z);
    float hR = getRawHeight(x + 1, z);
    float hD = getRawHeight(x, z - 1);
    float hU = getRawHeight(x, z + 1);

    // Scale by height scale and spacing for correct world-space normal
    float dX = (hL - hR) * m_config.heightScale;
    float dZ = (hD - hU) * m_config.heightScale;
    float dY = 2.0f * m_config.spacingX;  // Two texels apart

    glm::vec3 normal = glm::normalize(glm::vec3(dX, dY, dZ));
    m_normalData[static_cast<size_t>(z * m_config.width + x)] = normal;
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

void Terrain::worldToTexel(float worldX, float worldZ, float& tx, float& tz) const
{
    tx = (worldX - m_config.origin.x) / m_config.spacingX;
    tz = (worldZ - m_config.origin.z) / m_config.spacingZ;
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

nlohmann::json Terrain::serializeSettings() const
{
    nlohmann::json j;
    j["width"] = m_config.width;
    j["depth"] = m_config.depth;
    j["spacingX"] = m_config.spacingX;
    j["spacingZ"] = m_config.spacingZ;
    j["heightScale"] = m_config.heightScale;
    j["origin"] = {m_config.origin.x, m_config.origin.y, m_config.origin.z};
    j["gridResolution"] = m_config.gridResolution;
    j["maxLodLevels"] = m_config.maxLodLevels;
    j["baseLodDistance"] = m_config.baseLodDistance;
    return j;
}

bool Terrain::deserializeSettings(const nlohmann::json& j)
{
    TerrainConfig config;

    try
    {
        config.width = j.value("width", config.width);
        config.depth = j.value("depth", config.depth);
        config.spacingX = j.value("spacingX", config.spacingX);
        config.spacingZ = j.value("spacingZ", config.spacingZ);
        config.heightScale = j.value("heightScale", config.heightScale);
        config.gridResolution = j.value("gridResolution", config.gridResolution);
        config.maxLodLevels = j.value("maxLodLevels", config.maxLodLevels);
        config.baseLodDistance = j.value("baseLodDistance", config.baseLodDistance);

        if (j.contains("origin") && j["origin"].is_array() && j["origin"].size() == 3)
        {
            config.origin.x = j["origin"][0].get<float>();
            config.origin.y = j["origin"][1].get<float>();
            config.origin.z = j["origin"][2].get<float>();
        }
    }
    catch (const nlohmann::json::exception& e)
    {
        Logger::error("Terrain: failed to deserialize settings: " + std::string(e.what()));
        return false;
    }

    return initialize(config);
}

bool Terrain::saveHeightmap(const std::filesystem::path& path) const
{
    if (!m_initialized)
    {
        Logger::error("Terrain: cannot save heightmap — not initialized");
        return false;
    }

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        Logger::error("Terrain: failed to open " + path.string() + " for writing");
        return false;
    }

    file.write(reinterpret_cast<const char*>(m_heightData.data()),
               static_cast<std::streamsize>(m_heightData.size() * sizeof(float)));

    Logger::info("Terrain: saved heightmap to " + path.string()
                 + " (" + std::to_string(m_heightData.size() * sizeof(float)) + " bytes)");
    return true;
}

bool Terrain::loadHeightmap(const std::filesystem::path& path)
{
    if (!m_initialized)
    {
        Logger::error("Terrain: cannot load heightmap — not initialized");
        return false;
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        Logger::error("Terrain: failed to open " + path.string() + " for reading");
        return false;
    }

    auto fileSize = file.tellg();
    auto expectedSize = static_cast<std::streamoff>(m_heightData.size() * sizeof(float));
    if (fileSize != expectedSize)
    {
        Logger::error("Terrain: heightmap file size mismatch: expected "
                     + std::to_string(expectedSize) + " bytes, got "
                     + std::to_string(fileSize));
        return false;
    }

    file.seekg(0);
    file.read(reinterpret_cast<char*>(m_heightData.data()),
              static_cast<std::streamsize>(m_heightData.size() * sizeof(float)));

    // Rebuild GPU textures and quadtree from loaded data
    updateHeightmapRegion(0, 0, m_config.width, m_config.depth);
    computeAllNormals();
    updateNormalMapRegion(0, 0, m_config.width, m_config.depth);
    buildQuadtree();

    Logger::info("Terrain: loaded heightmap from " + path.string());
    return true;
}

bool Terrain::saveSplatmap(const std::filesystem::path& path) const
{
    if (!m_initialized)
    {
        Logger::error("Terrain: cannot save splatmap — not initialized");
        return false;
    }

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        Logger::error("Terrain: failed to open " + path.string() + " for writing");
        return false;
    }

    file.write(reinterpret_cast<const char*>(m_splatData.data()),
               static_cast<std::streamsize>(m_splatData.size() * sizeof(glm::vec4)));

    Logger::info("Terrain: saved splatmap to " + path.string()
                 + " (" + std::to_string(m_splatData.size() * sizeof(glm::vec4)) + " bytes)");
    return true;
}

bool Terrain::loadSplatmap(const std::filesystem::path& path)
{
    if (!m_initialized)
    {
        Logger::error("Terrain: cannot load splatmap — not initialized");
        return false;
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        Logger::error("Terrain: failed to open " + path.string() + " for reading");
        return false;
    }

    auto fileSize = file.tellg();
    auto expectedSize = static_cast<std::streamoff>(m_splatData.size() * sizeof(glm::vec4));
    if (fileSize != expectedSize)
    {
        Logger::error("Terrain: splatmap file size mismatch: expected "
                     + std::to_string(expectedSize) + " bytes, got "
                     + std::to_string(fileSize));
        return false;
    }

    file.seekg(0);
    file.read(reinterpret_cast<char*>(m_splatData.data()),
              static_cast<std::streamsize>(m_splatData.size() * sizeof(glm::vec4)));

    updateSplatmapRegion(0, 0, m_config.width, m_config.depth);

    Logger::info("Terrain: loaded splatmap from " + path.string());
    return true;
}

} // namespace Vestige
