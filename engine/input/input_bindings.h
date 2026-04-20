// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file input_bindings.h
/// @brief Phase 10 accessibility — fully remappable controls for
///        keyboard, mouse, and gamepad.
///
/// Action-map architecture (Unity Input System / Unreal Enhanced
/// Input / Godot InputMap): game code talks in high-level verbs
/// ("Jump", "Fire", "MoveForward") and the binding layer maps those
/// to physical inputs. Each action carries up to three bindings —
/// a primary keyboard / mouse binding, a secondary keyboard / mouse
/// binding, and a gamepad binding — so a user can remap any of them
/// independently without losing the other two.
///
/// Scope of this file: pure data model + free-function queries.
/// `InputManager` layers a thin shim on top to poll GLFW; tests
/// cover the free-function path with a dependency-injected binding
/// checker so they run without a GL / GLFW context.
///
/// Design references:
///  - Unity 2023 Input System documentation (action-asset concept).
///  - Unreal Enhanced Input (IA_… assets + IMC_… contexts).
///  - Godot 4 InputMap singleton.
///  - Microsoft / Xbox accessibility guidelines for binding
///    presentation (three-column rebind UI, per-device conflict
///    pill, one-shot reset-to-default).
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief The physical device a binding targets.
enum class InputDevice
{
    None,       ///< Unbound placeholder — matches nothing.
    Keyboard,   ///< GLFW key code (e.g. `GLFW_KEY_W`).
    Mouse,      ///< GLFW mouse button (e.g. `GLFW_MOUSE_BUTTON_LEFT`).
    Gamepad,    ///< GLFW gamepad button (e.g. `GLFW_GAMEPAD_BUTTON_A`).
};

/// @brief A single physical input.
///
/// `code` is interpreted in the context of `device` — a GLFW key
/// code for Keyboard, a GLFW mouse-button index for Mouse, a GLFW
/// gamepad-button index for Gamepad. `None` / `code == -1` means
/// "not bound".
struct InputBinding
{
    InputDevice device = InputDevice::None;
    int code = -1;

    bool isBound() const { return device != InputDevice::None && code >= 0; }

    /// @brief Value equality — two bindings match iff device + code match.
    bool operator==(const InputBinding& other) const
    {
        return device == other.device && code == other.code;
    }
    bool operator!=(const InputBinding& other) const { return !(*this == other); }

    /// @brief Factory helpers — keep call sites self-documenting.
    static InputBinding key(int glfwKey)         { return {InputDevice::Keyboard, glfwKey}; }
    static InputBinding mouse(int glfwButton)    { return {InputDevice::Mouse,    glfwButton}; }
    static InputBinding gamepad(int glfwButton)  { return {InputDevice::Gamepad,  glfwButton}; }
    static InputBinding none()                   { return {InputDevice::None,     -1}; }
};

/// @brief A named game verb with up to three physical bindings.
///
/// `id` is the stable string used by game code (`isActionDown("Jump")`).
/// `label` / `category` are user-facing — localizable later. The three
/// binding slots are conventional, not magical: they're treated
/// symmetrically by the query logic, and the rebind UI simply shows
/// three columns.
struct InputAction
{
    std::string id;
    std::string label;
    std::string category;

    InputBinding primary;
    InputBinding secondary;
    InputBinding gamepad;

    /// @brief True iff any of the three slots would fire on `binding`.
    bool matches(const InputBinding& binding) const;
};

/// @brief Registry of every action the game exposes to rebinding.
///
/// Maintains the authored defaults in parallel with the current
/// bindings so `resetToDefaults()` restores the shipped mapping
/// without the caller having to reconstruct the whole map.
class InputActionMap
{
public:
    InputActionMap() = default;

    /// @brief Registers a new action with its default bindings.
    ///
    /// If an action with the same id already exists it is replaced
    /// and its defaults are overwritten — useful for hot-reload in
    /// the editor. Returns a reference to the stored action so the
    /// caller can chain further configuration.
    InputAction& addAction(const InputAction& action);

    /// @brief Flat list of every registered action in insertion order.
    const std::vector<InputAction>& actions() const { return m_actions; }

    /// @brief Mutable lookup by id, nullptr if not found.
    InputAction* findAction(const std::string& id);

    /// @brief Const lookup by id, nullptr if not found.
    const InputAction* findAction(const std::string& id) const;

    /// @brief Reverse lookup — the first action whose any-slot matches
    ///        @a binding, or nullptr.
    const InputAction* findActionBoundTo(const InputBinding& binding) const;

    /// @brief Returns the ids of every action bound to @a binding,
    ///        excluding @a excludeActionId.
    ///
    /// Used by the rebind UI to show "this key is already assigned to
    /// X" warnings. The excluded id is typically the action being
    /// rebound so it doesn't flag itself.
    std::vector<std::string> findConflicts(const InputBinding& binding,
                                           const std::string& excludeActionId = {}) const;

    /// @brief Sets the primary / secondary / gamepad slot for @a id.
    /// @returns true if the action exists and the slot was updated.
    bool setPrimary(const std::string& id, const InputBinding& binding);
    bool setSecondary(const std::string& id, const InputBinding& binding);
    bool setGamepad(const std::string& id, const InputBinding& binding);

    /// @brief Clears a single slot on @a id.
    /// @returns true if the action exists.
    bool clearSlot(const std::string& id, int slotIndex);

    /// @brief Restores every action's bindings to what was supplied
    ///        on registration.
    void resetToDefaults();

    /// @brief Restores a single action's bindings to its registered
    ///        defaults. Returns true if the action exists.
    bool resetActionToDefaults(const std::string& id);

private:
    std::vector<InputAction> m_actions;
    std::vector<InputAction> m_defaults;  ///< Parallel — snapshot at addAction().
};

/// @brief Returns a stable, human-readable display name for a binding.
///
/// Keyboard: "W", "Space", "Left Shift", "Escape". Mouse: "Left
/// Mouse", "Right Mouse", "Middle Mouse", "Mouse 4". Gamepad: "A",
/// "B", "LB", "RT", "D-Pad Up". Unbound: "—".
///
/// Intended for the rebind UI's "bound to" label column. Does not
/// localize — the string table (Phase 10 Localization) will translate
/// the returned token.
std::string bindingDisplayLabel(const InputBinding& binding);

/// @brief Pure-function query: is an action currently pressed?
///
/// @a isBindingDown is a predicate the caller supplies (typically
/// `InputManager::isBindingDown`). Fully-dependency-injected so
/// tests run without GLFW. Returns true if *any* of the action's
/// three slots is bound and currently down.
bool isActionDown(const InputActionMap& map,
                  const std::string& actionId,
                  const std::function<bool(const InputBinding&)>& isBindingDown);

} // namespace Vestige
