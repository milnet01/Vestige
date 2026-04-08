/// @file nav_agent_component.h
/// @brief Entity component for navigation agents.
#pragma once

#include "scene/component.h"

#include <glm/glm.hpp>

#include <vector>

namespace Vestige
{

/// @brief Attaches navigation agent parameters to an entity.
///
/// The NavigationSystem uses these parameters for pathfinding queries
/// and path-following updates.
class NavAgentComponent : public Component
{
public:
    NavAgentComponent() = default;

    /// @brief Agent collision radius (meters).
    float radius = 0.4f;

    /// @brief Agent height (meters).
    float height = 1.8f;

    /// @brief Maximum movement speed (meters/second).
    float maxSpeed = 3.5f;

    /// @brief Maximum acceleration (meters/second^2).
    float maxAcceleration = 8.0f;

    /// @brief Current path waypoints (set by NavigationSystem).
    std::vector<glm::vec3> currentPath;

    /// @brief Current waypoint index in the path.
    int currentPathIndex = 0;

    /// @brief Whether the agent has reached its destination.
    bool hasReachedDestination() const;

    /// @brief Creates a deep copy of this component.
    std::unique_ptr<Component> clone() const override;
};

} // namespace Vestige
