// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file light_component.h
/// @brief Light components for entities.
#pragma once

#include "scene/component.h"
#include "renderer/light.h"

#include <memory>

namespace Vestige
{

/// @brief Attaches a directional light to an entity.
class DirectionalLightComponent : public Component
{
public:
    DirectionalLight light;

    std::unique_ptr<Component> clone() const override
    {
        auto copy = std::make_unique<DirectionalLightComponent>();
        copy->light = light;
        copy->setEnabled(isEnabled());
        return copy;
    }
};

/// @brief Attaches a point light to an entity (uses entity's world position).
class PointLightComponent : public Component
{
public:
    PointLight light;

    std::unique_ptr<Component> clone() const override
    {
        auto copy = std::make_unique<PointLightComponent>();
        copy->light = light;
        copy->setEnabled(isEnabled());
        return copy;
    }
};

/// @brief Attaches a spot light to an entity (uses entity's world position/direction).
class SpotLightComponent : public Component
{
public:
    SpotLight light;

    std::unique_ptr<Component> clone() const override
    {
        auto copy = std::make_unique<SpotLightComponent>();
        copy->light = light;
        copy->setEnabled(isEnabled());
        return copy;
    }
};

/// @brief Auto-generates a point light from an entity's emissive material properties.
/// Attach to an entity with a MeshRenderer whose material has emissive values > 0.
class EmissiveLightComponent : public Component
{
public:
    /// @brief Radius of the generated point light.
    float lightRadius = 5.0f;

    /// @brief Intensity multiplier for the generated light.
    float lightIntensity = 1.0f;

    /// @brief Override color. If zero (default), derives from material emissive color.
    glm::vec3 overrideColor = glm::vec3(0.0f);

    std::unique_ptr<Component> clone() const override
    {
        auto copy = std::make_unique<EmissiveLightComponent>();
        copy->lightRadius = lightRadius;
        copy->lightIntensity = lightIntensity;
        copy->overrideColor = overrideColor;
        copy->setEnabled(isEnabled());
        return copy;
    }
};

} // namespace Vestige
