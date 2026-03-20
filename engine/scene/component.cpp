/// @file component.cpp
/// @brief Component base class implementation.
#include "scene/component.h"

namespace Vestige
{

std::atomic<uint32_t> ComponentTypeId::s_nextId{0};

void Component::update(float /*deltaTime*/)
{
    // Default: do nothing — subclasses override as needed
}

Entity* Component::getOwner() const
{
    return m_owner;
}

void Component::setOwner(Entity* owner)
{
    m_owner = owner;
}

void Component::setEnabled(bool isEnabled)
{
    m_isEnabled = isEnabled;
}

bool Component::isEnabled() const
{
    return m_isEnabled;
}

std::unique_ptr<Component> Component::clone() const
{
    return nullptr;
}

} // namespace Vestige
