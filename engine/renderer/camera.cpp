/// @file camera.cpp
/// @brief Camera implementation.
#include "renderer/camera.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace Vestige
{

Camera::Camera(const glm::vec3& position, float yaw, float pitch)
    : m_position(position)
    , m_front(0.0f, 0.0f, -1.0f)
    , m_up(0.0f, 1.0f, 0.0f)
    , m_right(1.0f, 0.0f, 0.0f)
    , m_worldUp(0.0f, 1.0f, 0.0f)
    , m_yaw(yaw)
    , m_pitch(pitch)
    , m_speed(DEFAULT_SPEED)
    , m_sensitivity(DEFAULT_SENSITIVITY)
    , m_fov(DEFAULT_FOV)
{
    updateVectors();
}

glm::mat4 Camera::getViewMatrix() const
{
    return glm::lookAt(m_position, m_position + m_front, m_up);
}

glm::mat4 Camera::getProjectionMatrix(float aspectRatio) const
{
    // Reverse-Z infinite projection: near maps to 1.0, far maps to 0.0.
    // Combined with glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE), this gives
    // near-uniform depth precision across the entire view range and eliminates
    // Z-fighting at distance. The far plane is at infinity — no clipping.
    float fovRad = glm::radians(m_fov);
    float f = 1.0f / std::tan(fovRad * 0.5f);
    float nearPlane = 0.1f;

    // Reverse-Z infinite far plane projection matrix for [0, 1] depth range.
    // Row-major layout (GLM uses column-major, so we build column by column):
    //   f/aspect  0     0      0
    //   0         f     0      0
    //   0         0     0     near
    //   0         0    -1      0
    glm::mat4 proj(0.0f);
    proj[0][0] = f / aspectRatio;
    proj[1][1] = f;
    proj[2][3] = -1.0f;         // w = -z (perspective divide)
    proj[3][2] = nearPlane;     // maps near plane to depth 1.0

    return proj;
}

glm::mat4 Camera::getCullingProjectionMatrix(float aspectRatio) const
{
    // Standard GLM perspective for frustum plane extraction.
    // Uses a large but finite far plane so all 6 frustum planes are well-defined.
    return glm::perspective(glm::radians(m_fov), aspectRatio, 0.1f, 1000.0f);
}

void Camera::move(float forward, float right, float up, float deltaTime)
{
    float velocity = m_speed * deltaTime;
    m_position += m_front * forward * velocity;
    m_position += m_right * right * velocity;
    m_position += m_worldUp * up * velocity;
}

void Camera::rotate(float xOffset, float yOffset)
{
    m_yaw += xOffset * m_sensitivity;
    m_pitch += yOffset * m_sensitivity;

    // Clamp pitch to prevent flipping
    m_pitch = std::clamp(m_pitch, -89.0f, 89.0f);

    updateVectors();
}

void Camera::adjustFov(float offset)
{
    m_fov -= offset;
    m_fov = std::clamp(m_fov, 1.0f, 120.0f);
}

glm::vec3 Camera::getPosition() const
{
    return m_position;
}

glm::vec3 Camera::getFront() const
{
    return m_front;
}

float Camera::getFov() const
{
    return m_fov;
}

void Camera::setSpeed(float speed)
{
    m_speed = speed;
}

void Camera::setSensitivity(float sensitivity)
{
    m_sensitivity = sensitivity;
}

void Camera::setPosition(const glm::vec3& position)
{
    m_position = position;
}

float Camera::getYaw() const
{
    return m_yaw;
}

float Camera::getPitch() const
{
    return m_pitch;
}

void Camera::setYaw(float yaw)
{
    m_yaw = yaw;
    updateVectors();
}

void Camera::setPitch(float pitch)
{
    m_pitch = std::clamp(pitch, -89.0f, 89.0f);
    updateVectors();
}

void Camera::updateVectors()
{
    // Calculate new front vector from yaw and pitch angles
    glm::vec3 front;
    front.x = std::cos(glm::radians(m_yaw)) * std::cos(glm::radians(m_pitch));
    front.y = std::sin(glm::radians(m_pitch));
    front.z = std::sin(glm::radians(m_yaw)) * std::cos(glm::radians(m_pitch));
    m_front = glm::normalize(front);

    // Recalculate right and up vectors
    m_right = glm::normalize(glm::cross(m_front, m_worldUp));
    m_up = glm::normalize(glm::cross(m_right, m_front));
}

} // namespace Vestige
