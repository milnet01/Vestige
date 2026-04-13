/// @file script_events.h
/// @brief Scripting-specific EventBus event types.
///
/// These events are published by visual script nodes (e.g. PublishEvent)
/// and consumed by script event nodes (e.g. OnCustomEvent) via the engine
/// EventBus. They are deliberately kept out of core/system_events.h because
/// they depend on ScriptValue.
#pragma once

#include "core/event.h"
#include "scripting/script_value.h"

#include <string>

namespace Vestige
{

/// @brief User-defined event fired by the PublishEvent node.
///
/// Carries a string name and a single payload value. OnCustomEvent nodes
/// filter by name and expose the payload on a data output pin.
struct ScriptCustomEvent : public Event
{
    std::string name;
    ScriptValue payload;

    ScriptCustomEvent(const std::string& n, const ScriptValue& p)
        : name(n), payload(p)
    {
    }
};

} // namespace Vestige
