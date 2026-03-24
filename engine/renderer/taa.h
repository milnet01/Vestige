/// @file taa.h
/// @brief Temporal Anti-Aliasing with Halton jitter, motion vectors, and history clamping.
#pragma once

#include "renderer/framebuffer.h"
#include "renderer/shader.h"

#include <glm/glm.hpp>

#include <memory>

namespace Vestige
{

/// @brief Anti-aliasing mode selection.
enum class AntiAliasMode
{
    NONE,
    MSAA_4X,
    TAA,
    SMAA
};

/// @brief Manages TAA state: jitter, history buffer, motion vectors, resolve.
class Taa
{
public:
    /// @brief Creates TAA resources at the given resolution.
    Taa(int width, int height);
    ~Taa();

    // Non-copyable
    Taa(const Taa&) = delete;
    Taa& operator=(const Taa&) = delete;

    /// @brief Resizes all TAA framebuffers.
    void resize(int width, int height);

    /// @brief Gets the current frame's sub-pixel jitter offset in NDC [-1, 1].
    glm::vec2 getJitterOffset() const;

    /// @brief Advances to the next jitter sample (call once per frame).
    void nextFrame();

    /// @brief Returns the jittered projection matrix.
    glm::mat4 jitterProjection(const glm::mat4& projection, int viewportWidth, int viewportHeight) const;

    /// @brief Gets the frame index (for Halton sequence).
    int getFrameIndex() const;

    /// @brief Gets the TAA current-frame FBO (write target for resolve pass).
    Framebuffer& getCurrentFbo();

    /// @brief Gets the TAA history FBO (previous frame's resolved output).
    Framebuffer& getHistoryFbo();

    /// @brief Gets the motion vector FBO.
    Framebuffer& getMotionVectorFbo();

    /// @brief Swaps current and history buffers (call at end of frame).
    void swapBuffers();

    /// @brief Gets the feedback factor (0.0 to 1.0, how much history to blend).
    float getFeedbackFactor() const;

    /// @brief Sets the feedback factor.
    void setFeedbackFactor(float factor);

    /// @brief Halton sequence value for a given index and base.
    static float halton(int index, int base);

private:
    std::unique_ptr<Framebuffer> m_currentFbo;
    std::unique_ptr<Framebuffer> m_historyFbo;
    std::unique_ptr<Framebuffer> m_motionVectorFbo;

    int m_frameIndex = 0;
    float m_feedbackFactor = 0.9f;
    int m_width;
    int m_height;

    static constexpr int JITTER_SEQUENCE_LENGTH = 16;
};

} // namespace Vestige
