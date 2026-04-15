// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cascaded_shadow_map.cpp
/// @brief Cascaded shadow map implementation.
#include "renderer/cascaded_shadow_map.h"
#include "core/logger.h"

#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Vestige
{

CascadedShadowMap::CascadedShadowMap(const CascadedShadowConfig& config)
    : m_config(config)
    , m_lightSpaceMatrices(static_cast<size_t>(config.cascadeCount), glm::mat4(1.0f))
    , m_cascadeSplits(static_cast<size_t>(config.cascadeCount), 0.0f)
    , m_texelWorldSizes(static_cast<size_t>(config.cascadeCount), 0.0f)
{
    int res = config.resolution;
    int layers = config.cascadeCount;

    // Create depth texture array (one layer per cascade) — DSA
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &m_depthTextureArray);
    glTextureStorage3D(m_depthTextureArray, 1, GL_DEPTH_COMPONENT24, res, res, layers);
    glTextureParameteri(m_depthTextureArray, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_depthTextureArray, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(m_depthTextureArray, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTextureParameteri(m_depthTextureArray, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTextureParameterfv(m_depthTextureArray, GL_TEXTURE_BORDER_COLOR, borderColor);

    // Create FBO (layers are attached one at a time during rendering) — DSA
    glCreateFramebuffers(1, &m_fbo);
    glNamedFramebufferTextureLayer(m_fbo, GL_DEPTH_ATTACHMENT,
                                   m_depthTextureArray, 0, 0);
    glNamedFramebufferDrawBuffer(m_fbo, GL_NONE);
    glNamedFramebufferReadBuffer(m_fbo, GL_NONE);

    GLenum status = glCheckNamedFramebufferStatus(m_fbo, GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        Logger::error("Cascaded shadow map FBO incomplete: " + std::to_string(status));
    }

    Logger::info("Cascaded shadow map initialized: " + std::to_string(layers)
        + " cascades at " + std::to_string(res) + "x" + std::to_string(res));
}

CascadedShadowMap::~CascadedShadowMap()
{
    if (m_depthTextureArray != 0)
    {
        glDeleteTextures(1, &m_depthTextureArray);
    }
    if (m_fbo != 0)
    {
        glDeleteFramebuffers(1, &m_fbo);
    }
}

CascadedShadowMap::CascadedShadowMap(CascadedShadowMap&& other) noexcept
    : m_config(other.m_config)
    , m_fbo(other.m_fbo)
    , m_depthTextureArray(other.m_depthTextureArray)
    , m_lightSpaceMatrices(std::move(other.m_lightSpaceMatrices))
    , m_cascadeSplits(std::move(other.m_cascadeSplits))
    , m_texelWorldSizes(std::move(other.m_texelWorldSizes))
    , m_hasDepthBounds(other.m_hasDepthBounds)
    , m_depthBoundsNear(other.m_depthBoundsNear)
    , m_depthBoundsFar(other.m_depthBoundsFar)
{
    other.m_fbo = 0;
    other.m_depthTextureArray = 0;
    other.m_hasDepthBounds = false;
    other.m_depthBoundsNear = 0.0f;
    other.m_depthBoundsFar = 0.0f;
}

CascadedShadowMap& CascadedShadowMap::operator=(CascadedShadowMap&& other) noexcept
{
    if (this != &other)
    {
        // Destroy own GPU resources
        if (m_depthTextureArray != 0)
        {
            glDeleteTextures(1, &m_depthTextureArray);
        }
        if (m_fbo != 0)
        {
            glDeleteFramebuffers(1, &m_fbo);
        }

        // Transfer all state
        m_config = other.m_config;
        m_fbo = other.m_fbo;
        m_depthTextureArray = other.m_depthTextureArray;
        m_lightSpaceMatrices = std::move(other.m_lightSpaceMatrices);
        m_cascadeSplits = std::move(other.m_cascadeSplits);
        m_texelWorldSizes = std::move(other.m_texelWorldSizes);
        m_hasDepthBounds = other.m_hasDepthBounds;
        m_depthBoundsNear = other.m_depthBoundsNear;
        m_depthBoundsFar = other.m_depthBoundsFar;

        // Zero the source
        other.m_fbo = 0;
        other.m_depthTextureArray = 0;
        other.m_hasDepthBounds = false;
        other.m_depthBoundsNear = 0.0f;
        other.m_depthBoundsFar = 0.0f;
    }
    return *this;
}

std::vector<float> CascadedShadowMap::computeSplitDistances(
    float nearPlane, float farPlane, int cascadeCount, float lambda)
{
    std::vector<float> splits(static_cast<size_t>(cascadeCount));
    for (int i = 0; i < cascadeCount; i++)
    {
        float p = static_cast<float>(i + 1) / static_cast<float>(cascadeCount);
        float linear = nearPlane + (farPlane - nearPlane) * p;
        float logarithmic = nearPlane * std::pow(farPlane / nearPlane, p);
        splits[static_cast<size_t>(i)] = lambda * logarithmic + (1.0f - lambda) * linear;
    }
    return splits;
}

std::array<glm::vec3, 8> CascadedShadowMap::computeFrustumCorners(
    const glm::mat4& viewProjection)
{
    glm::mat4 inv = glm::inverse(viewProjection);
    std::array<glm::vec3, 8> corners;
    int idx = 0;

    for (int x = 0; x < 2; x++)
    {
        for (int y = 0; y < 2; y++)
        {
            for (int z = 0; z < 2; z++)
            {
                glm::vec4 pt = inv * glm::vec4(
                    2.0f * static_cast<float>(x) - 1.0f,
                    2.0f * static_cast<float>(y) - 1.0f,
                    2.0f * static_cast<float>(z) - 1.0f,
                    1.0f);
                corners[static_cast<size_t>(idx++)] = glm::vec3(pt) / pt.w;
            }
        }
    }

    return corners;
}

glm::mat4 CascadedShadowMap::computeCascadeMatrix(
    const DirectionalLight& light,
    const std::array<glm::vec3, 8>& frustumCorners,
    int resolution)
{
    // Compute the center of the frustum sub-volume
    glm::vec3 center(0.0f);
    for (const auto& corner : frustumCorners)
    {
        center += corner;
    }
    center /= static_cast<float>(frustumCorners.size());

    // Light view matrix: look from behind the frustum center toward it
    glm::vec3 lightDir = glm::normalize(light.direction);

    // Handle near-vertical light direction (avoid gimbal lock with Y-up)
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(lightDir, up)) > 0.99f)
    {
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    glm::mat4 lightView = glm::lookAt(center - lightDir, center, up);

    // Bounding sphere cascade fitting (industry standard — UE4/5, Unity, Bevy).
    // Instead of a tight AABB of the sub-frustum in light-space (which degenerates
    // to a narrow strip when looking steeply down), use the bounding sphere of the
    // sub-frustum corners. A sphere is rotation-invariant, so the orthographic
    // projection never shrinks regardless of camera angle. Costs ~30-50% wasted
    // shadow texels compared to tight AABB, but eliminates edge clipping entirely.
    float radius = 0.0f;
    for (const auto& corner : frustumCorners)
    {
        float dist = glm::length(corner - center);
        radius = std::max(radius, dist);
    }
    // Round radius up to texel grid for stable shadow edges
    float worldUnitsPerTexel = (radius * 2.0f) / static_cast<float>(resolution);
    if (worldUnitsPerTexel > 0.0f)
    {
        radius = std::ceil(radius / worldUnitsPerTexel) * worldUnitsPerTexel;
    }

    float minX = -radius;
    float maxX =  radius;
    float minY = -radius;
    float maxY =  radius;

    // Find Z range in light-view space (still need tight Z for depth precision)
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::lowest();
    for (const auto& corner : frustumCorners)
    {
        glm::vec4 lsCorner = lightView * glm::vec4(corner, 1.0f);
        minZ = std::min(minZ, lsCorner.z);
        maxZ = std::max(maxZ, lsCorner.z);
    }

    // Extend the Z range to capture shadow casters behind the frustum
    float zRange = maxZ - minZ;
    minZ -= zRange * 0.5f;
    maxZ += zRange * 0.5f;

    glm::mat4 lightProjection = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);
    glm::mat4 shadowMatrix = lightProjection * lightView;

    // Snap the shadow matrix origin to the texel grid. Without this, sub-texel
    // camera movement causes the entire shadow map to shift by fractional texels,
    // producing visible edge swimming/shimmering.
    glm::vec4 shadowOrigin = shadowMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    float halfRes = static_cast<float>(resolution) * 0.5f;
    shadowOrigin.x *= halfRes;
    shadowOrigin.y *= halfRes;
    glm::vec4 rounded(std::round(shadowOrigin.x), std::round(shadowOrigin.y),
                      shadowOrigin.z, shadowOrigin.w);
    glm::vec4 offset = rounded - shadowOrigin;
    offset.x /= halfRes;
    offset.y /= halfRes;

    shadowMatrix[3][0] += offset.x;
    shadowMatrix[3][1] += offset.y;

    return shadowMatrix;
}

void CascadedShadowMap::setDepthBounds(float near, float far)
{
    m_hasDepthBounds = true;
    m_depthBoundsNear = near;
    m_depthBoundsFar = far;
}

void CascadedShadowMap::clearDepthBounds()
{
    m_hasDepthBounds = false;
    m_depthBoundsNear = 0.0f;
    m_depthBoundsFar = 0.0f;
}

bool CascadedShadowMap::hasDepthBounds() const
{
    return m_hasDepthBounds;
}

void CascadedShadowMap::update(const DirectionalLight& light, const Camera& camera, float aspectRatio)
{
    float cameraNear = 0.1f;
    float shadowFar = m_config.shadowDistance;

    // SDSM: tighten the cascade range to where geometry actually exists.
    // This distributes cascade resolution more effectively.
    float effectiveNear = cameraNear;
    float effectiveFar = shadowFar;
    if (m_hasDepthBounds)
    {
        // Clamp to camera near (never closer) and shadow distance (never farther).
        // Add small margins to avoid popping at exact geometry boundaries.
        effectiveNear = std::max(cameraNear, m_depthBoundsNear * 0.9f);
        effectiveFar = std::min(shadowFar, m_depthBoundsFar * 1.1f);

        // Ensure valid range
        if (effectiveNear >= effectiveFar)
        {
            effectiveNear = cameraNear;
            effectiveFar = shadowFar;
        }
    }

    // Compute cascade split distances in-place (no heap allocation)
    for (int i = 0; i < m_config.cascadeCount; i++)
    {
        float p = static_cast<float>(i + 1) / static_cast<float>(m_config.cascadeCount);
        float linear = effectiveNear + (effectiveFar - effectiveNear) * p;
        float logarithmic = effectiveNear * std::pow(effectiveFar / effectiveNear, p);
        m_cascadeSplits[static_cast<size_t>(i)] =
            m_config.splitLambda * logarithmic + (1.0f - m_config.splitLambda) * linear;
    }

    glm::mat4 view = camera.getViewMatrix();

    // Compute a tight light-space matrix for each cascade
    for (int i = 0; i < m_config.cascadeCount; i++)
    {
        float near = (i == 0) ? cameraNear : m_cascadeSplits[static_cast<size_t>(i - 1)];
        float far = m_cascadeSplits[static_cast<size_t>(i)];

        // Build a sub-frustum projection for this cascade's depth range
        glm::mat4 subProjection = glm::perspective(
            glm::radians(camera.getFov()), aspectRatio, near, far);

        // Get the 8 world-space corners of this sub-frustum (stack array, no heap)
        auto corners = computeFrustumCorners(subProjection * view);

        // Compute a tight orthographic light-space matrix
        m_lightSpaceMatrices[static_cast<size_t>(i)] =
            computeCascadeMatrix(light, corners, m_config.resolution);

        // Compute world-space texel size for this cascade (for normal offset bias).
        // The ortho projection maps a world-space extent to the shadow map resolution.
        // We can extract this from the light-space matrix: element [0][0] = 2/width.
        float orthoWidth = 2.0f / std::abs(m_lightSpaceMatrices[static_cast<size_t>(i)][0][0]);
        m_texelWorldSizes[static_cast<size_t>(i)] =
            orthoWidth / static_cast<float>(m_config.resolution);
    }
}

void CascadedShadowMap::beginCascade(int cascade)
{
    if (cascade < 0 || cascade >= m_config.cascadeCount)
    {
        Logger::error("CascadedShadowMap::beginCascade — cascade index out of range: "
            + std::to_string(cascade) + " (max: " + std::to_string(m_config.cascadeCount - 1) + ")");
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glNamedFramebufferTextureLayer(m_fbo, GL_DEPTH_ATTACHMENT,
                                   m_depthTextureArray, 0, cascade);
    glViewport(0, 0, m_config.resolution, m_config.resolution);
    glClear(GL_DEPTH_BUFFER_BIT);
}

void CascadedShadowMap::endCascade()
{
    // FBO stays bound between cascades — caller unbinds after all cascades are done
}

void CascadedShadowMap::bindShadowTexture(int textureUnit)
{
    glBindTextureUnit(static_cast<GLuint>(textureUnit), m_depthTextureArray);
}

const glm::mat4& CascadedShadowMap::getLightSpaceMatrix(int cascade) const
{
    if (cascade < 0 || cascade >= m_config.cascadeCount)
    {
        Logger::error("CascadedShadowMap::getLightSpaceMatrix — cascade index out of range: "
            + std::to_string(cascade));
        static const glm::mat4 identity(1.0f);
        return identity;
    }
    return m_lightSpaceMatrices[static_cast<size_t>(cascade)];
}

float CascadedShadowMap::getCascadeSplit(int cascade) const
{
    if (cascade < 0 || cascade >= m_config.cascadeCount)
    {
        Logger::error("CascadedShadowMap::getCascadeSplit — cascade index out of range: "
            + std::to_string(cascade));
        return 0.0f;
    }
    return m_cascadeSplits[static_cast<size_t>(cascade)];
}

int CascadedShadowMap::getCascadeCount() const
{
    return m_config.cascadeCount;
}

float CascadedShadowMap::getTexelWorldSize(int cascade) const
{
    if (cascade < 0 || cascade >= m_config.cascadeCount)
    {
        Logger::error("CascadedShadowMap::getTexelWorldSize — cascade index out of range: "
            + std::to_string(cascade));
        return 0.0f;
    }
    return m_texelWorldSizes[static_cast<size_t>(cascade)];
}

const CascadedShadowConfig& CascadedShadowMap::getConfig() const
{
    return m_config;
}

} // namespace Vestige
