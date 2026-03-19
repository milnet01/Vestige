/// @file camera.h
/// @brief First-person camera with keyboard and mouse controls.
#pragma once

#include <glm/glm.hpp>

namespace Vestige
{

/// @brief Default camera configuration values.
constexpr float DEFAULT_YAW = -90.0f;
constexpr float DEFAULT_PITCH = 0.0f;
constexpr float DEFAULT_SPEED = 2.5f;
constexpr float DEFAULT_SENSITIVITY = 0.1f;
constexpr float DEFAULT_FOV = 45.0f;

/// @brief First-person fly camera with WASD movement and mouse look.
class Camera
{
public:
    /// @brief Creates a camera at the given position.
    /// @param position Starting world position.
    /// @param yaw Initial horizontal angle in degrees.
    /// @param pitch Initial vertical angle in degrees.
    explicit Camera(
        const glm::vec3& position = glm::vec3(0.0f, 0.0f, 3.0f),
        float yaw = DEFAULT_YAW,
        float pitch = DEFAULT_PITCH
    );

    /// @brief Gets the view matrix for rendering.
    /// @return The 4x4 view matrix.
    glm::mat4 getViewMatrix() const;

    /// @brief Gets the reverse-Z infinite far projection matrix for rendering.
    /// @param aspectRatio Window width / height.
    /// @return The 4x4 perspective projection matrix.
    glm::mat4 getProjectionMatrix(float aspectRatio) const;

    /// @brief Gets a standard projection matrix for frustum culling.
    /// Uses a finite far plane so frustum plane extraction works correctly.
    /// @param aspectRatio Window width / height.
    /// @return The 4x4 perspective projection matrix (standard depth).
    glm::mat4 getCullingProjectionMatrix(float aspectRatio) const;

    /// @brief Moves the camera forward/backward and left/right.
    /// @param forward Forward movement amount (negative = backward).
    /// @param right Right movement amount (negative = left).
    /// @param up Up movement amount (negative = down).
    /// @param deltaTime Time since last frame for frame-rate independent movement.
    void move(float forward, float right, float up, float deltaTime);

    /// @brief Rotates the camera based on mouse movement.
    /// @param xOffset Horizontal mouse movement.
    /// @param yOffset Vertical mouse movement.
    void rotate(float xOffset, float yOffset);

    /// @brief Adjusts the field of view (for scroll zoom).
    /// @param offset Scroll amount.
    void adjustFov(float offset);

    /// @brief Gets the camera's world position.
    glm::vec3 getPosition() const;

    /// @brief Gets the camera's forward direction.
    glm::vec3 getFront() const;

    /// @brief Gets the current field of view in degrees.
    float getFov() const;

    /// @brief Sets the movement speed.
    void setSpeed(float speed);

    /// @brief Sets the mouse sensitivity.
    void setSensitivity(float sensitivity);

    /// @brief Sets the camera position directly.
    void setPosition(const glm::vec3& position);

    /// @brief Sets the yaw angle directly (in degrees) and recalculates vectors.
    void setYaw(float yaw);

    /// @brief Sets the pitch angle directly (in degrees) and recalculates vectors.
    void setPitch(float pitch);

private:
    void updateVectors();

    glm::vec3 m_position;
    glm::vec3 m_front;
    glm::vec3 m_up;
    glm::vec3 m_right;
    glm::vec3 m_worldUp;

    float m_yaw;
    float m_pitch;
    float m_speed;
    float m_sensitivity;
    float m_fov;
};

} // namespace Vestige
