/// @file framebuffer.cpp
/// @brief Framebuffer implementation — manages OpenGL FBO lifecycle.
#include "renderer/framebuffer.h"
#include "core/logger.h"

#include <string>

namespace Vestige
{

Framebuffer::Framebuffer(const FramebufferConfig& config)
    : m_config(config)
{
    create();
}

Framebuffer::~Framebuffer()
{
    cleanup();
}

Framebuffer::Framebuffer(Framebuffer&& other) noexcept
    : m_config(other.m_config)
    , m_fboId(other.m_fboId)
    , m_colorAttachment(other.m_colorAttachment)
    , m_depthAttachment(other.m_depthAttachment)
    , m_isDepthRenderbuffer(other.m_isDepthRenderbuffer)
    , m_isComplete(other.m_isComplete)
{
    other.m_fboId = 0;
    other.m_colorAttachment = 0;
    other.m_depthAttachment = 0;
    other.m_isComplete = false;
}

Framebuffer& Framebuffer::operator=(Framebuffer&& other) noexcept
{
    if (this != &other)
    {
        cleanup();
        m_config = other.m_config;
        m_fboId = other.m_fboId;
        m_colorAttachment = other.m_colorAttachment;
        m_depthAttachment = other.m_depthAttachment;
        m_isDepthRenderbuffer = other.m_isDepthRenderbuffer;
        m_isComplete = other.m_isComplete;
        other.m_fboId = 0;
        other.m_colorAttachment = 0;
        other.m_depthAttachment = 0;
        other.m_isComplete = false;
    }
    return *this;
}

void Framebuffer::bind()
{
    if (!m_isComplete)
    {
        Logger::error("Framebuffer::bind — binding incomplete FBO (id="
            + std::to_string(m_fboId) + ")");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, m_fboId);
    glViewport(0, 0, m_config.width, m_config.height);
}

void Framebuffer::unbind()
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::resolve(Framebuffer& destination)
{
    glBlitNamedFramebuffer(m_fboId, destination.getId(),
        0, 0, m_config.width, m_config.height,
        0, 0, destination.getWidth(), destination.getHeight(),
        GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

void Framebuffer::resize(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return;
    }

    m_config.width = width;
    m_config.height = height;
    cleanup();
    create();
}

void Framebuffer::bindColorTexture(int textureUnit)
{
    if (textureUnit < 0 || textureUnit > 31)
    {
        Logger::error("Framebuffer::bindColorTexture — invalid texture unit: "
            + std::to_string(textureUnit));
        return;
    }
    glBindTextureUnit(static_cast<GLuint>(textureUnit), m_colorAttachment);
}

void Framebuffer::bindDepthTexture(int textureUnit)
{
    if (textureUnit < 0 || textureUnit > 31)
    {
        Logger::error("Framebuffer::bindDepthTexture — invalid texture unit: "
            + std::to_string(textureUnit));
        return;
    }
    if (m_isDepthRenderbuffer)
    {
        Logger::warning("Cannot bind depth renderbuffer as texture — use isDepthTexture=true");
        return;
    }
    glBindTextureUnit(static_cast<GLuint>(textureUnit), m_depthAttachment);
}

GLuint Framebuffer::getId() const
{
    return m_fboId;
}

GLuint Framebuffer::getColorAttachmentId() const
{
    return m_colorAttachment;
}

GLuint Framebuffer::getDepthTextureId() const
{
    return m_isDepthRenderbuffer ? 0 : m_depthAttachment;
}

int Framebuffer::getWidth() const
{
    return m_config.width;
}

int Framebuffer::getHeight() const
{
    return m_config.height;
}

bool Framebuffer::isMultisampled() const
{
    return m_config.samples > 1;
}

const FramebufferConfig& Framebuffer::getConfig() const
{
    return m_config;
}

void Framebuffer::create()
{
    // Create FBO with DSA
    glCreateFramebuffers(1, &m_fboId);

    bool isMultisample = m_config.samples > 1;
    GLenum colorFormat = m_config.isFloatingPoint ? GL_RGBA16F : GL_RGBA8;

    // --- Color attachment ---
    if (m_config.hasColorAttachment)
    {
        if (isMultisample)
        {
            glCreateTextures(GL_TEXTURE_2D_MULTISAMPLE, 1, &m_colorAttachment);
            glTextureStorage2DMultisample(m_colorAttachment, m_config.samples,
                                          colorFormat, m_config.width, m_config.height, GL_TRUE);
            glNamedFramebufferTexture(m_fboId, GL_COLOR_ATTACHMENT0, m_colorAttachment, 0);
        }
        else
        {
            glCreateTextures(GL_TEXTURE_2D, 1, &m_colorAttachment);
            glTextureStorage2D(m_colorAttachment, 1, colorFormat, m_config.width, m_config.height);

            glTextureParameteri(m_colorAttachment, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTextureParameteri(m_colorAttachment, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameteri(m_colorAttachment, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(m_colorAttachment, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glNamedFramebufferTexture(m_fboId, GL_COLOR_ATTACHMENT0, m_colorAttachment, 0);
        }
    }
    else
    {
        // Depth-only FBO (e.g., shadow map) — no color output
        glNamedFramebufferDrawBuffer(m_fboId, GL_NONE);
        glNamedFramebufferReadBuffer(m_fboId, GL_NONE);
    }

    // --- Depth attachment ---
    if (m_config.hasDepthAttachment)
    {
        bool useDepthTexture = m_config.isDepthTexture && !isMultisample;

        if (useDepthTexture)
        {
            // Sampleable depth texture (for shadow maps, SSAO)
            glCreateTextures(GL_TEXTURE_2D, 1, &m_depthAttachment);
            glTextureStorage2D(m_depthAttachment, 1, GL_DEPTH_COMPONENT24,
                               m_config.width, m_config.height);
            glTextureParameteri(m_depthAttachment, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTextureParameteri(m_depthAttachment, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTextureParameteri(m_depthAttachment, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
            glTextureParameteri(m_depthAttachment, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
            float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
            glTextureParameterfv(m_depthAttachment, GL_TEXTURE_BORDER_COLOR, borderColor);
            glNamedFramebufferTexture(m_fboId, GL_DEPTH_ATTACHMENT, m_depthAttachment, 0);
            m_isDepthRenderbuffer = false;
        }
        else if (isMultisample)
        {
            // Multisampled depth renderbuffer
            glCreateRenderbuffers(1, &m_depthAttachment);
            glNamedRenderbufferStorageMultisample(m_depthAttachment, m_config.samples,
                                                  GL_DEPTH24_STENCIL8,
                                                  m_config.width, m_config.height);
            glNamedFramebufferRenderbuffer(m_fboId, GL_DEPTH_STENCIL_ATTACHMENT,
                                           GL_RENDERBUFFER, m_depthAttachment);
            m_isDepthRenderbuffer = true;
        }
        else
        {
            // Regular depth renderbuffer
            glCreateRenderbuffers(1, &m_depthAttachment);
            glNamedRenderbufferStorage(m_depthAttachment, GL_DEPTH24_STENCIL8,
                                       m_config.width, m_config.height);
            glNamedFramebufferRenderbuffer(m_fboId, GL_DEPTH_STENCIL_ATTACHMENT,
                                           GL_RENDERBUFFER, m_depthAttachment);
            m_isDepthRenderbuffer = true;
        }
    }

    // Check for OpenGL errors during FBO setup
#ifndef NDEBUG
    GLenum glErr;
    while ((glErr = glGetError()) != GL_NO_ERROR)
    {
        Logger::error("OpenGL error during FBO creation: 0x"
            + std::to_string(static_cast<unsigned int>(glErr)));
    }
#endif

    // Verify completeness (DSA)
    GLenum status = glCheckNamedFramebufferStatus(m_fboId, GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        Logger::error("Framebuffer incomplete — status: 0x"
            + std::to_string(static_cast<unsigned int>(status)));
        m_isComplete = false;
    }
    else
    {
        m_isComplete = true;
        Logger::debug("Framebuffer created: "
            + std::to_string(m_config.width) + "x" + std::to_string(m_config.height)
            + (isMultisample ? " (" + std::to_string(m_config.samples) + "x MSAA)" : "")
            + (m_config.isFloatingPoint ? " HDR" : ""));
    }
}

void Framebuffer::cleanup()
{
    if (m_colorAttachment != 0)
    {
        glDeleteTextures(1, &m_colorAttachment);
        m_colorAttachment = 0;
    }

    if (m_depthAttachment != 0)
    {
        if (m_isDepthRenderbuffer)
        {
            glDeleteRenderbuffers(1, &m_depthAttachment);
        }
        else
        {
            glDeleteTextures(1, &m_depthAttachment);
        }
        m_depthAttachment = 0;
    }

    if (m_fboId != 0)
    {
        glDeleteFramebuffers(1, &m_fboId);
        m_fboId = 0;
    }
}

bool Framebuffer::isComplete() const
{
    return m_isComplete;
}

} // namespace Vestige
