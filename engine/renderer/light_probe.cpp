/// @file light_probe.cpp
/// @brief Light probe implementation — IBL capture and convolution.
#include "renderer/light_probe.h"
#include "core/logger.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace Vestige
{

// Same cube vertices used by EnvironmentMap for cubemap face rendering
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

// Standard cubemap capture view matrices (from the origin, looking at each face)
static const glm::mat4 CAPTURE_VIEWS[6] = {
    glm::lookAt(glm::vec3(0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
    glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
    glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
    glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
    glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
    glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
};

static const glm::mat4 CAPTURE_PROJECTION =
    glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);

LightProbe::LightProbe() = default;

LightProbe::~LightProbe()
{
    if (m_cubeVao != 0)    glDeleteVertexArrays(1, &m_cubeVao);
    if (m_cubeVbo != 0)    glDeleteBuffers(1, &m_cubeVbo);
    if (m_captureFbo != 0) glDeleteFramebuffers(1, &m_captureFbo);
    if (m_captureRbo != 0) glDeleteRenderbuffers(1, &m_captureRbo);
    if (m_irradianceMap != 0) glDeleteTextures(1, &m_irradianceMap);
    if (m_prefilterMap != 0)  glDeleteTextures(1, &m_prefilterMap);
}

void LightProbe::setPosition(const glm::vec3& position) { m_position = position; }
glm::vec3 LightProbe::getPosition() const { return m_position; }

void LightProbe::setInfluenceAABB(const AABB& bounds) { m_influenceAABB = bounds; }
AABB LightProbe::getInfluenceAABB() const { return m_influenceAABB; }

void LightProbe::setFadeDistance(float distance) { m_fadeDistance = std::max(0.01f, distance); }
float LightProbe::getFadeDistance() const { return m_fadeDistance; }

bool LightProbe::initialize(const Shader& irradianceShader, const Shader& prefilterShader)
{
    m_irradianceShader = &irradianceShader;
    m_prefilterShader = &prefilterShader;

    // Create cube VAO (DSA)
    glCreateBuffers(1, &m_cubeVbo);
    glNamedBufferStorage(m_cubeVbo, sizeof(CUBE_VERTICES), CUBE_VERTICES, 0);

    glCreateVertexArrays(1, &m_cubeVao);
    glVertexArrayVertexBuffer(m_cubeVao, 0, m_cubeVbo, 0, 3 * sizeof(float));
    glEnableVertexArrayAttrib(m_cubeVao, 0);
    glVertexArrayAttribFormat(m_cubeVao, 0, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(m_cubeVao, 0, 0);

    // Create capture FBO + depth RBO
    glCreateFramebuffers(1, &m_captureFbo);
    glCreateRenderbuffers(1, &m_captureRbo);

    Logger::debug("Light probe initialized at ("
        + std::to_string(m_position.x) + ", "
        + std::to_string(m_position.y) + ", "
        + std::to_string(m_position.z) + ")");
    return true;
}

void LightProbe::generateFromCubemap(GLuint capturedCubemap)
{
    // Delete old textures if regenerating
    if (m_irradianceMap != 0) { glDeleteTextures(1, &m_irradianceMap); m_irradianceMap = 0; }
    if (m_prefilterMap != 0)  { glDeleteTextures(1, &m_prefilterMap);  m_prefilterMap = 0; }

    generateIrradiance(capturedCubemap);
    generatePrefilter(capturedCubemap);

    m_ready = true;
    Logger::info("Light probe IBL generated (irradiance "
        + std::to_string(IRRADIANCE_RESOLUTION) + "x"
        + std::to_string(IRRADIANCE_RESOLUTION) + ", prefilter "
        + std::to_string(PREFILTER_RESOLUTION) + "x"
        + std::to_string(PREFILTER_RESOLUTION) + " with "
        + std::to_string(MAX_MIP_LEVELS) + " mips)");
}

void LightProbe::generateIrradiance(GLuint envCubemap)
{
    glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &m_irradianceMap);
    glTextureStorage2D(m_irradianceMap, 1, GL_RGB16F,
                       IRRADIANCE_RESOLUTION, IRRADIANCE_RESOLUTION);
    glTextureParameteri(m_irradianceMap, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_irradianceMap, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_irradianceMap, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_irradianceMap, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_irradianceMap, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    m_irradianceShader->use();
    m_irradianceShader->setMat4("u_projection", CAPTURE_PROJECTION);
    glBindTextureUnit(0, envCubemap);
    m_irradianceShader->setInt("u_environmentMap", 0);

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    glNamedRenderbufferStorage(m_captureRbo, GL_DEPTH_COMPONENT24,
                               IRRADIANCE_RESOLUTION, IRRADIANCE_RESOLUTION);
    glNamedFramebufferRenderbuffer(m_captureFbo, GL_DEPTH_ATTACHMENT,
                                   GL_RENDERBUFFER, m_captureRbo);

    glBindFramebuffer(GL_FRAMEBUFFER, m_captureFbo);
    glViewport(0, 0, IRRADIANCE_RESOLUTION, IRRADIANCE_RESOLUTION);
    glDisable(GL_CULL_FACE);

    for (int face = 0; face < 6; face++)
    {
        m_irradianceShader->setMat4("u_view", CAPTURE_VIEWS[face]);
        glNamedFramebufferTextureLayer(m_captureFbo, GL_COLOR_ATTACHMENT0,
                                       m_irradianceMap, 0, face);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderCube();
    }

    glEnable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

void LightProbe::generatePrefilter(GLuint envCubemap)
{
    glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &m_prefilterMap);
    glTextureStorage2D(m_prefilterMap, MAX_MIP_LEVELS, GL_RGB16F,
                       PREFILTER_RESOLUTION, PREFILTER_RESOLUTION);
    glTextureParameteri(m_prefilterMap, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(m_prefilterMap, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_prefilterMap, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_prefilterMap, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_prefilterMap, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    m_prefilterShader->use();
    m_prefilterShader->setMat4("u_projection", CAPTURE_PROJECTION);
    m_prefilterShader->setFloat("u_envResolution", static_cast<float>(CAPTURE_RESOLUTION));
    glBindTextureUnit(0, envCubemap);
    m_prefilterShader->setInt("u_environmentMap", 0);

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

        glNamedRenderbufferStorage(m_captureRbo, GL_DEPTH_COMPONENT24,
                                   mipWidth, mipHeight);
        glViewport(0, 0, mipWidth, mipHeight);

        float roughness = static_cast<float>(mip)
                        / static_cast<float>(MAX_MIP_LEVELS - 1);
        m_prefilterShader->setFloat("u_roughness", roughness);

        for (int face = 0; face < 6; face++)
        {
            m_prefilterShader->setMat4("u_view", CAPTURE_VIEWS[face]);
            glNamedFramebufferTextureLayer(m_captureFbo, GL_COLOR_ATTACHMENT0,
                                           m_prefilterMap, mip, face);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            renderCube();
        }
    }

    glEnable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

void LightProbe::renderCube() const
{
    glBindVertexArray(m_cubeVao);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
}

void LightProbe::bindIrradiance(int unit) const
{
    glBindTextureUnit(unit, m_irradianceMap);
}

void LightProbe::bindPrefilter(int unit) const
{
    glBindTextureUnit(unit, m_prefilterMap);
}

bool LightProbe::containsPoint(const glm::vec3& point) const
{
    return m_influenceAABB.contains(point);
}

float LightProbe::getBlendWeight(const glm::vec3& point) const
{
    if (!m_influenceAABB.contains(point))
    {
        return 0.0f;
    }

    // Compute minimum distance from point to any face of the AABB
    glm::vec3 minDist = point - m_influenceAABB.min;
    glm::vec3 maxDist = m_influenceAABB.max - point;
    float edgeDist = std::min({minDist.x, minDist.y, minDist.z,
                               maxDist.x, maxDist.y, maxDist.z});

    // Fade from 1.0 (deep inside) to 0.0 (at the boundary)
    return glm::clamp(edgeDist / m_fadeDistance, 0.0f, 1.0f);
}

bool LightProbe::isReady() const
{
    return m_ready;
}

} // namespace Vestige
