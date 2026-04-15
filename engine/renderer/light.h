// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file light.h
/// @brief Light types for Blinn-Phong shading.
#pragma once

#include <glm/glm.hpp>

namespace Vestige
{

/// @brief A directional light (like the sun) — parallel rays, no position.
struct DirectionalLight
{
    glm::vec3 direction = glm::vec3(-0.2f, -1.0f, -0.3f);
    glm::vec3 ambient   = glm::vec3(0.1f, 0.1f, 0.1f);
    glm::vec3 diffuse   = glm::vec3(0.8f, 0.8f, 0.8f);
    glm::vec3 specular  = glm::vec3(1.0f, 1.0f, 1.0f);
};

/// @brief A point light — emits in all directions from a position, with falloff.
struct PointLight
{
    glm::vec3 position = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 ambient  = glm::vec3(0.05f, 0.05f, 0.05f);
    glm::vec3 diffuse  = glm::vec3(0.8f, 0.8f, 0.8f);
    glm::vec3 specular = glm::vec3(1.0f, 1.0f, 1.0f);

    // Attenuation factors (controls how light fades with distance)
    float constant  = 1.0f;
    float linear    = 0.09f;
    float quadratic = 0.032f;

    /// @brief Cached range for the inspector (not used by the shader).
    float range = 0.0f;

    // Shadow casting
    bool castsShadow = false;
};

/// @brief A spot light — cone-shaped light from a position in a direction.
struct SpotLight
{
    glm::vec3 position  = glm::vec3(0.0f, 3.0f, 0.0f);
    glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);
    glm::vec3 ambient   = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 diffuse   = glm::vec3(1.0f, 1.0f, 1.0f);
    glm::vec3 specular  = glm::vec3(1.0f, 1.0f, 1.0f);

    // Cone angles (in cosine, not degrees — for efficiency)
    float innerCutoff = 0.9763f;  // cos(12.5 degrees)
    float outerCutoff = 0.9659f;  // cos(15.0 degrees)

    // Attenuation
    float constant  = 1.0f;
    float linear    = 0.09f;
    float quadratic = 0.032f;

    /// @brief Cached range for the inspector (not used by the shader).
    float range = 0.0f;
};

/// @brief Maximum number of each light type supported by the shader.
constexpr int MAX_POINT_LIGHTS = 8;
constexpr int MAX_SPOT_LIGHTS = 4;

} // namespace Vestige
