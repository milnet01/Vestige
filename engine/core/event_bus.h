/// @file event_bus.h
/// @brief Publish/subscribe event system for decoupled subsystem communication.
#pragma once

#include "core/event.h"

#include <functional>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include <memory>

namespace Vestige
{

/// @brief A lightweight publish/subscribe event bus.
/// @details Subsystems subscribe to specific event types. When an event is
///          published, all subscribers for that type are notified synchronously.
class EventBus
{
public:
    /// @brief Subscribes a callback to a specific event type.
    /// @tparam T The event type to listen for (must derive from Event).
    /// @param callback Function to call when the event is published.
    template <typename T>
    void subscribe(std::function<void(const T&)> callback)
    {
        auto wrapper = [callback](const Event& event)
        {
            callback(static_cast<const T&>(event));
        };
        m_listeners[std::type_index(typeid(T))].push_back(wrapper);
    }

    /// @brief Publishes an event immediately to all subscribers.
    /// @tparam T The event type (must derive from Event).
    /// @param event The event instance to publish.
    template <typename T>
    void publish(const T& event)
    {
        auto it = m_listeners.find(std::type_index(typeid(T)));
        if (it != m_listeners.end())
        {
            for (const auto& listener : it->second)
            {
                listener(event);
            }
        }
    }

    /// @brief Removes all subscribers for all event types.
    void clearAll();

    /// @brief Gets the total number of registered listeners across all types.
    /// @return The listener count.
    size_t getListenerCount() const;

private:
    using EventCallback = std::function<void(const Event&)>;
    std::unordered_map<std::type_index, std::vector<EventCallback>> m_listeners;
};

} // namespace Vestige
