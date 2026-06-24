// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

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
    bool secondColorAttachment = false; // true = add a GL_COLOR_ATTACHMENT1 (MRT, e.g. motion vectors)
    bool thirdColorAttachment = false;  // true = add a GL_COLOR_ATTACHMENT2 (MRT, e.g. world normals); requires secondColorAttachment
    bool fourthColorAttachment = false; // true = add a GL_COLOR_ATTACHMENT3 (MRT, e.g. GI injection source); requires thirdColorAttachment
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

    /// @brief Binds a color attachment to a texture unit for sampling.
    /// @param textureUnit The texture unit to bind to (e.g., 0 for GL_TEXTURE0).
    /// @param attachmentIndex Which color attachment to bind (0 = GL_COLOR_ATTACHMENT0,
    ///        1 = GL_COLOR_ATTACHMENT1, requires secondColorAttachment;
    ///        2 = GL_COLOR_ATTACHMENT2, requires thirdColorAttachment;
    ///        3 = GL_COLOR_ATTACHMENT3, requires fourthColorAttachment).
    void bindColorTexture(int textureUnit = 0, int attachmentIndex = 0);

    /// @brief Clears the second color attachment (GL_COLOR_ATTACHMENT1) to (0,0,0,0).
    /// @note A single glClear writes the global clear-color into every draw buffer, so the
    ///       second attachment must be cleared independently to keep its coverage flag at 0
    ///       regardless of the scene clear color. No-op if secondColorAttachment is false.
    void clearSecondAttachment();

    /// @brief Clears the third color attachment (GL_COLOR_ATTACHMENT2) to (0,0,0,0).
    /// @note Same rationale as clearSecondAttachment. For the normal buffer, (0,0,0,0) is a
    ///       zero-length normal — the disocclusion sentinel that disables V_mask on pixels
    ///       no opaque geometry wrote. No-op if thirdColorAttachment is false.
    void clearThirdAttachment();

    /// @brief Clears the fourth color attachment (GL_COLOR_ATTACHMENT3) to (0,0,0,0).
    /// @note Same rationale as clearSecondAttachment. For the GI injection source, (0,0,0,0)
    ///       means uncovered / non-opaque texels inject zero radiance into the froxel cache
    ///       rather than stale garbage. No-op if fourthColorAttachment is false.
    void clearFourthAttachment();

    /// @brief Binds the depth texture to a texture unit for sampling.
    /// @param textureUnit The texture unit to bind to.
    void bindDepthTexture(int textureUnit = 0);

    /// @brief Gets the OpenGL framebuffer ID.
    GLuint getId() const;

    /// @brief Gets the color attachment texture/renderbuffer ID (attachment 0).
    GLuint getColorAttachmentId() const;

    /// @brief Gets a specific MRT colour-attachment texture ID (1/2/3; any other
    ///        index returns attachment 0). 0 if that attachment is not enabled.
    GLuint getColorAttachmentId(int attachmentIndex) const;

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
    GLuint m_colorAttachment1 = 0;       // Second color attachment (GL_COLOR_ATTACHMENT1, MRT)
    GLuint m_colorAttachment2 = 0;       // Third color attachment (GL_COLOR_ATTACHMENT2, MRT)
    GLuint m_colorAttachment3 = 0;       // Fourth color attachment (GL_COLOR_ATTACHMENT3, MRT)
    GLuint m_depthAttachment = 0;
    bool m_isDepthRenderbuffer = false;  // Tracks depth attachment type for cleanup
    bool m_isComplete = false;           // True if FBO passed completeness check
};

} // namespace Vestige
