// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

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
///
///          Phase 10.9 Pe4 — re-entrancy is supported (a callback may
///          publish further events, subscribe new listeners, or unsubscribe
///          itself or others). The per-publish vector copy that the
///          previous implementation used has been replaced by a depth
///          counter + pending-adds queue + tombstone-and-compact: the
///          dispatch loop holds a `const std::vector<ListenerEntry>&`
///          reference to the live storage and walks `[0, startSize)`
///          captured at entry. New subscribers added mid-dispatch land
///          in `m_pendingAdds` and are drained when the outermost
///          publish unwinds; unsubscribed entries are tombstoned (marked
///          `!valid`) and compacted at the same drain. No heap copy on
///          the per-event hot path.
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
        const auto key = std::type_index(typeid(T));
        if (m_dispatchDepth > 0)
        {
            // Pe4 — appending to the live vector mid-dispatch could
            // reallocate and invalidate the by-ref iteration window;
            // queue instead and drain when dispatch unwinds.
            m_pendingAdds[key].push_back({id, std::move(wrapper), true});
        }
        else
        {
            m_listeners[key].push_back({id, std::move(wrapper), true});
        }
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
        if (it == m_listeners.end()) return;

        // Pe4 — by-ref iteration. Snapshot the size at entry so a callback
        // that subscribes more listeners (which land in m_pendingAdds, not
        // here) doesn't extend the loop. Tombstoned entries (unsubscribed
        // mid-dispatch) skip via the `valid` flag.
        const std::vector<ListenerEntry>& listeners = it->second;
        const size_t snapshot = listeners.size();
        ++m_dispatchDepth;
        for (size_t i = 0; i < snapshot; ++i)
        {
            const ListenerEntry& entry = listeners[i];
            if (entry.valid)
            {
                entry.callback(event);
            }
        }
        --m_dispatchDepth;
        if (m_dispatchDepth == 0)
        {
            drainPending();
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
        bool valid = true;  ///< Pe4 tombstone — unsubscribed mid-dispatch.
    };

    /// @brief Pe4 — drain queued adds and remove tombstones.
    void drainPending();

    std::unordered_map<std::type_index, std::vector<ListenerEntry>> m_listeners;
    std::unordered_map<std::type_index, std::vector<ListenerEntry>> m_pendingAdds;
    SubscriptionId m_nextId = 1;
    int m_dispatchDepth = 0;
};

} // namespace Vestige
