/// @file nav_agent_component.cpp
/// @brief NavAgentComponent implementation.
#include "navigation/nav_agent_component.h"

namespace Vestige
{

bool NavAgentComponent::hasReachedDestination() const
{
    return currentPath.empty() ||
           currentPathIndex >= static_cast<int>(currentPath.size());
}

std::unique_ptr<Component> NavAgentComponent::clone() const
{
    auto copy = std::make_unique<NavAgentComponent>();
    copy->radius = radius;
    copy->height = height;
    copy->maxSpeed = maxSpeed;
    copy->maxAcceleration = maxAcceleration;
    return copy;
}

} // namespace Vestige
