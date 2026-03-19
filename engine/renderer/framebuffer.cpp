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
{
    other.m_fboId = 0;
    other.m_colorAttachment = 0;
    other.m_depthAttachment = 0;
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
        other.m_fboId = 0;
        other.m_colorAttachment = 0;
        other.m_depthAttachment = 0;
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
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fboId);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination.getId());
    glBlitFramebuffer(
        0, 0, m_config.width, m_config.height,
        0, 0, destination.getWidth(), destination.getHeight(),
        GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(textureUnit));
    if (m_config.samples > 1)
    {
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, m_colorAttachment);
    }
    else
    {
        glBindTexture(GL_TEXTURE_2D, m_colorAttachment);
    }
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
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(textureUnit));
    glBindTexture(GL_TEXTURE_2D, m_depthAttachment);
}

GLuint Framebuffer::getId() const
{
    return m_fboId;
}

GLuint Framebuffer::getColorAttachmentId() const
{
    return m_colorAttachment;
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
    glGenFramebuffers(1, &m_fboId);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fboId);

    bool isMultisample = m_config.samples > 1;
    GLenum colorFormat = m_config.isFloatingPoint ? GL_RGBA16F : GL_RGBA8;

    // --- Color attachment ---
    if (m_config.hasColorAttachment)
    {
        glGenTextures(1, &m_colorAttachment);

        if (isMultisample)
        {
            glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, m_colorAttachment);
            glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, m_config.samples,
                                   colorFormat, m_config.width, m_config.height, GL_TRUE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_TEXTURE_2D_MULTISAMPLE, m_colorAttachment, 0);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, m_colorAttachment);

            if (m_config.isFloatingPoint)
            {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_config.width, m_config.height,
                            0, GL_RGBA, GL_FLOAT, nullptr);
            }
            else
            {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_config.width, m_config.height,
                            0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            }

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_TEXTURE_2D, m_colorAttachment, 0);
        }
    }
    else
    {
        // Depth-only FBO (e.g., shadow map) — no color output
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }

    // --- Depth attachment ---
    if (m_config.hasDepthAttachment)
    {
        bool useDepthTexture = m_config.isDepthTexture && !isMultisample;

        if (useDepthTexture)
        {
            // Sampleable depth texture (for shadow maps, SSAO)
            glGenTextures(1, &m_depthAttachment);
            glBindTexture(GL_TEXTURE_2D, m_depthAttachment);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                        m_config.width, m_config.height,
                        0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
            float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
            glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_TEXTURE_2D, m_depthAttachment, 0);
            m_isDepthRenderbuffer = false;
        }
        else if (isMultisample)
        {
            // Multisampled depth renderbuffer
            glGenRenderbuffers(1, &m_depthAttachment);
            glBindRenderbuffer(GL_RENDERBUFFER, m_depthAttachment);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_config.samples,
                                           GL_DEPTH24_STENCIL8,
                                           m_config.width, m_config.height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                    GL_RENDERBUFFER, m_depthAttachment);
            m_isDepthRenderbuffer = true;
        }
        else
        {
            // Regular depth renderbuffer
            glGenRenderbuffers(1, &m_depthAttachment);
            glBindRenderbuffer(GL_RENDERBUFFER, m_depthAttachment);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                                m_config.width, m_config.height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
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

    // Verify completeness
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
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

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
