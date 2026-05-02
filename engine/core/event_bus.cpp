// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file event_bus.cpp
/// @brief EventBus implementation.
#include "core/event_bus.h"

#include <algorithm>
#include <iterator>

namespace Vestige
{

bool EventBus::unsubscribe(SubscriptionId id)
{
    // Pe4 — search live listeners first. During dispatch, mark tombstone
    // (`valid = false`) so the iterating loop's `if (entry.valid)` skips
    // it; outside dispatch, erase eagerly so the visible listener-count
    // matches the live state immediately.
    for (auto& [typeIdx, entries] : m_listeners)
    {
        for (auto it = entries.begin(); it != entries.end(); ++it)
        {
            if (it->id == id)
            {
                if (m_dispatchDepth > 0)
                {
                    it->valid = false;
                }
                else
                {
                    entries.erase(it);
                }
                return true;
            }
        }
    }
    // Pe4 — also check pending adds (a subscribe-then-unsubscribe pair
    // entirely within a dispatch should net to zero).
    for (auto& [typeIdx, entries] : m_pendingAdds)
    {
        for (auto it = entries.begin(); it != entries.end(); ++it)
        {
            if (it->id == id)
            {
                entries.erase(it);
                return true;
            }
        }
    }
    return false;
}

void EventBus::clearAll()
{
    m_listeners.clear();
    m_pendingAdds.clear();
}

size_t EventBus::getListenerCount() const
{
    size_t count = 0;
    for (const auto& [typeIdx, entries] : m_listeners)
    {
        for (const auto& entry : entries)
        {
            // Pe4 — tombstoned entries are still in the vector during
            // dispatch but should not be counted as live listeners.
            if (entry.valid) ++count;
        }
    }
    for (const auto& [typeIdx, entries] : m_pendingAdds)
    {
        count += entries.size();
    }
    return count;
}

void EventBus::drainPending()
{
    // Pe4 — runs at the outermost publish unwind. Two phases:
    //  1. Compact tombstoned entries from m_listeners.
    //  2. Append every queued add into m_listeners[T] in subscribe order.
    for (auto& [typeIdx, entries] : m_listeners)
    {
        entries.erase(
            std::remove_if(entries.begin(), entries.end(),
                [](const ListenerEntry& e) { return !e.valid; }),
            entries.end());
    }
    for (auto& [typeIdx, adds] : m_pendingAdds)
    {
        if (adds.empty()) continue;
        auto& live = m_listeners[typeIdx];
        live.insert(live.end(),
                     std::make_move_iterator(adds.begin()),
                     std::make_move_iterator(adds.end()));
        adds.clear();
    }
}

} // namespace Vestige
