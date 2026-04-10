/// @file water_fbo.cpp
/// @brief Manages reflection and refraction FBOs for water rendering.
#include "renderer/water_fbo.h"
#include "core/logger.h"

namespace Vestige
{

WaterFbo::~WaterFbo()
{
    shutdown();
}

WaterFbo::WaterFbo(WaterFbo&& other) noexcept
    : m_reflectionFbo(other.m_reflectionFbo)
    , m_reflectionColorTex(other.m_reflectionColorTex)
    , m_reflectionDepthRbo(other.m_reflectionDepthRbo)
    , m_reflectionWidth(other.m_reflectionWidth)
    , m_reflectionHeight(other.m_reflectionHeight)
    , m_refractionFbo(other.m_refractionFbo)
    , m_refractionColorTex(other.m_refractionColorTex)
    , m_refractionDepthTex(other.m_refractionDepthTex)
    , m_refractionWidth(other.m_refractionWidth)
    , m_refractionHeight(other.m_refractionHeight)
{
    other.m_reflectionFbo = 0;
    other.m_reflectionColorTex = 0;
    other.m_reflectionDepthRbo = 0;
    other.m_reflectionWidth = 0;
    other.m_reflectionHeight = 0;
    other.m_refractionFbo = 0;
    other.m_refractionColorTex = 0;
    other.m_refractionDepthTex = 0;
    other.m_refractionWidth = 0;
    other.m_refractionHeight = 0;
}

WaterFbo& WaterFbo::operator=(WaterFbo&& other) noexcept
{
    if (this != &other)
    {
        // Destroy own GPU resources
        shutdown();

        // Transfer all state
        m_reflectionFbo = other.m_reflectionFbo;
        m_reflectionColorTex = other.m_reflectionColorTex;
        m_reflectionDepthRbo = other.m_reflectionDepthRbo;
        m_reflectionWidth = other.m_reflectionWidth;
        m_reflectionHeight = other.m_reflectionHeight;
        m_refractionFbo = other.m_refractionFbo;
        m_refractionColorTex = other.m_refractionColorTex;
        m_refractionDepthTex = other.m_refractionDepthTex;
        m_refractionWidth = other.m_refractionWidth;
        m_refractionHeight = other.m_refractionHeight;

        // Zero the source
        other.m_reflectionFbo = 0;
        other.m_reflectionColorTex = 0;
        other.m_reflectionDepthRbo = 0;
        other.m_reflectionWidth = 0;
        other.m_reflectionHeight = 0;
        other.m_refractionFbo = 0;
        other.m_refractionColorTex = 0;
        other.m_refractionDepthTex = 0;
        other.m_refractionWidth = 0;
        other.m_refractionHeight = 0;
    }
    return *this;
}

bool WaterFbo::init(int reflW, int reflH, int refrW, int refrH)
{
    createReflectionFbo(reflW, reflH);
    createRefractionFbo(refrW, refrH);

    // Verify both FBOs are complete
    GLenum reflStatus = glCheckNamedFramebufferStatus(m_reflectionFbo, GL_FRAMEBUFFER);
    GLenum refrStatus = glCheckNamedFramebufferStatus(m_refractionFbo, GL_FRAMEBUFFER);

    if (reflStatus != GL_FRAMEBUFFER_COMPLETE)
    {
        Logger::error("Water reflection FBO incomplete");
        return false;
    }
    if (refrStatus != GL_FRAMEBUFFER_COMPLETE)
    {
        Logger::error("Water refraction FBO incomplete");
        return false;
    }

    Logger::debug("Water FBOs initialized (reflection: " + std::to_string(reflW) + "x"
                  + std::to_string(reflH) + ", refraction: " + std::to_string(refrW) + "x"
                  + std::to_string(refrH) + ")");
    return true;
}

void WaterFbo::resize(int reflW, int reflH, int refrW, int refrH)
{
    // Skip if size hasn't changed
    if (reflW == m_reflectionWidth && reflH == m_reflectionHeight
        && refrW == m_refractionWidth && refrH == m_refractionHeight)
    {
        return;
    }

    shutdown();
    init(reflW, reflH, refrW, refrH);
}

void WaterFbo::shutdown()
{
    destroyReflectionFbo();
    destroyRefractionFbo();
}

void WaterFbo::bindReflection()
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_reflectionFbo);
    glViewport(0, 0, m_reflectionWidth, m_reflectionHeight);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void WaterFbo::bindRefraction()
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_refractionFbo);
    glViewport(0, 0, m_refractionWidth, m_refractionHeight);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void WaterFbo::unbind(int viewportWidth, int viewportHeight)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, viewportWidth, viewportHeight);
}

