/// @file environment_map.cpp
/// @brief IBL environment map generation implementation.
#include "renderer/environment_map.h"
#include "renderer/fullscreen_quad.h"
#include "core/logger.h"

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Vestige
{

// Skybox cube vertices (same as skybox.cpp — inward-facing for cubemap capture)
static const float CUBE_VERTICES[] = {
    -1.0f,  1.0f, -1.0f,  -1.0f, -1.0f, -1.0f,   1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,   1.0f,  1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,
    -1.0f, -1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,   1.0f, -1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f,  -1.0f,  1.0f, -1.0f,  -1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,
     1.0f,  1.0f, -1.0f,   1.0f,  1.0f,  1.0f,   1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,   1.0f, -1.0f, -1.0f,   1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f,  1.0f,   1.0f,  1.0f,  1.0f,   1.0f,  1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,  -1.0f,  1.0f,  1.0f,
    -1.0f, -1.0f, -1.0f,   1.0f, -1.0f, -1.0f,   1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,  -1.0f, -1.0f, -1.0f,
};

// View matrices for rendering to each cubemap face (from the origin)
static const glm::mat4 CAPTURE_VIEWS[6] = {
    glm::lookAt(glm::vec3(0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
    glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
    glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
    glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
    glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
    glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
};

static const glm::mat4 CAPTURE_PROJECTION =
    glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);

EnvironmentMap::EnvironmentMap()
{
    createCubeVAO();
}

EnvironmentMap::~EnvironmentMap()
{
    if (m_cubeVao != 0)  glDeleteVertexArrays(1, &m_cubeVao);
    if (m_cubeVbo != 0)  glDeleteBuffers(1, &m_cubeVbo);
    if (m_captureFbo != 0) glDeleteFramebuffers(1, &m_captureFbo);
    if (m_captureRbo != 0) glDeleteRenderbuffers(1, &m_captureRbo);
    if (m_envCubemap != 0) glDeleteTextures(1, &m_envCubemap);
    if (m_irradianceMap != 0) glDeleteTextures(1, &m_irradianceMap);
    if (m_prefilterMap != 0) glDeleteTextures(1, &m_prefilterMap);
    if (m_brdfLut != 0) glDeleteTextures(1, &m_brdfLut);
}

void EnvironmentMap::createCubeVAO()
{
    glGenVertexArrays(1, &m_cubeVao);
    glGenBuffers(1, &m_cubeVbo);

    glBindVertexArray(m_cubeVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_cubeVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(CUBE_VERTICES), CUBE_VERTICES, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

    glBindVertexArray(0);
}

void EnvironmentMap::renderCube() const
{
    glBindVertexArray(m_cubeVao);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
}

bool EnvironmentMap::initialize(const std::string& assetPath)
{
    std::string cubeVert = assetPath + "/shaders/cubemap_render.vert.glsl";
    std::string screenVert = assetPath + "/shaders/screen_quad.vert.glsl";

    // Capture shader: cubemap_render.vert + skybox.frag (for procedural gradient capture)
    if (!m_captureShader.loadFromFiles(cubeVert,
            assetPath + "/shaders/skybox.frag.glsl"))
    {
        Logger::error("Failed to load IBL capture shader");
        return false;
    }

    if (!m_irradianceShader.loadFromFiles(cubeVert,
            assetPath + "/shaders/irradiance_convolution.frag.glsl"))
    {
        Logger::error("Failed to load IBL irradiance shader");
        return false;
    }

    if (!m_prefilterShader.loadFromFiles(cubeVert,
            assetPath + "/shaders/prefilter.frag.glsl"))
    {
        Logger::error("Failed to load IBL prefilter shader");
        return false;
    }

    if (!m_brdfLutShader.loadFromFiles(screenVert,
            assetPath + "/shaders/brdf_lut.frag.glsl"))
    {
        Logger::error("Failed to load IBL BRDF LUT shader");
        return false;
    }

    // Create capture FBO with depth renderbuffer
    glGenFramebuffers(1, &m_captureFbo);
    glGenRenderbuffers(1, &m_captureRbo);

    glBindFramebuffer(GL_FRAMEBUFFER, m_captureFbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_captureRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                          ENV_RESOLUTION, ENV_RESOLUTION);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, m_captureRbo);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    Logger::info("IBL shaders loaded successfully");
    return true;
}

void EnvironmentMap::generate(GLuint skyboxCubemap, bool hasCubemap,
                               const FullscreenQuad& quad, const Shader& /*skyboxShader*/)
{
    // Step 1: Capture the environment to an HDR cubemap
    captureEnvironment(skyboxCubemap, hasCubemap, m_captureShader);

    // Step 2: Convolve environment into irradiance cubemap
    generateIrradiance();

    // Step 3: Prefilter environment for specular reflections
    generatePrefilter();

    // Step 4: Generate BRDF integration LUT
    generateBrdfLut(quad);

    m_ready = true;
    Logger::info("IBL environment maps generated (irradiance "
        + std::to_string(IRRADIANCE_RESOLUTION) + "x"
        + std::to_string(IRRADIANCE_RESOLUTION) + ", prefilter "
        + std::to_string(PREFILTER_RESOLUTION) + "x"
        + std::to_string(PREFILTER_RESOLUTION) + " with "
        + std::to_string(MAX_MIP_LEVELS) + " mips, BRDF LUT "
        + std::to_string(BRDF_LUT_RESOLUTION) + "x"
        + std::to_string(BRDF_LUT_RESOLUTION) + ")");
}

void EnvironmentMap::captureEnvironment(GLuint skyboxCubemap, bool hasCubemap,
                                         const Shader& captureShader)
{
    // Create HDR environment cubemap
    glGenTextures(1, &m_envCubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_envCubemap);
    for (int i = 0; i < 6; i++)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
                     ENV_RESOLUTION, ENV_RESOLUTION, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    // Render each face of the cubemap
    captureShader.use();
    captureShader.setMat4("u_projection", CAPTURE_PROJECTION);
    captureShader.setBool("u_hasCubemap", hasCubemap);

    if (hasCubemap)
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxCubemap);
        captureShader.setInt("u_skyboxTexture", 0);
    }

    // Save current viewport
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    glBindFramebuffer(GL_FRAMEBUFFER, m_captureFbo);
    glViewport(0, 0, ENV_RESOLUTION, ENV_RESOLUTION);
    glDisable(GL_CULL_FACE);

    for (int face = 0; face < 6; face++)
    {
        captureShader.setMat4("u_view", CAPTURE_VIEWS[face]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                               m_envCubemap, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderCube();
    }

    glEnable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Generate mipmaps for the environment map (used by prefilter shader for anti-aliasing)
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_envCubemap);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    // Restore viewport
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

    Logger::debug("Environment cubemap captured ("
        + std::to_string(ENV_RESOLUTION) + "x" + std::to_string(ENV_RESOLUTION) + ")");
}

