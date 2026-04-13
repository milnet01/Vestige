/// @file script_common.h
/// @brief Common types for the visual scripting system: pins, connections, nodes.
#pragma once

#include "scripting/pin_id.h"
#include "scripting/script_value.h"

#include <cstdint>
#include <map>
#include <string>

namespace Vestige
{

/// @brief Pin kind: execution flow or data transfer.
enum class PinKind
{
    EXECUTION, ///< White arrow pins — control flow
    DATA       ///< Colored pins — value transfer
};

/// @brief Pin direction.
enum class PinDirection
{
    INPUT,
    OUTPUT
};

/// @brief Definition of a pin on a node type (template, not instance).
///
/// `id` is populated at registration time (NodeTypeRegistry::registerNode)
/// from `name` via internPin(). Lambdas / hot paths look up values by `id`
/// to avoid string hashing on every read; the editor still uses `name` for
/// display.
struct PinDef
{
    PinKind kind = PinKind::DATA;
    std::string name;
    ScriptDataType dataType = ScriptDataType::FLOAT;
    ScriptValue defaultValue;
    PinId id = INVALID_PIN_ID; ///< Set by NodeTypeRegistry::registerNode.
};

/// @brief A runtime pin on a specific node instance.
struct ScriptPin
{
    uint32_t id = 0;
    std::string name;
    PinKind kind = PinKind::DATA;
    PinDirection direction = PinDirection::INPUT;
    ScriptDataType dataType = ScriptDataType::FLOAT;
    ScriptValue defaultValue;
    bool connected = false;
};

/// @brief A connection between two pins in a script graph.
struct ScriptConnection
{
    uint32_t id = 0;
    uint32_t sourceNode = 0;
    std::string sourcePin;
    uint32_t targetNode = 0;
    std::string targetPin;
};

/// @brief A node instance in a script graph.
struct ScriptNodeDef
{
    uint32_t id = 0;
    std::string typeName;   ///< Registered type name (e.g. "Branch", "PlaySound")
    float posX = 0.0f;
    float posY = 0.0f;

    /// @brief Per-instance property overrides (pin defaults, config).
    /// Key is the pin name or property name.
    std::map<std::string, ScriptValue> properties;
};

} // namespace Vestige
