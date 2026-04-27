// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file input_bindings_wire.h
/// @brief JSON wire format for input bindings (Phase 10.9 Slice 9 I2).
///
/// Phase 10 settings persistence layered the binding wire format inside
/// `engine/core/settings.cpp`, mixing input-domain concerns with the
/// settings-orchestration code. This header relocates the binding-level
/// data shape and its `toJson` / `fromJson` helpers to the input subsystem
/// where they belong; `engine/core/settings.{h,cpp}` keeps `ControlsSettings`
/// as the surrounding orchestrator and delegates the per-binding work here.
///
/// PHASE10_SETTINGS_DESIGN.md slice 13.4 originally specified the goal state
/// (`InputActionMap::toJson` / `fromJson` living under `engine/input/`); this
/// is the data-shape half of that move. The runtime `InputActionMap` ↔
/// `ActionBindingWire` translation lives in `settings_apply.cpp` and is not
/// a serialisation step.
#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string>

namespace Vestige
{

/// @brief A single physical-input binding in the wire format.
///
/// `device` is one of `"keyboard"`, `"mouse"`, `"gamepad"`, `"none"`.
/// `scancode` is a GLFW scan code for keyboards (Godot's
/// `physical_keycode` convention — preserves WASD across AZERTY /
/// Dvorak), a GLFW mouse-button index for Mouse, a GLFW gamepad-button
/// index for Gamepad, and ignored for None.
struct InputBindingWire
{
    std::string device   = "none";
    int         scancode = -1;

    bool operator==(const InputBindingWire& o) const
    {
        return device == o.device && scancode == o.scancode;
    }
    bool operator!=(const InputBindingWire& o) const { return !(*this == o); }
};

/// @brief One action + its three binding slots. `id` matches a
///        registered `InputAction::id`; unknown ids are dropped on
///        load with a logged warning.
struct ActionBindingWire
{
    std::string        id;
    InputBindingWire   primary;
    InputBindingWire   secondary;
    InputBindingWire   gamepad;

    bool operator==(const ActionBindingWire& o) const
    {
        return id == o.id
            && primary   == o.primary
            && secondary == o.secondary
            && gamepad   == o.gamepad;
    }
    bool operator!=(const ActionBindingWire& o) const { return !(*this == o); }
};

/// @brief Pure binding ↔ JSON helpers. Symmetric: `bindingFromJson(bindingToJson(b)) == b`
///        for any well-formed `b`. Missing fields take the wire defaults.
nlohmann::json   bindingToJson(const InputBindingWire& b);
InputBindingWire bindingFromJson(const nlohmann::json& j);

/// @brief Pure action-binding ↔ JSON helpers. Reads/writes `id`, `primary`,
///        `secondary`, `gamepad` keys.
nlohmann::json    actionBindingToJson(const ActionBindingWire& ab);
ActionBindingWire actionBindingFromJson(const nlohmann::json& j);

} // namespace Vestige
