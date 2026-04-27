// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file input_bindings_wire.cpp
/// @brief Implementation of the binding-level JSON wire helpers.

#include "input/input_bindings_wire.h"

#include <nlohmann/json.hpp>

namespace Vestige
{

using nlohmann::json;

json bindingToJson(const InputBindingWire& b)
{
    return json{
        {"device",   b.device},
        {"scancode", b.scancode},
    };
}

InputBindingWire bindingFromJson(const json& j)
{
    InputBindingWire b;
    b.device   = j.value("device", std::string("none"));
    b.scancode = j.value("scancode", -1);
    if (b.device == "none")
    {
        b.scancode = -1;
    }
    return b;
}

json actionBindingToJson(const ActionBindingWire& ab)
{
    return json{
        {"id",        ab.id},
        {"primary",   bindingToJson(ab.primary)},
        {"secondary", bindingToJson(ab.secondary)},
        {"gamepad",   bindingToJson(ab.gamepad)},
    };
}

ActionBindingWire actionBindingFromJson(const json& j)
{
    ActionBindingWire ab;
    if (j.contains("id") && j["id"].is_string())
    {
        ab.id = j["id"].get<std::string>();
    }
    if (j.contains("primary") && j["primary"].is_object())
    {
        ab.primary = bindingFromJson(j["primary"]);
    }
    if (j.contains("secondary") && j["secondary"].is_object())
    {
        ab.secondary = bindingFromJson(j["secondary"]);
    }
    if (j.contains("gamepad") && j["gamepad"].is_object())
    {
        ab.gamepad = bindingFromJson(j["gamepad"]);
    }
    return ab;
}

} // namespace Vestige