void WaterFbo::createReflectionFbo(int width, int height)
{
    m_reflectionWidth = width;
    m_reflectionHeight = height;

    // Color texture (RGBA8)
    glCreateTextures(GL_TEXTURE_2D, 1, &m_reflectionColorTex);
    glTextureStorage2D(m_reflectionColorTex, 1, GL_RGBA8,
                       static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glTextureParameteri(m_reflectionColorTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_reflectionColorTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_reflectionColorTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_reflectionColorTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Depth renderbuffer (not sampled, just for correct depth testing)
    glCreateRenderbuffers(1, &m_reflectionDepthRbo);
    glNamedRenderbufferStorage(m_reflectionDepthRbo, GL_DEPTH_COMPONENT24,
                               static_cast<GLsizei>(width), static_cast<GLsizei>(height));

    // Create FBO and attach
    glCreateFramebuffers(1, &m_reflectionFbo);
    glNamedFramebufferTexture(m_reflectionFbo, GL_COLOR_ATTACHMENT0, m_reflectionColorTex, 0);
    glNamedFramebufferRenderbuffer(m_reflectionFbo, GL_DEPTH_ATTACHMENT,
                                   GL_RENDERBUFFER, m_reflectionDepthRbo);
}

void WaterFbo::createRefractionFbo(int width, int height)
{
    m_refractionWidth = width;
    m_refractionHeight = height;

    // Color texture (RGBA8)
    glCreateTextures(GL_TEXTURE_2D, 1, &m_refractionColorTex);
    glTextureStorage2D(m_refractionColorTex, 1, GL_RGBA8,
                       static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glTextureParameteri(m_refractionColorTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_refractionColorTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_refractionColorTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_refractionColorTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Depth texture (sampled for water thickness / Beer's law)
    glCreateTextures(GL_TEXTURE_2D, 1, &m_refractionDepthTex);
    glTextureStorage2D(m_refractionDepthTex, 1, GL_DEPTH_COMPONENT32F,
                       static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glTextureParameteri(m_refractionDepthTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_refractionDepthTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_refractionDepthTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_refractionDepthTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Create FBO and attach
    glCreateFramebuffers(1, &m_refractionFbo);
    glNamedFramebufferTexture(m_refractionFbo, GL_COLOR_ATTACHMENT0, m_refractionColorTex, 0);
    glNamedFramebufferTexture(m_refractionFbo, GL_DEPTH_ATTACHMENT, m_refractionDepthTex, 0);
}

void WaterFbo::destroyReflectionFbo()
{
    if (m_reflectionFbo != 0)
    {
        glDeleteFramebuffers(1, &m_reflectionFbo);
        m_reflectionFbo = 0;
    }
    if (m_reflectionColorTex != 0)
    {
        glDeleteTextures(1, &m_reflectionColorTex);
        m_reflectionColorTex = 0;
    }
    if (m_reflectionDepthRbo != 0)
    {
        glDeleteRenderbuffers(1, &m_reflectionDepthRbo);
        m_reflectionDepthRbo = 0;
    }
}

void WaterFbo::destroyRefractionFbo()
{
    if (m_refractionFbo != 0)
    {
        glDeleteFramebuffers(1, &m_refractionFbo);
        m_refractionFbo = 0;
    }
    if (m_refractionColorTex != 0)
    {
        glDeleteTextures(1, &m_refractionColorTex);
        m_refractionColorTex = 0;
    }
    if (m_refractionDepthTex != 0)
    {
        glDeleteTextures(1, &m_refractionDepthTex);
        m_refractionDepthTex = 0;
    }
}

} // namespace Vestige
