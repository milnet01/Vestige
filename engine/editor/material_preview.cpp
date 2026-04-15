// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file material_preview.cpp
/// @brief Material preview sphere rendering implementation.
#include "editor/material_preview.h"
#include "core/logger.h"
#include "renderer/material.h"
#include "renderer/texture.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Vestige
{

bool MaterialPreview::initialize(const std::string& assetPath, int resolution)
{
    if (m_initialized)
    {
        return true;
    }

    m_resolution = resolution;

    // Load preview shader
    std::string vertPath = assetPath + "/shaders/material_preview.vert.glsl";
    std::string fragPath = assetPath + "/shaders/material_preview.frag.glsl";
    if (!m_previewShader.loadFromFiles(vertPath, fragPath))
    {
        Logger::error("MaterialPreview: failed to load preview shaders");
        return false;
    }

    // Create the preview FBO
    FramebufferConfig config;
    config.width = resolution;
    config.height = resolution;
    config.samples = 1;
    config.hasColorAttachment = true;
    config.hasDepthAttachment = true;
    config.isFloatingPoint = false;
    config.isDepthTexture = false;
    m_fbo = std::make_unique<Framebuffer>(config);

    if (!m_fbo->isComplete())
    {
        Logger::error("MaterialPreview: FBO creation failed");
        return false;
    }

    // Create sphere mesh for preview
    m_sphere = Mesh::createSphere(32, 16);

    // Create a 1x1 white fallback texture for unused sampler slots
    m_defaultTexture = std::make_shared<Texture>();
    m_defaultTexture->createSolidColor(255, 255, 255);

    // Fixed camera: looking at origin from front-right-above
    m_viewMatrix = glm::lookAt(
        glm::vec3(0.0f, 0.3f, 2.2f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));

    m_projMatrix = glm::perspective(
        glm::radians(35.0f),
        1.0f,
        0.1f,
        10.0f);

    m_initialized = true;
    Logger::info("MaterialPreview initialized (" + std::to_string(resolution) + "x"
                 + std::to_string(resolution) + ")");
    return true;
}

void MaterialPreview::render(const Material& material)
{
    if (!m_initialized || !m_dirty)
    {
        return;
    }

    // Save current OpenGL state
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevFbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    // Bind preview FBO
    m_fbo->bind();
    glViewport(0, 0, m_resolution, m_resolution);
    glClearColor(0.15f, 0.15f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    // Use the preview shader
    m_previewShader.use();
    m_previewShader.setMat4("u_model", glm::mat4(1.0f));
    m_previewShader.setMat4("u_view", m_viewMatrix);
    m_previewShader.setMat4("u_projection", m_projMatrix);

    // Fixed light from upper-right
    glm::vec3 lightDir = glm::normalize(glm::vec3(-1.0f, -1.0f, -1.0f));
    m_previewShader.setVec3("u_lightDir", lightDir);
    m_previewShader.setVec3("u_viewPos", glm::vec3(0.0f, 0.3f, 2.2f));

    // Upload material properties
    bool isPBR = (material.getType() == MaterialType::PBR);
    m_previewShader.setBool("u_usePBR", isPBR);

    if (isPBR)
    {
        m_previewShader.setVec3("u_albedo", material.getAlbedo());
        m_previewShader.setFloat("u_metallic", material.getMetallic());
        m_previewShader.setFloat("u_roughness", material.getRoughness());
        m_previewShader.setFloat("u_ao", material.getAo());
    }
    else
    {
        m_previewShader.setVec3("u_diffuseColor", material.getDiffuseColor());
        m_previewShader.setVec3("u_specularColor", material.getSpecularColor());
        m_previewShader.setFloat("u_shininess", material.getShininess());
    }

    // Bind albedo/diffuse texture if available
    bool hasAlbedoTex = material.hasDiffuseTexture();
    m_previewShader.setBool("u_hasAlbedoTex", hasAlbedoTex);
    if (hasAlbedoTex)
    {
        material.getDiffuseTexture()->bind(0);
    }
    else
    {
        // Bind default white texture to satisfy Mesa AMD driver
        m_defaultTexture->bind(0);
    }
    m_previewShader.setInt("u_albedoTex", 0);

    // Draw the sphere
    m_sphere.bind();
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_sphere.getIndexCount()),
                   GL_UNSIGNED_INT, nullptr);
    m_sphere.unbind();

    // Restore previous state
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1],
               prevViewport[2], prevViewport[3]);

    m_dirty = false;
}

GLuint MaterialPreview::getTextureId() const
{
    if (!m_fbo)
    {
        return 0;
    }
    return m_fbo->getColorAttachmentId();
}

void MaterialPreview::cleanup()
{
    m_fbo.reset();
    m_defaultTexture.reset();
    m_initialized = false;
}

} // namespace Vestige
