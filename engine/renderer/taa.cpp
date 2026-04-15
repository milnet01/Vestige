// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file taa.cpp
/// @brief TAA implementation.
#include "renderer/taa.h"
#include "core/logger.h"

namespace Vestige
{

Taa::Taa(int width, int height)
    : m_width(width)
    , m_height(height)
{
    // Current and history FBOs: full-res, RGBA16F, no depth
    FramebufferConfig colorConfig;
    colorConfig.width = width;
    colorConfig.height = height;
    colorConfig.samples = 1;
    colorConfig.hasColorAttachment = true;
    colorConfig.hasDepthAttachment = false;
    colorConfig.isFloatingPoint = true;

    m_currentFbo = std::make_unique<Framebuffer>(colorConfig);
    m_historyFbo = std::make_unique<Framebuffer>(colorConfig);

    // Motion vector FBO: full-res, RGBA16F (only RG used).
    // AUDIT.md §H15 / FIXPLAN G1: depth attachment required so the
    // per-object overlay pass can depth-test against geometry it has
    // drawn earlier in the pass.
    FramebufferConfig mvConfig;
    mvConfig.width = width;
    mvConfig.height = height;
    mvConfig.samples = 1;
    mvConfig.hasColorAttachment = true;
    mvConfig.hasDepthAttachment = true;
    mvConfig.isFloatingPoint = true;

    m_motionVectorFbo = std::make_unique<Framebuffer>(mvConfig);

    Logger::debug("TAA initialized: " + std::to_string(width) + "x" + std::to_string(height));
}

Taa::~Taa() = default;

void Taa::resize(int width, int height)
{
    m_width = width;
    m_height = height;
    if (m_currentFbo) m_currentFbo->resize(width, height);
    if (m_historyFbo) m_historyFbo->resize(width, height);
    if (m_motionVectorFbo) m_motionVectorFbo->resize(width, height);
}

glm::vec2 Taa::getJitterOffset() const
{
    int idx = m_frameIndex % JITTER_SEQUENCE_LENGTH + 1;  // 1-based for Halton
    return glm::vec2(
        halton(idx, 2) - 0.5f,
        halton(idx, 3) - 0.5f
    );
}

void Taa::nextFrame()
{
    m_frameIndex++;
}

glm::mat4 Taa::jitterProjection(const glm::mat4& projection,
                                  int viewportWidth, int viewportHeight) const
{
    glm::vec2 jitter = getJitterOffset();
    glm::mat4 jittered = projection;
    // Apply sub-pixel offset: scale jitter to NDC (2 pixels / resolution)
    jittered[2][0] += jitter.x * 2.0f / static_cast<float>(viewportWidth);
    jittered[2][1] += jitter.y * 2.0f / static_cast<float>(viewportHeight);
    return jittered;
}

int Taa::getFrameIndex() const
{
    return m_frameIndex;
}

Framebuffer& Taa::getCurrentFbo()
{
    return *m_currentFbo;
}

Framebuffer& Taa::getHistoryFbo()
{
    return *m_historyFbo;
}

Framebuffer& Taa::getMotionVectorFbo()
{
    return *m_motionVectorFbo;
}

void Taa::swapBuffers()
{
    std::swap(m_currentFbo, m_historyFbo);
}

float Taa::getFeedbackFactor() const
{
    return m_feedbackFactor;
}

void Taa::setFeedbackFactor(float factor)
{
    if (factor < 0.0f) factor = 0.0f;
    if (factor > 1.0f) factor = 1.0f;
    m_feedbackFactor = factor;
}

float Taa::halton(int index, int base)
{
    float f = 1.0f;
    float r = 0.0f;
    int i = index;
    while (i > 0)
    {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(i % base);
        i /= base;
    }
    return r;
}

} // namespace Vestige
