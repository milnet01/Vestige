/// @file point_shadow_map.cpp
/// @brief Point light shadow map implementation (depth cubemap).
#include "renderer/point_shadow_map.h"
#include "core/logger.h"

#include <glm/gtc/matrix_transform.hpp>

#include <string>

namespace Vestige
{

PointShadowMap::PointShadowMap(const PointShadowConfig& config)
    : m_config(config)
{
    int res = config.resolution;

    // Create depth cubemap texture
    glGenTextures(1, &m_depthCubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_depthCubemap);

    for (int i = 0; i < 6; i++)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_DEPTH_COMPONENT,
            res, res, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    // Create FBO
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    // Attach face 0 initially to validate completeness
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_CUBE_MAP_POSITIVE_X, m_depthCubemap, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        Logger::error("Point shadow map FBO incomplete: 0x"
            + std::to_string(static_cast<unsigned int>(status)));
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Pre-compute the projection matrix (90 degree FOV for cube faces)
    m_shadowProjection = glm::perspective(
        glm::radians(90.0f), 1.0f, config.nearPlane, config.farPlane);

    Logger::info("Point shadow map initialized: "
        + std::to_string(res) + "x" + std::to_string(res) + " per face");
}

PointShadowMap::~PointShadowMap()
{
    if (m_fbo != 0)
    {
        glDeleteFramebuffers(1, &m_fbo);
    }
    if (m_depthCubemap != 0)
    {
        glDeleteTextures(1, &m_depthCubemap);
    }
}

void PointShadowMap::update(const glm::vec3& lightPos)
{
    // 6 view matrices: +X, -X, +Y, -Y, +Z, -Z
    m_lightSpaceMatrices[0] = m_shadowProjection * glm::lookAt(lightPos,
        lightPos + glm::vec3( 1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    m_lightSpaceMatrices[1] = m_shadowProjection * glm::lookAt(lightPos,
        lightPos + glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    m_lightSpaceMatrices[2] = m_shadowProjection * glm::lookAt(lightPos,
        lightPos + glm::vec3( 0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    m_lightSpaceMatrices[3] = m_shadowProjection * glm::lookAt(lightPos,
        lightPos + glm::vec3( 0.0f,-1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    m_lightSpaceMatrices[4] = m_shadowProjection * glm::lookAt(lightPos,
        lightPos + glm::vec3( 0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    m_lightSpaceMatrices[5] = m_shadowProjection * glm::lookAt(lightPos,
        lightPos + glm::vec3( 0.0f, 0.0f,-1.0f), glm::vec3(0.0f, -1.0f, 0.0f));
}

void PointShadowMap::beginFace(int face)
{
    if (face < 0 || face >= 6)
    {
        Logger::error("PointShadowMap::beginFace — face index out of range: "
            + std::to_string(face));
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, m_depthCubemap, 0);
    glViewport(0, 0, m_config.resolution, m_config.resolution);
    glClear(GL_DEPTH_BUFFER_BIT);
}

void PointShadowMap::endFace()
{
    // FBO stays bound between faces — caller unbinds after all faces are done
}

void PointShadowMap::bindShadowTexture(int textureUnit) const
{
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(textureUnit));
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_depthCubemap);
}

const glm::mat4& PointShadowMap::getLightSpaceMatrix(int face) const
{
    if (face < 0 || face >= 6)
    {
        Logger::error("PointShadowMap::getLightSpaceMatrix — face index out of range: "
            + std::to_string(face));
        static const glm::mat4 identity(1.0f);
        return identity;
    }
    return m_lightSpaceMatrices[face];
}

const PointShadowConfig& PointShadowMap::getConfig() const
{
    return m_config;
}

} // namespace Vestige
