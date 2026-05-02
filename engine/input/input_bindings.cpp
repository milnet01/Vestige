// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file input_bindings.cpp
/// @brief `InputActionMap` implementation + GLFW → display-name table.

#include "input/input_bindings.h"

#include "core/logger.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <mutex>
#include <unordered_map>

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
        // AUDIT I5 — warn when re-registering an action whose live bindings
        // differ from the new defaults. This is the silent-nuke signature:
        // user rebinds loaded into m_actions get clobbered by a late
        // addAction() call (e.g., a code path running after Settings::load).
        // Hot-reload from the editor still works, but the warning surfaces
        // accidental re-registration.
        const InputAction& existing = *it;
        if (existing.primary   != action.primary
         || existing.secondary != action.secondary
         || existing.gamepad   != action.gamepad)
        {
            Logger::warning(
                "InputActionMap::addAction(\"" + action.id + "\"): "
                "overwriting live bindings — any user rebinds for this "
                "action are being discarded. Call addAction() before "
                "Settings::load(), or treat this re-registration as a "
                "deliberate hot-reload.");
        }
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
    // Phase 10.9 Slice 9 I4 — only slots of the same physical device can
    // physically conflict. A keyboard binding can never collide with a
    // gamepad-bound slot (different hardware), so the device gate runs
    // before the per-slot equality check. `InputBinding::operator==`
    // already includes the device field, so the explicit gate is also a
    // defence against future `==` changes that drop the device field.
    for (const auto& action : m_actions)
    {
        if (action.id == excludeActionId) continue;
        const InputBinding* slots[] = {
            &action.primary, &action.secondary, &action.gamepad
        };
        for (const InputBinding* slot : slots)
        {
            if (slot->device != binding.device) continue;
            if (*slot == binding)
            {
                conflicts.push_back(action.id);
                break;
            }
        }
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
// AUDIT I1 — keyboard display path is two-stage:
//  1. `glfwGetKeyName(GLFW_KEY_UNKNOWN, scancode)` returns the layout-aware
//     printable-character name ("W" on QWERTY, "Z" on AZERTY for the same
//     physical key). This is what we want for letters / digits / punctuation.
//  2. For non-printable keys (Space, Shift, F-row, numpad, system keys)
//     `glfwGetKeyName` returns nullptr. We fall back to a scancode-keyed
//     table built once by walking GLFW_KEY_* through `glfwGetKeyScancode`.
//
// `keyboardKeycodeToName` keeps the keycode→string mapping that the I6 work
// established (numpad, F13–F25, world keys, etc.). The lazy fallback table
// at first call inverts it via `glfwGetKeyScancode` so the same coverage
// reaches scancode-indexed callers. Without GLFW init the table stays empty
// and the debug fallback fires — display correctness is exercised at engine
// launch per the project's runtime-verification precedent.
const char* keyboardKeycodeToName(int code)
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
        // AUDIT I6 — extended function row (F13–F25 exist on full-size
        // keyboards and stream-deck mappings; rebind UI was showing
        // raw "Key 302" for these).
        case GLFW_KEY_F13: return "F13"; case GLFW_KEY_F14: return "F14";
        case GLFW_KEY_F15: return "F15"; case GLFW_KEY_F16: return "F16";
        case GLFW_KEY_F17: return "F17"; case GLFW_KEY_F18: return "F18";
        case GLFW_KEY_F19: return "F19"; case GLFW_KEY_F20: return "F20";
        case GLFW_KEY_F21: return "F21"; case GLFW_KEY_F22: return "F22";
        case GLFW_KEY_F23: return "F23"; case GLFW_KEY_F24: return "F24";
        case GLFW_KEY_F25: return "F25";

        // AUDIT I6 — system / lock keys.
        case GLFW_KEY_PAUSE:        return "Pause";
        case GLFW_KEY_PRINT_SCREEN: return "Print Screen";
        case GLFW_KEY_SCROLL_LOCK:  return "Scroll Lock";
        case GLFW_KEY_NUM_LOCK:     return "Num Lock";
        case GLFW_KEY_MENU:         return "Menu";

        // AUDIT I6 — keypad block. Keyboard-primary users were seeing
        // raw "Key 320…329" for the entire numpad in the rebind UI.
        case GLFW_KEY_KP_0:        return "Numpad 0";
        case GLFW_KEY_KP_1:        return "Numpad 1";
        case GLFW_KEY_KP_2:        return "Numpad 2";
        case GLFW_KEY_KP_3:        return "Numpad 3";
        case GLFW_KEY_KP_4:        return "Numpad 4";
        case GLFW_KEY_KP_5:        return "Numpad 5";
        case GLFW_KEY_KP_6:        return "Numpad 6";
        case GLFW_KEY_KP_7:        return "Numpad 7";
        case GLFW_KEY_KP_8:        return "Numpad 8";
        case GLFW_KEY_KP_9:        return "Numpad 9";
        case GLFW_KEY_KP_DECIMAL:  return "Numpad .";
        case GLFW_KEY_KP_DIVIDE:   return "Numpad /";
        case GLFW_KEY_KP_MULTIPLY: return "Numpad *";
        case GLFW_KEY_KP_SUBTRACT: return "Numpad -";
        case GLFW_KEY_KP_ADD:      return "Numpad +";
        case GLFW_KEY_KP_ENTER:    return "Numpad Enter";
        case GLFW_KEY_KP_EQUAL:    return "Numpad =";

        // AUDIT I6 — locale-specific keys. WORLD_1 / WORLD_2 cover the
        // ISO 105-key (non-US) and Japanese (yen / underscore) extras.
        case GLFW_KEY_WORLD_1: return "World 1";
        case GLFW_KEY_WORLD_2: return "World 2";

        default: return nullptr;
    }
}

