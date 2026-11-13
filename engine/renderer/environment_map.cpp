// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file environment_map.cpp
/// @brief IBL environment map generation implementation.
#include "renderer/environment_map.h"
#include "renderer/framebuffer.h"
#include "renderer/fullscreen_quad.h"
#include "renderer/ibl_capture_sequence.h"
#include "renderer/ibl_prefilter.h"
#include "renderer/skybox.h"
#include "core/logger.h"

#include <glad/gl.h>

#include <cmath>

namespace Vestige
{

EnvironmentMap::EnvironmentMap()
{
    createIblCubeMesh(m_cubeVao, m_cubeVbo);
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

    // Create capture FBO with depth renderbuffer (DSA)
    glCreateFramebuffers(1, &m_captureFbo);
    glCreateRenderbuffers(1, &m_captureRbo);

    glNamedRenderbufferStorage(m_captureRbo, GL_DEPTH_COMPONENT24,
                               ENV_RESOLUTION, ENV_RESOLUTION);
    glNamedFramebufferRenderbuffer(m_captureFbo, GL_DEPTH_ATTACHMENT,
                                   GL_RENDERBUFFER, m_captureRbo);

    GLenum status = glCheckNamedFramebufferStatus(m_captureFbo, GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        Logger::error("IBL capture FBO incomplete — status: "
            + framebufferStatusText(status));
        return false;
    }

    Logger::info("IBL shaders loaded successfully");
    return true;
}

void EnvironmentMap::generate(GLuint skyboxCubemap, bool hasCubemap,
                               const FullscreenQuad& quad, const Shader& /*skyboxShader*/)
{
    // Delete old textures if regenerating (prevents GPU memory leaks)
    if (m_envCubemap != 0)   { glDeleteTextures(1, &m_envCubemap);   m_envCubemap = 0; }
    if (m_irradianceMap != 0){ glDeleteTextures(1, &m_irradianceMap); m_irradianceMap = 0; }
    if (m_prefilterMap != 0) { glDeleteTextures(1, &m_prefilterMap);  m_prefilterMap = 0; }
    if (m_brdfLut != 0)      { glDeleteTextures(1, &m_brdfLut);       m_brdfLut = 0; }

    // The four IBL passes need standard NDC depth (`GL_NEGATIVE_ONE_TO_ONE`
    // + `GL_LESS` + clearDepth 1.0); the engine's scene draw runs reverse-Z.
    // `runIblCaptureSequence` wraps a `ScopedForwardZ` around the whole
    // sequence so the cubemap captures see forward-Z and the caller's
    // reverse-Z state is restored before the next scene draw. (Phase 10.9
    // Slice 4 R1.)
    // A pass whose framebuffer is incomplete draws nothing; the maps are then
    // not ready, so the scene skips IBL rather than lighting from an empty
    // texture (3D_E-0874).
    bool ok = true;
    runIblCaptureSequence({
        [&]() {
            ok = captureEnvironment(skyboxCubemap, hasCubemap, m_captureShader) && ok;
            GLenum err = glGetError();
            if (err != GL_NO_ERROR)
                Logger::error("GL error after captureEnvironment: " + std::to_string(err));
        },
        [&]() {
            ok = ok && generateIrradiance();
            GLenum err = glGetError();
            if (err != GL_NO_ERROR)
                Logger::error("GL error after generateIrradiance: " + std::to_string(err));
        },
        [&]() {
            generatePrefilter();
            GLenum err = glGetError();
            if (err != GL_NO_ERROR)
                Logger::error("GL error after generatePrefilter: " + std::to_string(err));
        },
        [&]() {
            generateBrdfLut(quad);
            GLenum err = glGetError();
            if (err != GL_NO_ERROR)
                Logger::error("GL error after generateBrdfLut: " + std::to_string(err));
        },
    });

    m_ready = ok;
    if (!ok)
    {
        Logger::error("IBL environment maps not generated: a capture pass failed");
        return;
    }
    Logger::info("IBL environment maps generated (irradiance "
        + std::to_string(IRRADIANCE_RESOLUTION) + "x"
        + std::to_string(IRRADIANCE_RESOLUTION) + ", prefilter "
        + std::to_string(PREFILTER_RESOLUTION) + "x"
        + std::to_string(PREFILTER_RESOLUTION) + " with "
        + std::to_string(MAX_MIP_LEVELS) + " mips, BRDF LUT "
        + std::to_string(BRDF_LUT_RESOLUTION) + "x"
        + std::to_string(BRDF_LUT_RESOLUTION) + ")");
}

bool EnvironmentMap::captureEnvironment(GLuint skyboxCubemap, bool hasCubemap,
                                         const Shader& captureShader)
{
    // Create HDR environment cubemap (DSA)
    int mipLevels = static_cast<int>(std::floor(std::log2(ENV_RESOLUTION))) + 1;
    glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &m_envCubemap);
    glTextureStorage2D(m_envCubemap, mipLevels, GL_RGB16F,
                       ENV_RESOLUTION, ENV_RESOLUTION);
    glTextureParameteri(m_envCubemap, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(m_envCubemap, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_envCubemap, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_envCubemap, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_envCubemap, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    // Render each face of the cubemap
    captureShader.use();
    captureShader.setMat4("u_projection", iblCaptureProjection());
    captureShader.setBool("u_hasCubemap", hasCubemap);
    captureShader.setFloat("u_maxRadiance", Skybox::IBL_MAX_RADIANCE);

    if (hasCubemap)
    {
        glBindTextureUnit(0, skyboxCubemap);
        captureShader.setInt("u_skyboxTexture", 0);
    }

    IblFacePassParams pass;
    pass.shader = &captureShader;
    pass.captureFbo = m_captureFbo;
    pass.captureRbo = m_captureRbo;
    pass.target = m_envCubemap;
    pass.resolution = ENV_RESOLUTION;
    pass.drawCube = [this]() { drawIblCube(m_cubeVao); };
    if (!runIblFacePass(pass))
    {
        return false;
    }

    // Generate mipmaps for the environment map (used by prefilter shader for anti-aliasing)
    glGenerateTextureMipmap(m_envCubemap);

    Logger::debug("Environment cubemap captured ("
        + std::to_string(ENV_RESOLUTION) + "x" + std::to_string(ENV_RESOLUTION) + ")");
    return true;
}

bool EnvironmentMap::generateIrradiance()
{
    // Create irradiance cubemap (low-res, holds diffuse irradiance) (DSA)
    glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &m_irradianceMap);
    glTextureStorage2D(m_irradianceMap, 1, GL_RGB16F,
                       IRRADIANCE_RESOLUTION, IRRADIANCE_RESOLUTION);
    glTextureParameteri(m_irradianceMap, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_irradianceMap, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_irradianceMap, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_irradianceMap, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_irradianceMap, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    // Convolve the environment cubemap into the irradiance map
    m_irradianceShader.use();
    m_irradianceShader.setMat4("u_projection", iblCaptureProjection());

    glBindTextureUnit(0, m_envCubemap);
    m_irradianceShader.setInt("u_environmentMap", 0);

    IblFacePassParams pass;
    pass.shader = &m_irradianceShader;
    pass.captureFbo = m_captureFbo;
    pass.captureRbo = m_captureRbo;
    pass.target = m_irradianceMap;
    pass.resolution = IRRADIANCE_RESOLUTION;
    pass.drawCube = [this]() { drawIblCube(m_cubeVao); };
    if (!runIblFacePass(pass))
    {
        return false;
    }

    Logger::debug("Irradiance map generated ("
        + std::to_string(IRRADIANCE_RESOLUTION) + "x"
        + std::to_string(IRRADIANCE_RESOLUTION) + ")");
    return true;
}

void EnvironmentMap::generatePrefilter()
{
    // Create prefiltered cubemap with mip chain (DSA)
    glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &m_prefilterMap);
    glTextureStorage2D(m_prefilterMap, MAX_MIP_LEVELS, GL_RGB16F,
                       PREFILTER_RESOLUTION, PREFILTER_RESOLUTION);
    glTextureParameteri(m_prefilterMap, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(m_prefilterMap, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_prefilterMap, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_prefilterMap, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_prefilterMap, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    // Prefilter at each roughness mip level
    m_prefilterShader.use();
    m_prefilterShader.setMat4("u_projection", iblCaptureProjection());
    m_prefilterShader.setFloat("u_envResolution", static_cast<float>(ENV_RESOLUTION));

    glBindTextureUnit(0, m_envCubemap);
    m_prefilterShader.setInt("u_environmentMap", 0);

    IblPrefilterParams params;
    params.shader = &m_prefilterShader;
    params.captureFbo = m_captureFbo;
    params.captureRbo = m_captureRbo;
    params.prefilterMap = m_prefilterMap;
    params.captureViews = iblCaptureViews();
    params.resolution = PREFILTER_RESOLUTION;
    params.mipLevels = MAX_MIP_LEVELS;
    params.drawCube = [this]() { drawIblCube(m_cubeVao); };
    runIblPrefilterLoop(params);

    Logger::debug("Prefiltered environment map generated ("
        + std::to_string(PREFILTER_RESOLUTION) + "x"
        + std::to_string(PREFILTER_RESOLUTION) + ", "
        + std::to_string(MAX_MIP_LEVELS) + " mip levels)");
}

void EnvironmentMap::generateBrdfLut(const FullscreenQuad& quad)
{
    // Create 2D BRDF integration LUT (DSA)
    glCreateTextures(GL_TEXTURE_2D, 1, &m_brdfLut);
    glTextureStorage2D(m_brdfLut, 1, GL_RG16F,
                       BRDF_LUT_RESOLUTION, BRDF_LUT_RESOLUTION);
    glTextureParameteri(m_brdfLut, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_brdfLut, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_brdfLut, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_brdfLut, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    glBindFramebuffer(GL_FRAMEBUFFER, m_captureFbo);
    glNamedRenderbufferStorage(m_captureRbo, GL_DEPTH_COMPONENT24,
                               BRDF_LUT_RESOLUTION, BRDF_LUT_RESOLUTION);
    glNamedFramebufferTexture(m_captureFbo, GL_COLOR_ATTACHMENT0,
                              m_brdfLut, 0);
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
    glBindTextureUnit(static_cast<GLuint>(unit), m_irradianceMap);
}

void EnvironmentMap::bindPrefilter(int unit) const
{
    glBindTextureUnit(static_cast<GLuint>(unit), m_prefilterMap);
}

void EnvironmentMap::bindBrdfLut(int unit) const
{
    glBindTextureUnit(static_cast<GLuint>(unit), m_brdfLut);
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
