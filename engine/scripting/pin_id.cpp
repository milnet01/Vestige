// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file pin_id.cpp
/// @brief PinId intern table implementation. See pin_id.h for the design.
#include "scripting/pin_id.h"

#include <unordered_map>
#include <vector>

namespace Vestige
{

namespace
{

/// @brief Forward map: name → id. Sized to dedup repeated lookups in O(1).
std::unordered_map<std::string, PinId>& nameToIdTable()
{
    static std::unordered_map<std::string, PinId> table;
    return table;
}

/// @brief Reverse map: id → name. Indexed directly by PinId (id 0 reserved).
std::vector<std::string>& idToNameTable()
{
    // Index 0 is reserved for INVALID_PIN_ID — leave it as an empty string
    // so pinName(INVALID_PIN_ID) returns "" without an out-of-range branch.
    static std::vector<std::string> table = {std::string{}};
    return table;
}

} // namespace

PinId internPin(const std::string& name)
{
    if (name.empty())
    {
        return INVALID_PIN_ID;
    }

    auto& n2i = nameToIdTable();
    auto it = n2i.find(name);
    if (it != n2i.end())
    {
        return it->second;
    }

    auto& i2n = idToNameTable();
    const auto id = static_cast<PinId>(i2n.size());
    i2n.push_back(name);
    n2i.emplace(name, id);
    return id;
}

const std::string& pinName(PinId id)
{
    const auto& i2n = idToNameTable();
    if (id < i2n.size())
    {
        return i2n[id];
    }
    static const std::string empty;
    return empty;
}

size_t internedPinCount()
{
    // Subtract 1 for the reserved index 0.
    return idToNameTable().size() - 1;
}

} // namespace Vestige
