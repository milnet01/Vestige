/// @file light_component.h
/// @brief Light components for entities.
#pragma once

#include "scene/component.h"
#include "renderer/light.h"

namespace Vestige
{

/// @brief Attaches a directional light to an entity.
class DirectionalLightComponent : public Component
{
public:
    DirectionalLight light;
};

/// @brief Attaches a point light to an entity (uses entity's world position).
class PointLightComponent : public Component
{
public:
    PointLight light;
};

/// @brief Attaches a spot light to an entity (uses entity's world position/direction).
class SpotLightComponent : public Component
{
public:
    SpotLight light;
};

} // namespace Vestige
