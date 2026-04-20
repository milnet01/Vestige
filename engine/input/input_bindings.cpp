// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file input_bindings.cpp
/// @brief `InputActionMap` implementation + GLFW → display-name table.

#include "input/input_bindings.h"

#include <GLFW/glfw3.h>

#include <algorithm>

namespace Vestige
{

// -- InputAction --------------------------------------------------

bool InputAction::matches(const InputBinding& binding) const
{
    if (!binding.isBound()) return false;
    return primary == binding || secondary == binding || gamepad == binding;
}

// -- InputActionMap ----------------------------------------------

InputAction& InputActionMap::addAction(const InputAction& action)
{
    // Replace existing entry (and its default snapshot) if id already
    // registered — editor hot-reload and runtime re-registration path.
    auto findById = [&](auto& vec) {
        return std::find_if(vec.begin(), vec.end(),
            [&](const InputAction& a) { return a.id == action.id; });
    };

    auto it = findById(m_actions);
    if (it != m_actions.end())
    {
        *it = action;
    }
    else
    {
        m_actions.push_back(action);
    }

    auto dit = findById(m_defaults);
    if (dit != m_defaults.end())
    {
        *dit = action;
    }
    else
    {
        m_defaults.push_back(action);
    }

    // Return ref to the canonical (non-default) entry.
    return *findById(m_actions);
}

InputAction* InputActionMap::findAction(const std::string& id)
{
    auto it = std::find_if(m_actions.begin(), m_actions.end(),
        [&](const InputAction& a) { return a.id == id; });
    return it == m_actions.end() ? nullptr : &*it;
}

const InputAction* InputActionMap::findAction(const std::string& id) const
{
    auto it = std::find_if(m_actions.begin(), m_actions.end(),
        [&](const InputAction& a) { return a.id == id; });
    return it == m_actions.end() ? nullptr : &*it;
}

const InputAction* InputActionMap::findActionBoundTo(const InputBinding& binding) const
{
    if (!binding.isBound()) return nullptr;
    for (const auto& action : m_actions)
    {
        if (action.matches(binding)) return &action;
    }
    return nullptr;
}

std::vector<std::string> InputActionMap::findConflicts(const InputBinding& binding,
                                                       const std::string& excludeActionId) const
{
    std::vector<std::string> conflicts;
    if (!binding.isBound()) return conflicts;
    for (const auto& action : m_actions)
    {
        if (action.id == excludeActionId) continue;
        if (action.matches(binding)) conflicts.push_back(action.id);
    }
    return conflicts;
}

bool InputActionMap::setPrimary(const std::string& id, const InputBinding& binding)
{
    if (auto* a = findAction(id)) { a->primary = binding; return true; }
    return false;
}

bool InputActionMap::setSecondary(const std::string& id, const InputBinding& binding)
{
    if (auto* a = findAction(id)) { a->secondary = binding; return true; }
    return false;
}

bool InputActionMap::setGamepad(const std::string& id, const InputBinding& binding)
{
    if (auto* a = findAction(id)) { a->gamepad = binding; return true; }
    return false;
}

bool InputActionMap::clearSlot(const std::string& id, int slotIndex)
{
    auto* a = findAction(id);
    if (!a) return false;
    switch (slotIndex)
    {
        case 0: a->primary   = InputBinding::none(); return true;
        case 1: a->secondary = InputBinding::none(); return true;
        case 2: a->gamepad   = InputBinding::none(); return true;
        default: return false;
    }
}

void InputActionMap::resetToDefaults()
{
    m_actions = m_defaults;
}

bool InputActionMap::resetActionToDefaults(const std::string& id)
{
    auto dit = std::find_if(m_defaults.begin(), m_defaults.end(),
        [&](const InputAction& a) { return a.id == id; });
    if (dit == m_defaults.end()) return false;
    auto* a = findAction(id);
    if (!a) return false;
    *a = *dit;
    return true;
}

// -- Display labels ----------------------------------------------

namespace
{
// Names for keys that GLFW exposes by their printable character; this
// covers the letters/digits glfwGetKeyName can't translate without a
// layout (on a headless build). Anything not in the table falls back
// to a short hex/GLFW token.
const char* keyboardName(int code)
{
    switch (code)
    {
        // Alphanumerics
        case GLFW_KEY_A: return "A"; case GLFW_KEY_B: return "B"; case GLFW_KEY_C: return "C";
        case GLFW_KEY_D: return "D"; case GLFW_KEY_E: return "E"; case GLFW_KEY_F: return "F";
        case GLFW_KEY_G: return "G"; case GLFW_KEY_H: return "H"; case GLFW_KEY_I: return "I";
        case GLFW_KEY_J: return "J"; case GLFW_KEY_K: return "K"; case GLFW_KEY_L: return "L";
        case GLFW_KEY_M: return "M"; case GLFW_KEY_N: return "N"; case GLFW_KEY_O: return "O";
        case GLFW_KEY_P: return "P"; case GLFW_KEY_Q: return "Q"; case GLFW_KEY_R: return "R";
        case GLFW_KEY_S: return "S"; case GLFW_KEY_T: return "T"; case GLFW_KEY_U: return "U";
        case GLFW_KEY_V: return "V"; case GLFW_KEY_W: return "W"; case GLFW_KEY_X: return "X";
        case GLFW_KEY_Y: return "Y"; case GLFW_KEY_Z: return "Z";
        case GLFW_KEY_0: return "0"; case GLFW_KEY_1: return "1"; case GLFW_KEY_2: return "2";
        case GLFW_KEY_3: return "3"; case GLFW_KEY_4: return "4"; case GLFW_KEY_5: return "5";
        case GLFW_KEY_6: return "6"; case GLFW_KEY_7: return "7"; case GLFW_KEY_8: return "8";
        case GLFW_KEY_9: return "9";

        // Control / navigation
        case GLFW_KEY_SPACE:         return "Space";
        case GLFW_KEY_ENTER:         return "Enter";
        case GLFW_KEY_TAB:           return "Tab";
        case GLFW_KEY_ESCAPE:        return "Escape";
        case GLFW_KEY_BACKSPACE:     return "Backspace";
        case GLFW_KEY_INSERT:        return "Insert";
        case GLFW_KEY_DELETE:        return "Delete";
        case GLFW_KEY_HOME:          return "Home";
        case GLFW_KEY_END:           return "End";
        case GLFW_KEY_PAGE_UP:       return "Page Up";
        case GLFW_KEY_PAGE_DOWN:     return "Page Down";

        case GLFW_KEY_LEFT:          return "Left";
        case GLFW_KEY_RIGHT:         return "Right";
        case GLFW_KEY_UP:            return "Up";
        case GLFW_KEY_DOWN:          return "Down";

        case GLFW_KEY_LEFT_SHIFT:    return "Left Shift";
        case GLFW_KEY_RIGHT_SHIFT:   return "Right Shift";
        case GLFW_KEY_LEFT_CONTROL:  return "Left Ctrl";
        case GLFW_KEY_RIGHT_CONTROL: return "Right Ctrl";
        case GLFW_KEY_LEFT_ALT:      return "Left Alt";
        case GLFW_KEY_RIGHT_ALT:     return "Right Alt";
        case GLFW_KEY_LEFT_SUPER:    return "Left Super";
        case GLFW_KEY_RIGHT_SUPER:   return "Right Super";
        case GLFW_KEY_CAPS_LOCK:     return "Caps Lock";

        // Punctuation
        case GLFW_KEY_MINUS:         return "-";
        case GLFW_KEY_EQUAL:         return "=";
        case GLFW_KEY_LEFT_BRACKET:  return "[";
        case GLFW_KEY_RIGHT_BRACKET: return "]";
        case GLFW_KEY_SEMICOLON:     return ";";
        case GLFW_KEY_APOSTROPHE:    return "'";
        case GLFW_KEY_GRAVE_ACCENT:  return "`";
        case GLFW_KEY_COMMA:         return ",";
        case GLFW_KEY_PERIOD:        return ".";
        case GLFW_KEY_SLASH:         return "/";
        case GLFW_KEY_BACKSLASH:     return "\\";

        // Function keys
        case GLFW_KEY_F1:  return "F1";  case GLFW_KEY_F2:  return "F2";
        case GLFW_KEY_F3:  return "F3";  case GLFW_KEY_F4:  return "F4";
        case GLFW_KEY_F5:  return "F5";  case GLFW_KEY_F6:  return "F6";
        case GLFW_KEY_F7:  return "F7";  case GLFW_KEY_F8:  return "F8";
        case GLFW_KEY_F9:  return "F9";  case GLFW_KEY_F10: return "F10";
        case GLFW_KEY_F11: return "F11"; case GLFW_KEY_F12: return "F12";

        default: return nullptr;
    }
}

const char* mouseName(int code)
{
    switch (code)
    {
        case GLFW_MOUSE_BUTTON_LEFT:   return "Left Mouse";
        case GLFW_MOUSE_BUTTON_RIGHT:  return "Right Mouse";
        case GLFW_MOUSE_BUTTON_MIDDLE: return "Middle Mouse";
        case GLFW_MOUSE_BUTTON_4:      return "Mouse 4";
        case GLFW_MOUSE_BUTTON_5:      return "Mouse 5";
        case GLFW_MOUSE_BUTTON_6:      return "Mouse 6";
        case GLFW_MOUSE_BUTTON_7:      return "Mouse 7";
        case GLFW_MOUSE_BUTTON_8:      return "Mouse 8";
        default:                       return nullptr;
    }
}

const char* gamepadName(int code)
{
    // GLFW's gamepad button enum follows the Xbox layout; PlayStation
    // users see "A" ~ Cross etc. in the rebind UI and that is
    // intentionally the documented translation (see GLFW gamepad docs).
    switch (code)
    {
        case GLFW_GAMEPAD_BUTTON_A:             return "A";
        case GLFW_GAMEPAD_BUTTON_B:             return "B";
        case GLFW_GAMEPAD_BUTTON_X:             return "X";
        case GLFW_GAMEPAD_BUTTON_Y:             return "Y";
        case GLFW_GAMEPAD_BUTTON_LEFT_BUMPER:   return "LB";
        case GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER:  return "RB";
        case GLFW_GAMEPAD_BUTTON_BACK:          return "Back";
        case GLFW_GAMEPAD_BUTTON_START:         return "Start";
        case GLFW_GAMEPAD_BUTTON_GUIDE:         return "Guide";
        case GLFW_GAMEPAD_BUTTON_LEFT_THUMB:    return "Left Stick";
        case GLFW_GAMEPAD_BUTTON_RIGHT_THUMB:   return "Right Stick";
        case GLFW_GAMEPAD_BUTTON_DPAD_UP:       return "D-Pad Up";
        case GLFW_GAMEPAD_BUTTON_DPAD_RIGHT:    return "D-Pad Right";
        case GLFW_GAMEPAD_BUTTON_DPAD_DOWN:     return "D-Pad Down";
        case GLFW_GAMEPAD_BUTTON_DPAD_LEFT:     return "D-Pad Left";
        default:                                return nullptr;
    }
}
} // namespace

std::string bindingDisplayLabel(const InputBinding& binding)
{
    if (!binding.isBound()) return "\u2014";  // em-dash for "unbound"

    switch (binding.device)
    {
        case InputDevice::Keyboard:
            if (const char* n = keyboardName(binding.code)) return n;
            break;
        case InputDevice::Mouse:
            if (const char* n = mouseName(binding.code)) return n;
            break;
        case InputDevice::Gamepad:
            if (const char* n = gamepadName(binding.code)) return n;
            break;
        case InputDevice::None:
            break;
    }

    // Fallback for exotic codes — keep informative for debugging.
    return "Key " + std::to_string(binding.code);
}

// -- Query --------------------------------------------------------

bool isActionDown(const InputActionMap& map,
                  const std::string& actionId,
                  const std::function<bool(const InputBinding&)>& isBindingDown)
{
    const InputAction* action = map.findAction(actionId);
    if (!action || !isBindingDown) return false;

    if (action->primary.isBound()   && isBindingDown(action->primary))   return true;
    if (action->secondary.isBound() && isBindingDown(action->secondary)) return true;
    if (action->gamepad.isBound()   && isBindingDown(action->gamepad))   return true;
    return false;
}

} // namespace Vestige
