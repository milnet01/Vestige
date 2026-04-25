// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file eye_controller.cpp
/// @brief Procedural eye animation implementation.

#include "experimental/animation/eye_controller.h"
#include "experimental/animation/facial_presets.h"

#include <algorithm>

namespace Vestige
{

EyeController::EyeController()
    : m_rng(std::random_device{}())
{
    m_blinkTimer = randomFloat(m_blinkMinInterval, m_blinkMaxInterval);
    m_saccadeTimer = randomFloat(0.1f, 0.3f);
}

void EyeController::update(float deltaTime)
{
    updateBlink(deltaTime);
    updateGaze(deltaTime);
    updateSaccade(deltaTime);
    computeWeights();
}

// --- Look-at ---

void EyeController::setGazeTarget(const glm::vec3& directionInHeadSpace)
{
    m_gazeEnabled = true;
    float len = glm::length(directionInHeadSpace);
    if (len > 0.0001f)
    {
        m_gazeDirection = directionInHeadSpace / len;
    }
}

void EyeController::clearGazeTarget()
{
    m_gazeEnabled = false;
}

void EyeController::setGazeSpeed(float degreesPerSecond)
{
    m_gazeSpeed = degreesPerSecond;
}

void EyeController::setHorizontalLimit(float degrees)
{
    m_horizontalLimit = degrees;
}

void EyeController::setVerticalLimit(float degrees)
{
    m_verticalLimit = degrees;
}

// --- Blink ---

void EyeController::setBlinkEnabled(bool enabled)
{
    m_blinkEnabled = enabled;
    if (!enabled)
    {
        m_blinkProgress = -1.0f;
        m_blinkWeight = 0.0f;
        m_pendingDoubleBlink = false;
    }
}

void EyeController::setBlinkInterval(float minSeconds, float maxSeconds)
{
    m_blinkMinInterval = std::max(0.1f, minSeconds);
    m_blinkMaxInterval = std::max(m_blinkMinInterval, maxSeconds);
}

void EyeController::triggerBlink()
{
    if (m_blinkProgress < 0.0f && !m_pendingDoubleBlink)
    {
        m_blinkProgress = 0.0f;
    }
}

bool EyeController::isBlinking() const
{
    return m_blinkProgress >= 0.0f;
}

// --- Saccade ---

void EyeController::setSaccadeEnabled(bool enabled)
{
    m_saccadeEnabled = enabled;
    if (!enabled)
    {
        m_saccadeH = 0.0f;
        m_saccadeV = 0.0f;
    }
}

// --- Output ---

float EyeController::getWeight(const std::string& shapeName) const
{
    auto it = m_weights.find(shapeName);
    return (it != m_weights.end()) ? it->second : 0.0f;
}

const std::unordered_map<std::string, float>& EyeController::getWeights() const
{
    return m_weights;
}

// --- Private ---

void EyeController::updateBlink(float deltaTime)
{
    if (!m_blinkEnabled)
    {
        m_blinkWeight = 0.0f;
        return;
    }

    // Currently mid-blink
    if (m_blinkProgress >= 0.0f)
    {
        m_blinkProgress += deltaTime / BLINK_DURATION;
        if (m_blinkProgress >= 1.0f)
        {
            // Blink complete
            m_blinkProgress = -1.0f;
            m_blinkWeight = 0.0f;

            // Roll for double-blink
            if (!m_pendingDoubleBlink && randomFloat(0.0f, 1.0f) < DOUBLE_BLINK_CHANCE)
            {
                m_pendingDoubleBlink = true;
                m_doubleBlinkDelay = DOUBLE_BLINK_GAP;
            }
        }
        else
        {
            // Asymmetric blink curve: fast close, slower open
            float t = m_blinkProgress;
            if (t < BLINK_CLOSE_POINT)
            {
                m_blinkWeight = glm::smoothstep(0.0f, BLINK_CLOSE_POINT, t);
            }
            else
            {
                m_blinkWeight = 1.0f - glm::smoothstep(BLINK_CLOSE_POINT, 1.0f, t);
            }
        }
        return;
    }

    // Waiting for double-blink
    if (m_pendingDoubleBlink)
    {
        m_doubleBlinkDelay -= deltaTime;
        if (m_doubleBlinkDelay <= 0.0f)
        {
            m_pendingDoubleBlink = false;
            m_blinkProgress = 0.0f;
        }
        return;
    }

    // Normal blink timer countdown
    m_blinkTimer -= deltaTime;
    if (m_blinkTimer <= 0.0f)
    {
        m_blinkProgress = 0.0f;
        m_blinkTimer = randomFloat(m_blinkMinInterval, m_blinkMaxInterval);
    }
}

void EyeController::updateGaze(float deltaTime)
{
    if (!m_gazeEnabled)
    {
        m_targetGazeH = 0.0f;
        m_targetGazeV = 0.0f;
    }
    else
    {
        // Convert head-local direction to horizontal/vertical angles.
        // Head-local space: -Z = forward, +X = right, +Y = up.
        float horizontal = glm::atan(m_gazeDirection.x, -m_gazeDirection.z);
        float vertical = glm::asin(glm::clamp(m_gazeDirection.y, -1.0f, 1.0f));

        // Normalize to [-1, 1] based on limits
        float hLimitRad = glm::radians(m_horizontalLimit);
        float vLimitRad = glm::radians(m_verticalLimit);

        m_targetGazeH = glm::clamp(horizontal / hLimitRad, -1.0f, 1.0f);
        m_targetGazeV = glm::clamp(vertical / vLimitRad, -1.0f, 1.0f);
    }

    // Smooth exponential interpolation toward target
    float rate = glm::min(1.0f, glm::radians(m_gazeSpeed) * deltaTime * 5.0f);
    m_currentGazeH = glm::mix(m_currentGazeH, m_targetGazeH, rate);
    m_currentGazeV = glm::mix(m_currentGazeV, m_targetGazeV, rate);
}

void EyeController::updateSaccade(float deltaTime)
{
    if (!m_saccadeEnabled)
    {
        m_saccadeH = 0.0f;
        m_saccadeV = 0.0f;
        return;
    }

    m_saccadeTimer -= deltaTime;
    if (m_saccadeTimer <= 0.0f)
    {
        m_saccadeH = randomFloat(-0.05f, 0.05f);
        m_saccadeV = randomFloat(-0.03f, 0.03f);
        m_saccadeTimer = randomFloat(0.1f, 0.3f);
    }
}

void EyeController::computeWeights()
{
    m_weights.clear();

    // Blink
    if (m_blinkWeight > 0.0f)
    {
        m_weights[BlendShape::EYE_BLINK_LEFT] = m_blinkWeight;
        m_weights[BlendShape::EYE_BLINK_RIGHT] = m_blinkWeight;
    }

    // Gaze (current + saccade offset)
    float gazeH = glm::clamp(m_currentGazeH + m_saccadeH, -1.0f, 1.0f);
    float gazeV = glm::clamp(m_currentGazeV + m_saccadeV, -1.0f, 1.0f);

    // Horizontal: positive = looking right from character's perspective
    // "In" = toward nose, "Out" = away from nose
    if (gazeH > 0.001f)
    {
        // Looking right: left eye turns in (toward nose), right eye turns out
        m_weights[BlendShape::EYE_LOOK_IN_LEFT] = gazeH;
        m_weights[BlendShape::EYE_LOOK_OUT_RIGHT] = gazeH;
    }
    else if (gazeH < -0.001f)
    {
        // Looking left: left eye turns out, right eye turns in (toward nose)
        m_weights[BlendShape::EYE_LOOK_OUT_LEFT] = -gazeH;
        m_weights[BlendShape::EYE_LOOK_IN_RIGHT] = -gazeH;
    }

    // Vertical: positive = looking up
    if (gazeV > 0.001f)
    {
        m_weights[BlendShape::EYE_LOOK_UP_LEFT] = gazeV;
        m_weights[BlendShape::EYE_LOOK_UP_RIGHT] = gazeV;
    }
    else if (gazeV < -0.001f)
    {
        m_weights[BlendShape::EYE_LOOK_DOWN_LEFT] = -gazeV;
        m_weights[BlendShape::EYE_LOOK_DOWN_RIGHT] = -gazeV;
    }
}

float EyeController::randomFloat(float minVal, float maxVal)
{
    std::uniform_real_distribution<float> dist(minVal, maxVal);
    return dist(m_rng);
}

} // namespace Vestige
