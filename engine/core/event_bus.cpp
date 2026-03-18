/// @file event_bus.cpp
/// @brief EventBus implementation.
#include "core/event_bus.h"

namespace Vestige
{

bool EventBus::unsubscribe(SubscriptionId id)
{
    for (auto& [typeIdx, entries] : m_listeners)
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
}

size_t EventBus::getListenerCount() const
{
    size_t count = 0;
    for (const auto& [typeIdx, entries] : m_listeners)
    {
        count += entries.size();
    }
    return count;
}

} // namespace Vestige