void EnvironmentMap::generateIrradiance()
{
    // Create irradiance cubemap (low-res, holds diffuse irradiance)
    glGenTextures(1, &m_irradianceMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_irradianceMap);
    for (int i = 0; i < 6; i++)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
                     IRRADIANCE_RESOLUTION, IRRADIANCE_RESOLUTION,
                     0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    // Convolve the environment cubemap into the irradiance map
    m_irradianceShader.use();
    m_irradianceShader.setMat4("u_projection", CAPTURE_PROJECTION);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_envCubemap);
    m_irradianceShader.setInt("u_environmentMap", 0);

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    glBindFramebuffer(GL_FRAMEBUFFER, m_captureFbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_captureRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                          IRRADIANCE_RESOLUTION, IRRADIANCE_RESOLUTION);
    glViewport(0, 0, IRRADIANCE_RESOLUTION, IRRADIANCE_RESOLUTION);
    glDisable(GL_CULL_FACE);

    for (int face = 0; face < 6; face++)
    {
        m_irradianceShader.setMat4("u_view", CAPTURE_VIEWS[face]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                               m_irradianceMap, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderCube();
    }

    glEnable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

    Logger::debug("Irradiance map generated ("
        + std::to_string(IRRADIANCE_RESOLUTION) + "x"
        + std::to_string(IRRADIANCE_RESOLUTION) + ")");
}

