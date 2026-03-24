/// @file framebuffer.h
/// @brief OpenGL framebuffer object wrapper for off-screen rendering.
#pragma once

#include <glad/gl.h>

namespace Vestige
{

/// @brief Configuration for creating a framebuffer.
struct FramebufferConfig
{
    int width = 1280;
    int height = 720;
    int samples = 1;               // 1 = no MSAA, 4 = 4x MSAA
    bool hasColorAttachment = true;
    bool hasDepthAttachment = true;
    bool isFloatingPoint = false;   // true = GL_RGBA16F (for HDR)
    bool isDepthTexture = false;    // true = depth stored as sampleable texture
};

/// @brief Wraps an OpenGL framebuffer object for off-screen rendering.
class Framebuffer
{
public:
    /// @brief Creates a framebuffer with the given configuration.
    /// @param config Framebuffer settings.
    explicit Framebuffer(const FramebufferConfig& config);
    ~Framebuffer();

    // Non-copyable
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    // Movable
    Framebuffer(Framebuffer&& other) noexcept;
    Framebuffer& operator=(Framebuffer&& other) noexcept;

    /// @brief Binds this framebuffer for rendering.
    void bind();

    /// @brief Unbinds all framebuffers — returns to the default (screen) framebuffer.
    static void unbind();

    /// @brief Resolves this multisampled framebuffer to a non-multisampled destination.
    /// @param destination The target framebuffer (must not be multisampled).
    void resolve(Framebuffer& destination);

    /// @brief Resizes the framebuffer to new dimensions.
    /// @param width New width in pixels.
    /// @param height New height in pixels.
    void resize(int width, int height);

    /// @brief Binds the color texture to a texture unit for sampling.
    /// @param textureUnit The texture unit to bind to (e.g., 0 for GL_TEXTURE0).
    void bindColorTexture(int textureUnit = 0);

    /// @brief Binds the depth texture to a texture unit for sampling.
    /// @param textureUnit The texture unit to bind to.
    void bindDepthTexture(int textureUnit = 0);

    /// @brief Gets the OpenGL framebuffer ID.
    GLuint getId() const;

    /// @brief Gets the color attachment texture/renderbuffer ID.
    GLuint getColorAttachmentId() const;

    /// @brief Gets the depth attachment texture ID (0 if renderbuffer or absent).
    GLuint getDepthTextureId() const;

    /// @brief Gets the framebuffer width.
    int getWidth() const;

    /// @brief Gets the framebuffer height.
    int getHeight() const;

    /// @brief Checks if this framebuffer is multisampled.
    bool isMultisampled() const;

    /// @brief Gets the current configuration.
    const FramebufferConfig& getConfig() const;

    /// @brief Returns true if the framebuffer was created successfully.
    bool isComplete() const;

private:
    void create();
    void cleanup();

    FramebufferConfig m_config;
    GLuint m_fboId = 0;
    GLuint m_colorAttachment = 0;
    GLuint m_depthAttachment = 0;
    bool m_isDepthRenderbuffer = false;  // Tracks depth attachment type for cleanup
    bool m_isComplete = false;           // True if FBO passed completeness check
};

} // namespace Vestige
