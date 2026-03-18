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

    // Create depth texture array (one layer per cascade)
    glGenTextures(1, &m_depthTextureArray);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_depthTextureArray);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24,
                 res, res, layers, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, borderColor);

    // Create FBO (layers are attached one at a time during rendering)
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              m_depthTextureArray, 0, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        Logger::error("Cascaded shadow map FBO incomplete: " + std::to_string(status));
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

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

    // Find the AABB of the frustum corners in light-view space
    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::lowest();

    for (const auto& corner : frustumCorners)
    {
        glm::vec4 lsCorner = lightView * glm::vec4(corner, 1.0f);
        minX = std::min(minX, lsCorner.x);
        maxX = std::max(maxX, lsCorner.x);
        minY = std::min(minY, lsCorner.y);
        maxY = std::max(maxY, lsCorner.y);
        minZ = std::min(minZ, lsCorner.z);
        maxZ = std::max(maxZ, lsCorner.z);
    }

    // Extend the Z range to capture shadow casters behind the frustum
    float zRange = maxZ - minZ;
    minZ -= zRange * 0.5f;
    maxZ += zRange * 0.5f;

    // Snap ortho bounds to texel grid to prevent shadow swimming
    float worldUnitsPerTexelX = (maxX - minX) / static_cast<float>(resolution);
    float worldUnitsPerTexelY = (maxY - minY) / static_cast<float>(resolution);

    if (worldUnitsPerTexelX > 0.0f)
    {
        minX = std::floor(minX / worldUnitsPerTexelX) * worldUnitsPerTexelX;
        maxX = std::floor(maxX / worldUnitsPerTexelX) * worldUnitsPerTexelX;
    }
    if (worldUnitsPerTexelY > 0.0f)
    {
        minY = std::floor(minY / worldUnitsPerTexelY) * worldUnitsPerTexelY;
        maxY = std::floor(maxY / worldUnitsPerTexelY) * worldUnitsPerTexelY;
    }

    glm::mat4 lightProjection = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);

    return lightProjection * lightView;
}

void CascadedShadowMap::update(const DirectionalLight& light, const Camera& camera, float aspectRatio)
{
    float cameraNear = 0.1f;
    float shadowFar = m_config.shadowDistance;

    // Compute cascade split distances in-place (no heap allocation)
    for (int i = 0; i < m_config.cascadeCount; i++)
    {
        float p = static_cast<float>(i + 1) / static_cast<float>(m_config.cascadeCount);
        float linear = cameraNear + (shadowFar - cameraNear) * p;
        float logarithmic = cameraNear * std::pow(shadowFar / cameraNear, p);
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
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
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
    glActiveTexture(GL_TEXTURE0 + textureUnit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_depthTextureArray);
}

const glm::mat4& CascadedShadowMap::getLightSpaceMatrix(int cascade) const
{
    return m_lightSpaceMatrices[static_cast<size_t>(cascade)];
}

float CascadedShadowMap::getCascadeSplit(int cascade) const
{
    return m_cascadeSplits[static_cast<size_t>(cascade)];
}

int CascadedShadowMap::getCascadeCount() const
{
    return m_config.cascadeCount;
}

float CascadedShadowMap::getTexelWorldSize(int cascade) const
{
    return m_texelWorldSizes[static_cast<size_t>(cascade)];
}

const CascadedShadowConfig& CascadedShadowMap::getConfig() const
{
    return m_config;
}

} // namespace Vestige
