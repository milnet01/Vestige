/// @file event_bus.h
/// @brief Publish/subscribe event system for decoupled subsystem communication.
#pragma once

#include "core/event.h"

#include <cstdint>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include <memory>

namespace Vestige
{

/// @brief Opaque subscription handle returned by subscribe(), used to unsubscribe.
using SubscriptionId = uint32_t;

/// @brief A lightweight publish/subscribe event bus.
/// @details Subsystems subscribe to specific event types. When an event is
///          published, all subscribers for that type are notified synchronously.
///          Subscriptions can be removed via unsubscribe() using the returned ID.
class EventBus
{
public:
    /// @brief Subscribes a callback to a specific event type.
    /// @tparam T The event type to listen for (must derive from Event).
    /// @param callback Function to call when the event is published.
    /// @return A subscription ID that can be passed to unsubscribe().
    template <typename T>
    SubscriptionId subscribe(std::function<void(const T&)> callback)
    {
        SubscriptionId id = m_nextId++;
        auto wrapper = [callback](const Event& event)
        {
            callback(static_cast<const T&>(event));
        };
        m_listeners[std::type_index(typeid(T))].push_back({id, wrapper});
        return id;
    }

    /// @brief Removes a specific subscription by its ID.
    /// @param id The subscription ID returned by subscribe().
    /// @return True if the subscription was found and removed.
    bool unsubscribe(SubscriptionId id);

    /// @brief Publishes an event immediately to all subscribers.
    /// @tparam T The event type (must derive from Event).
    /// @param event The event instance to publish.
    template <typename T>
    void publish(const T& event)
    {
        auto it = m_listeners.find(std::type_index(typeid(T)));
        if (it != m_listeners.end())
        {
            for (const auto& entry : it->second)
            {
                entry.callback(event);
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

    struct ListenerEntry
    {
        SubscriptionId id;
        EventCallback callback;
    };

    std::unordered_map<std::type_index, std::vector<ListenerEntry>> m_listeners;
    SubscriptionId m_nextId = 1;
};

} // namespace Vestige