void EnvironmentMap::generatePrefilter()
{
    // Create prefiltered cubemap with mip chain
    glGenTextures(1, &m_prefilterMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_prefilterMap);
    for (int i = 0; i < 6; i++)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
                     PREFILTER_RESOLUTION, PREFILTER_RESOLUTION,
                     0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    // Prefilter at each roughness mip level
    m_prefilterShader.use();
    m_prefilterShader.setMat4("u_projection", CAPTURE_PROJECTION);
    m_prefilterShader.setFloat("u_envResolution", static_cast<float>(ENV_RESOLUTION));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_envCubemap);
    m_prefilterShader.setInt("u_environmentMap", 0);

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    glBindFramebuffer(GL_FRAMEBUFFER, m_captureFbo);
    glDisable(GL_CULL_FACE);

    for (int mip = 0; mip < MAX_MIP_LEVELS; mip++)
    {
        int mipWidth = PREFILTER_RESOLUTION >> mip;
        int mipHeight = PREFILTER_RESOLUTION >> mip;
        if (mipWidth < 1) mipWidth = 1;
        if (mipHeight < 1) mipHeight = 1;

        glBindRenderbuffer(GL_RENDERBUFFER, m_captureRbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                              mipWidth, mipHeight);
        glViewport(0, 0, mipWidth, mipHeight);

        float roughness = static_cast<float>(mip)
                        / static_cast<float>(MAX_MIP_LEVELS - 1);
        m_prefilterShader.setFloat("u_roughness", roughness);

        for (int face = 0; face < 6; face++)
        {
            m_prefilterShader.setMat4("u_view", CAPTURE_VIEWS[face]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                                   m_prefilterMap, mip);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            renderCube();
        }
    }

    glEnable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

    Logger::debug("Prefiltered environment map generated ("
        + std::to_string(PREFILTER_RESOLUTION) + "x"
        + std::to_string(PREFILTER_RESOLUTION) + ", "
        + std::to_string(MAX_MIP_LEVELS) + " mip levels)");
}

void EnvironmentMap::generateBrdfLut(const FullscreenQuad& quad)
{
    // Create 2D BRDF integration LUT
    glGenTextures(1, &m_brdfLut);
    glBindTexture(GL_TEXTURE_2D, m_brdfLut);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F,
                 BRDF_LUT_RESOLUTION, BRDF_LUT_RESOLUTION,
                 0, GL_RG, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    glBindFramebuffer(GL_FRAMEBUFFER, m_captureFbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_captureRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                          BRDF_LUT_RESOLUTION, BRDF_LUT_RESOLUTION);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, m_brdfLut, 0);
    glViewport(0, 0, BRDF_LUT_RESOLUTION, BRDF_LUT_RESOLUTION);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_brdfLutShader.use();
    quad.draw();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

    Logger::debug("BRDF LUT generated ("
        + std::to_string(BRDF_LUT_RESOLUTION) + "x"
        + std::to_string(BRDF_LUT_RESOLUTION) + ")");
}

void EnvironmentMap::bindIrradiance(int unit) const
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_irradianceMap);
}

void EnvironmentMap::bindPrefilter(int unit) const
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_prefilterMap);
}

void EnvironmentMap::bindBrdfLut(int unit) const
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_brdfLut);
}

bool EnvironmentMap::isReady() const
{
    return m_ready;
}

GLuint EnvironmentMap::getEnvironmentCubemap() const
{
    return m_envCubemap;
}

} // namespace Vestige
