/// @file shadow_map.cpp
/// @brief Directional shadow map implementation.
#include "renderer/shadow_map.h"
#include "core/logger.h"

#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>

namespace Vestige
{

ShadowMap::ShadowMap(const ShadowConfig& config)
    : m_config(config)
{
    // Create a depth-only FBO for the shadow map
    FramebufferConfig fboConfig;
    fboConfig.width = config.resolution;
    fboConfig.height = config.resolution;
    fboConfig.samples = 1;
    fboConfig.hasColorAttachment = false;   // Depth only
    fboConfig.hasDepthAttachment = true;
    fboConfig.isDepthTexture = true;        // Must be sampleable in the lighting pass

    m_depthFbo = std::make_unique<Framebuffer>(fboConfig);

    Logger::info("Shadow map initialized: " + std::to_string(config.resolution) + "x"
        + std::to_string(config.resolution));
}

void ShadowMap::update(const DirectionalLight& light, const glm::vec3& sceneCenter)
{
    // Position the light "camera" behind the scene center, looking toward it
    glm::vec3 lightDir = glm::normalize(light.direction);
    glm::vec3 lightPos = sceneCenter - lightDir * (m_config.farPlane * 0.5f);

    // View matrix: look from the light position toward the scene center
    glm::mat4 lightView = glm::lookAt(lightPos, sceneCenter, glm::vec3(0.0f, 1.0f, 0.0f));

    // Orthographic projection: parallel rays (directional light has no perspective)
    glm::mat4 lightProjection = glm::ortho(
        -m_config.orthoSize, m_config.orthoSize,
        -m_config.orthoSize, m_config.orthoSize,
        m_config.nearPlane, m_config.farPlane);

    m_lightSpaceMatrix = lightProjection * lightView;
}

void ShadowMap::beginShadowPass()
{
    m_depthFbo->bind();
    glClear(GL_DEPTH_BUFFER_BIT);

    // Keep normal back-face culling during shadow pass.
    // Front faces (facing the light) write depth — combined with a small
    // angle-dependent bias in the fragment shader, this prevents both
    // shadow acne and peter panning (the lit gap at shadow caster bases).
}

void ShadowMap::endShadowPass()
{
    Framebuffer::unbind();
}

void ShadowMap::bindShadowTexture(int textureUnit)
{
    m_depthFbo->bindDepthTexture(textureUnit);
}

const glm::mat4& ShadowMap::getLightSpaceMatrix() const
{
    return m_lightSpaceMatrix;
}

ShadowConfig& ShadowMap::getConfig()
{
    return m_config;
}

} // namespace Vestige