// Lazy scancode → display-name table. Built once on first call from the
// keycode-keyed table above, mapping each known GLFW_KEY_* to its platform
// scancode. Empty if GLFW isn't initialised — display falls through to
// `glfwGetKeyName` (which itself returns nullptr without init), then to
// the debug `"Key NN"` token.
const std::unordered_map<int, const char*>& scancodeToNameMap()
{
    static std::unordered_map<int, const char*> map;
    static std::once_flag flag;
    std::call_once(flag, []() {
        // Iterate every keycode the I6 table covers; resolve each to a
        // scancode via GLFW. Out-of-range scancodes (-1) just don't get
        // recorded — the debug fallback handles them at display time.
        constexpr int kKeycodes[] = {
            GLFW_KEY_A, GLFW_KEY_B, GLFW_KEY_C, GLFW_KEY_D, GLFW_KEY_E,
            GLFW_KEY_F, GLFW_KEY_G, GLFW_KEY_H, GLFW_KEY_I, GLFW_KEY_J,
            GLFW_KEY_K, GLFW_KEY_L, GLFW_KEY_M, GLFW_KEY_N, GLFW_KEY_O,
            GLFW_KEY_P, GLFW_KEY_Q, GLFW_KEY_R, GLFW_KEY_S, GLFW_KEY_T,
            GLFW_KEY_U, GLFW_KEY_V, GLFW_KEY_W, GLFW_KEY_X, GLFW_KEY_Y,
            GLFW_KEY_Z,
            GLFW_KEY_0, GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4,
            GLFW_KEY_5, GLFW_KEY_6, GLFW_KEY_7, GLFW_KEY_8, GLFW_KEY_9,
            GLFW_KEY_SPACE, GLFW_KEY_ENTER, GLFW_KEY_TAB, GLFW_KEY_ESCAPE,
            GLFW_KEY_BACKSPACE, GLFW_KEY_INSERT, GLFW_KEY_DELETE,
            GLFW_KEY_HOME, GLFW_KEY_END, GLFW_KEY_PAGE_UP, GLFW_KEY_PAGE_DOWN,
            GLFW_KEY_LEFT, GLFW_KEY_RIGHT, GLFW_KEY_UP, GLFW_KEY_DOWN,
            GLFW_KEY_LEFT_SHIFT, GLFW_KEY_RIGHT_SHIFT,
            GLFW_KEY_LEFT_CONTROL, GLFW_KEY_RIGHT_CONTROL,
            GLFW_KEY_LEFT_ALT, GLFW_KEY_RIGHT_ALT,
            GLFW_KEY_LEFT_SUPER, GLFW_KEY_RIGHT_SUPER, GLFW_KEY_CAPS_LOCK,
            GLFW_KEY_MINUS, GLFW_KEY_EQUAL, GLFW_KEY_LEFT_BRACKET,
            GLFW_KEY_RIGHT_BRACKET, GLFW_KEY_SEMICOLON, GLFW_KEY_APOSTROPHE,
            GLFW_KEY_GRAVE_ACCENT, GLFW_KEY_COMMA, GLFW_KEY_PERIOD,
            GLFW_KEY_SLASH, GLFW_KEY_BACKSLASH,
            GLFW_KEY_F1,  GLFW_KEY_F2,  GLFW_KEY_F3,  GLFW_KEY_F4,
            GLFW_KEY_F5,  GLFW_KEY_F6,  GLFW_KEY_F7,  GLFW_KEY_F8,
            GLFW_KEY_F9,  GLFW_KEY_F10, GLFW_KEY_F11, GLFW_KEY_F12,
            GLFW_KEY_F13, GLFW_KEY_F14, GLFW_KEY_F15, GLFW_KEY_F16,
            GLFW_KEY_F17, GLFW_KEY_F18, GLFW_KEY_F19, GLFW_KEY_F20,
            GLFW_KEY_F21, GLFW_KEY_F22, GLFW_KEY_F23, GLFW_KEY_F24,
            GLFW_KEY_F25,
            GLFW_KEY_PAUSE, GLFW_KEY_PRINT_SCREEN, GLFW_KEY_SCROLL_LOCK,
            GLFW_KEY_NUM_LOCK, GLFW_KEY_MENU,
            GLFW_KEY_KP_0, GLFW_KEY_KP_1, GLFW_KEY_KP_2, GLFW_KEY_KP_3,
            GLFW_KEY_KP_4, GLFW_KEY_KP_5, GLFW_KEY_KP_6, GLFW_KEY_KP_7,
            GLFW_KEY_KP_8, GLFW_KEY_KP_9, GLFW_KEY_KP_DECIMAL,
            GLFW_KEY_KP_DIVIDE, GLFW_KEY_KP_MULTIPLY, GLFW_KEY_KP_SUBTRACT,
            GLFW_KEY_KP_ADD, GLFW_KEY_KP_ENTER, GLFW_KEY_KP_EQUAL,
            GLFW_KEY_WORLD_1, GLFW_KEY_WORLD_2,
        };
        for (int kc : kKeycodes)
        {
            const int sc = glfwGetKeyScancode(kc);
            if (sc < 0) continue;
            if (const char* n = keyboardKeycodeToName(kc))
            {
                map.emplace(sc, n);
            }
        }
    });
    return map;
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

const char* gamepadAxisName(int axis)
{
    // Slice 9 I3 — axis index → human label. Unsigned by convention
    // because the +/- half is rendered separately in `bindingDisplayLabel`.
    switch (axis)
    {
        case GLFW_GAMEPAD_AXIS_LEFT_X:        return "Left Stick X";
        case GLFW_GAMEPAD_AXIS_LEFT_Y:        return "Left Stick Y";
        case GLFW_GAMEPAD_AXIS_RIGHT_X:       return "Right Stick X";
        case GLFW_GAMEPAD_AXIS_RIGHT_Y:       return "Right Stick Y";
        case GLFW_GAMEPAD_AXIS_LEFT_TRIGGER:  return "Left Trigger";
        case GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER: return "Right Trigger";
        default:                              return nullptr;
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
        {
            // I1 — keyboard codes are scancodes. Prefer our curated
            // scancode→name fallback table FIRST: GLFW's `glfwGetKeyName`
            // returns a useless literal " " for Space and printable
            // glyphs for some locale keys (WORLD_1/2 on certain layouts),
            // which would clobber the rebind UI. The fallback table
            // covers every non-printable key the I6 work named. Fall
            // through to `glfwGetKeyName` for the printable letters /
            // digits / punctuation that NEED to be layout-aware
            // ("W" on QWERTY, "Z" on AZERTY for the same physical key).
            const auto& fallback = scancodeToNameMap();
            auto it = fallback.find(binding.code);
            if (it != fallback.end()) return it->second;
            if (const char* n = glfwGetKeyName(GLFW_KEY_UNKNOWN, binding.code))
            {
                return n;
            }
            break;
        }
        case InputDevice::Mouse:
            if (const char* n = mouseName(binding.code)) return n;
            break;
        case InputDevice::Gamepad:
            if (const char* n = gamepadName(binding.code)) return n;
            break;
        case InputDevice::GamepadAxis:
        {
            // Slice 9 I3 — render axis half as "Left Stick Y -" (up half)
            // or "Right Trigger +" (always positive). Triggers are
            // unidirectional so we suppress the sign suffix for them so
            // the rebind UI doesn't mislead the user.
            const int axis = unpackGamepadAxisIndex(binding.code);
            const int sign = unpackGamepadAxisSign(binding.code);
            const char* axisLabel = gamepadAxisName(axis);
            if (!axisLabel) break;
            const bool isTrigger = (axis == GLFW_GAMEPAD_AXIS_LEFT_TRIGGER
                                 || axis == GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER);
            if (isTrigger) return axisLabel;
            return std::string(axisLabel) + (sign < 0 ? " -" : " +");
        }
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

float axisValue(const InputActionMap& map,
                const std::string& actionId,
                const std::function<float(const InputBinding&)>& probe)
{
    const InputAction* action = map.findAction(actionId);
    if (!action || !probe) return 0.0f;

    float best = 0.0f;
    const InputBinding* slots[] = {
        &action->primary, &action->secondary, &action->gamepad
    };
    for (const InputBinding* slot : slots)
    {
        if (!slot->isBound()) continue;
        const float v = probe(*slot);
        if (v > best) best = v;
    }
    return std::clamp(best, 0.0f, 1.0f);
}

} // namespace Vestige
