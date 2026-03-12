/// @file event_bus.cpp
/// @brief EventBus implementation.
#include "core/event_bus.h"

namespace Vestige
{

void EventBus::clearAll()
{
    m_listeners.clear();
}

size_t EventBus::getListenerCount() const
{
    size_t count = 0;
    for (const auto& pair : m_listeners)
    {
        count += pair.second.size();
    }
    return count;
}

} // namespace Vestige
