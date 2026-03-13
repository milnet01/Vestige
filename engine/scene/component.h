/// @file component.h
/// @brief Base component class for the entity-component system.
#pragma once

#include <cstdint>
#include <atomic>

namespace Vestige
{

class Entity;

/// @brief Generates unique type IDs for component types at compile time.
class ComponentTypeId
{
public:
    template <typename T>
    static uint32_t get()
    {
        static uint32_t id = s_nextId++;
        return id;
    }

private:
    static std::atomic<uint32_t> s_nextId;
};

/// @brief Base class for all components attached to entities.
class Component
{
public:
    virtual ~Component() = default;

    /// @brief Called once per frame to update the component.
    /// @param deltaTime Time elapsed since last frame in seconds.
    virtual void update(float deltaTime);

    /// @brief Gets the entity that owns this component.
    Entity* getOwner() const;

    /// @brief Sets the entity that owns this component (called by Entity).
    void setOwner(Entity* owner);

    /// @brief Enables or disables this component.
    void setEnabled(bool isEnabled);

    /// @brief Checks if this component is enabled.
    bool isEnabled() const;

protected:
    Entity* m_owner = nullptr;
    bool m_isEnabled = true;
};

} // namespace Vestige
