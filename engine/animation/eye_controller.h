/// @file eye_controller.h
/// @brief Procedural eye animation: blink, look-at gaze, and saccade noise.
#pragma once

#include <glm/glm.hpp>

#include <random>
#include <string>
#include <unordered_map>

namespace Vestige
{

/// @brief Procedural eye animation utility.
///
/// Generates blend shape weights for blink, gaze direction, and microsaccade
/// noise. Used internally by FacialAnimator to populate the eye layer.
///
/// Blink uses an asymmetric smoothstep curve (~80ms close, ~70ms open) with
/// random intervals (2-6s) and a 15% chance of double-blink.
///
/// Gaze maps a head-local direction to the 8 ARKit eyeLook blend shapes
/// with anatomical limits (35 deg horizontal, 25 deg vertical).
class EyeController
{
public:
    EyeController();
    EyeController(const EyeController&) = default;
    EyeController& operator=(const EyeController&) = default;

    /// @brief Per-frame update. Call before reading weights.
    void update(float deltaTime);

    // --- Look-at ---

    /// @brief Sets a gaze target direction in head-local space (-Z = forward).
    void setGazeTarget(const glm::vec3& directionInHeadSpace);

    /// @brief Clears the gaze target (eyes return to center).
    void clearGazeTarget();

    /// @brief Sets gaze interpolation speed in degrees per second.
    void setGazeSpeed(float degreesPerSecond);

    /// @brief Sets horizontal gaze limit in degrees (default 35).
    void setHorizontalLimit(float degrees);

    /// @brief Sets vertical gaze limit in degrees (default 25).
    void setVerticalLimit(float degrees);

    // --- Blink ---

    /// @brief Enables or disables procedural blink.
    void setBlinkEnabled(bool enabled);

    /// @brief Sets the random blink interval range in seconds.
    void setBlinkInterval(float minSeconds, float maxSeconds);

    /// @brief Forces an immediate blink.
    void triggerBlink();

    /// @brief Returns true if currently mid-blink.
    bool isBlinking() const;

    // --- Saccade ---

    /// @brief Enables or disables microsaccade noise.
    void setSaccadeEnabled(bool enabled);

    // --- Output ---

    /// @brief Gets the weight for a specific blend shape (0 if not set).
    float getWeight(const std::string& shapeName) const;

    /// @brief Gets all non-zero eye blend shape weights.
    const std::unordered_map<std::string, float>& getWeights() const;

private:
    void updateBlink(float deltaTime);
    void updateGaze(float deltaTime);
    void updateSaccade(float deltaTime);
    void computeWeights();
    float randomFloat(float minVal, float maxVal);

    // Blink
    bool m_blinkEnabled = true;
    float m_blinkTimer;
    float m_blinkProgress = -1.0f;          ///< Negative = not blinking.
    float m_blinkMinInterval = 2.0f;
    float m_blinkMaxInterval = 6.0f;
    float m_blinkWeight = 0.0f;
    bool m_pendingDoubleBlink = false;
    float m_doubleBlinkDelay = 0.0f;

    static constexpr float BLINK_DURATION = 0.15f;
    static constexpr float BLINK_CLOSE_POINT = 0.53f;  ///< Fraction of duration at which eyelid is fully closed.
    static constexpr float DOUBLE_BLINK_CHANCE = 0.15f;
    static constexpr float DOUBLE_BLINK_GAP = 0.2f;    ///< Seconds between double-blink pairs.

    // Gaze
    bool m_gazeEnabled = false;
    glm::vec3 m_gazeDirection = glm::vec3(0.0f, 0.0f, -1.0f);
    float m_gazeSpeed = 300.0f;
    float m_horizontalLimit = 35.0f;
    float m_verticalLimit = 25.0f;
    float m_currentGazeH = 0.0f;
    float m_currentGazeV = 0.0f;
    float m_targetGazeH = 0.0f;
    float m_targetGazeV = 0.0f;

    // Saccade
    bool m_saccadeEnabled = true;
    float m_saccadeTimer = 0.0f;
    float m_saccadeH = 0.0f;
    float m_saccadeV = 0.0f;

    // Output
    std::unordered_map<std::string, float> m_weights;

    // RNG
    std::mt19937 m_rng;
};

} // namespace Vestige
