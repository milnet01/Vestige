// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_input_bindings.cpp
/// @brief Phase 10 accessibility coverage for the remappable-controls
///        action-map data model and pure-function query path.

#include <gtest/gtest.h>

#include "core/logger.h"
#include "input/input_bindings.h"

#include <GLFW/glfw3.h>

using namespace Vestige;

namespace
{
// Convenience: construct a small sample map with three actions so
// tests don't have to rebuild it every case.
InputActionMap sampleMap()
{
    InputActionMap m;

    InputAction jump;
    jump.id       = "Jump";
    jump.label    = "Jump";
    jump.category = "Gameplay";
    jump.primary  = InputBinding::key(GLFW_KEY_SPACE);
    jump.gamepad  = InputBinding::gamepad(GLFW_GAMEPAD_BUTTON_A);
    m.addAction(jump);

    InputAction fire;
    fire.id        = "Fire";
    fire.label     = "Fire Weapon";
    fire.category  = "Gameplay";
    fire.primary   = InputBinding::mouse(GLFW_MOUSE_BUTTON_LEFT);
    fire.secondary = InputBinding::key(GLFW_KEY_ENTER);
    fire.gamepad   = InputBinding::gamepad(GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER);
    m.addAction(fire);

    InputAction moveFwd;
    moveFwd.id       = "MoveForward";
    moveFwd.label    = "Move Forward";
    moveFwd.category = "Gameplay";
    moveFwd.primary  = InputBinding::key(GLFW_KEY_W);
    m.addAction(moveFwd);

    return m;
}
}

// -- InputBinding primitive --

TEST(InputBinding, IsBoundFalseForDefault)
{
    InputBinding b;
    EXPECT_FALSE(b.isBound());
}

TEST(InputBinding, FactoryHelpersProduceBoundInstances)
{
    EXPECT_TRUE(InputBinding::key(GLFW_KEY_W).isBound());
    EXPECT_TRUE(InputBinding::mouse(GLFW_MOUSE_BUTTON_LEFT).isBound());
    EXPECT_TRUE(InputBinding::gamepad(GLFW_GAMEPAD_BUTTON_A).isBound());
}

TEST(InputBinding, NoneFactoryIsUnbound)
{
    EXPECT_FALSE(InputBinding::none().isBound());
}

TEST(InputBinding, EqualityMatchesDeviceAndCode)
{
    EXPECT_EQ(InputBinding::key(GLFW_KEY_W), InputBinding::key(GLFW_KEY_W));
    EXPECT_NE(InputBinding::key(GLFW_KEY_W), InputBinding::key(GLFW_KEY_A));
    // Same code, different device is NOT equal.
    InputBinding kb{InputDevice::Keyboard, 32};
    InputBinding mb{InputDevice::Mouse,    32};
    EXPECT_NE(kb, mb);
}

// -- InputAction matches() --

TEST(InputAction, MatchesAnyOfTheThreeSlots)
{
    InputAction a;
    a.primary   = InputBinding::key(GLFW_KEY_SPACE);
    a.secondary = InputBinding::mouse(GLFW_MOUSE_BUTTON_MIDDLE);
    a.gamepad   = InputBinding::gamepad(GLFW_GAMEPAD_BUTTON_A);

    EXPECT_TRUE(a.matches(InputBinding::key(GLFW_KEY_SPACE)));
    EXPECT_TRUE(a.matches(InputBinding::mouse(GLFW_MOUSE_BUTTON_MIDDLE)));
    EXPECT_TRUE(a.matches(InputBinding::gamepad(GLFW_GAMEPAD_BUTTON_A)));
    EXPECT_FALSE(a.matches(InputBinding::key(GLFW_KEY_W)));
    EXPECT_FALSE(a.matches(InputBinding::none()));
}

// -- InputActionMap: registration / lookup --

TEST(InputActionMap, AddActionPreservesInsertionOrder)
{
    InputActionMap m = sampleMap();
    ASSERT_EQ(m.actions().size(), 3u);
    EXPECT_EQ(m.actions()[0].id, "Jump");
    EXPECT_EQ(m.actions()[1].id, "Fire");
    EXPECT_EQ(m.actions()[2].id, "MoveForward");
}

TEST(InputActionMap, FindActionByIdReturnsSameObject)
{
    InputActionMap m = sampleMap();
    const InputAction* a = m.findAction("Fire");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->label, "Fire Weapon");
    EXPECT_EQ(a->primary, InputBinding::mouse(GLFW_MOUSE_BUTTON_LEFT));
    EXPECT_EQ(m.findAction("Unknown"), nullptr);
}

TEST(InputActionMap, ReaddingSameIdReplacesTheAction)
{
    InputActionMap m;
    InputAction v1;
    v1.id = "Jump";
    v1.primary = InputBinding::key(GLFW_KEY_SPACE);
    m.addAction(v1);

    InputAction v2;
    v2.id = "Jump";
    v2.label = "Hop";
    v2.primary = InputBinding::key(GLFW_KEY_J);
    m.addAction(v2);

    ASSERT_EQ(m.actions().size(), 1u);
    EXPECT_EQ(m.findAction("Jump")->label, "Hop");
    EXPECT_EQ(m.findAction("Jump")->primary, InputBinding::key(GLFW_KEY_J));
}

// -- Reverse lookup / conflict detection --

TEST(InputActionMap, FindActionBoundToReturnsFirstMatch)
{
    InputActionMap m = sampleMap();
    const InputAction* a = m.findActionBoundTo(InputBinding::key(GLFW_KEY_SPACE));
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->id, "Jump");

    EXPECT_EQ(m.findActionBoundTo(InputBinding::key(GLFW_KEY_Q)), nullptr);
    EXPECT_EQ(m.findActionBoundTo(InputBinding::none()), nullptr);
}

TEST(InputActionMap, ConflictDetectionReportsColliders)
{
    InputActionMap m = sampleMap();

    // Rebinding Fire.primary to Space would conflict with Jump.primary.
    auto cs = m.findConflicts(InputBinding::key(GLFW_KEY_SPACE), "Fire");
    ASSERT_EQ(cs.size(), 1u);
    EXPECT_EQ(cs[0], "Jump");
}

TEST(InputActionMap, ConflictDetectionExcludesSelf)
{
    // Rebinding Jump to its own existing key — self-conflict doesn't
    // surface, otherwise the UI would light up every time the user
    // opens the rebind dialog.
    InputActionMap m = sampleMap();
    auto cs = m.findConflicts(InputBinding::key(GLFW_KEY_SPACE), "Jump");
    EXPECT_TRUE(cs.empty());
}

TEST(InputActionMap, UnboundBindingProducesNoConflicts)
{
    InputActionMap m = sampleMap();
    auto cs = m.findConflicts(InputBinding::none());
    EXPECT_TRUE(cs.empty());
}

// -- Rebinding --

TEST(InputActionMap, SetPrimaryUpdatesInPlace)
{
    InputActionMap m = sampleMap();
    EXPECT_TRUE(m.setPrimary("Jump", InputBinding::key(GLFW_KEY_J)));
    EXPECT_EQ(m.findAction("Jump")->primary, InputBinding::key(GLFW_KEY_J));
}

TEST(InputActionMap, SetSecondaryUpdatesInPlace)
{
    InputActionMap m = sampleMap();
    EXPECT_TRUE(m.setSecondary("Jump", InputBinding::mouse(GLFW_MOUSE_BUTTON_RIGHT)));
    EXPECT_EQ(m.findAction("Jump")->secondary, InputBinding::mouse(GLFW_MOUSE_BUTTON_RIGHT));
}

TEST(InputActionMap, SetGamepadUpdatesInPlace)
{
    InputActionMap m = sampleMap();
    EXPECT_TRUE(m.setGamepad("Jump", InputBinding::gamepad(GLFW_GAMEPAD_BUTTON_Y)));
    EXPECT_EQ(m.findAction("Jump")->gamepad, InputBinding::gamepad(GLFW_GAMEPAD_BUTTON_Y));
}

TEST(InputActionMap, SettersReturnFalseForUnknownAction)
{
    InputActionMap m;
    EXPECT_FALSE(m.setPrimary("Unknown",   InputBinding::key(GLFW_KEY_W)));
    EXPECT_FALSE(m.setSecondary("Unknown", InputBinding::key(GLFW_KEY_W)));
    EXPECT_FALSE(m.setGamepad("Unknown",   InputBinding::gamepad(GLFW_GAMEPAD_BUTTON_A)));
}

TEST(InputActionMap, ClearSlotWipesTheTargetedSlot)
{
    InputActionMap m = sampleMap();
    EXPECT_TRUE(m.clearSlot("Fire", 1));
    EXPECT_FALSE(m.findAction("Fire")->secondary.isBound());
    // Primary + gamepad unaffected.
    EXPECT_TRUE(m.findAction("Fire")->primary.isBound());
    EXPECT_TRUE(m.findAction("Fire")->gamepad.isBound());
}

TEST(InputActionMap, ClearSlotRejectsInvalidSlotIndex)
{
    InputActionMap m = sampleMap();
    EXPECT_FALSE(m.clearSlot("Jump", 3));
    EXPECT_FALSE(m.clearSlot("Jump", -1));
}

// -- Reset to defaults --

TEST(InputActionMap, ResetToDefaultsRestoresEntireMap)
{
    InputActionMap m = sampleMap();
    m.setPrimary("Jump", InputBinding::key(GLFW_KEY_J));
    m.setPrimary("Fire", InputBinding::key(GLFW_KEY_F));

    m.resetToDefaults();

    EXPECT_EQ(m.findAction("Jump")->primary, InputBinding::key(GLFW_KEY_SPACE));
    EXPECT_EQ(m.findAction("Fire")->primary, InputBinding::mouse(GLFW_MOUSE_BUTTON_LEFT));
}

TEST(InputActionMap, ResetSingleActionLeavesOthersAlone)
{
    InputActionMap m = sampleMap();
    m.setPrimary("Jump", InputBinding::key(GLFW_KEY_J));
    m.setPrimary("Fire", InputBinding::key(GLFW_KEY_F));

    EXPECT_TRUE(m.resetActionToDefaults("Jump"));

    EXPECT_EQ(m.findAction("Jump")->primary, InputBinding::key(GLFW_KEY_SPACE));  // restored
    EXPECT_EQ(m.findAction("Fire")->primary, InputBinding::key(GLFW_KEY_F));      // user rebind kept
}

TEST(InputActionMap, ResetSingleActionUnknownIdReturnsFalse)
{
    InputActionMap m = sampleMap();
    EXPECT_FALSE(m.resetActionToDefaults("Unknown"));
}

// -- Display labels --

TEST(BindingDisplayLabel, KeyboardNamesAreReadable)
{
    EXPECT_EQ(bindingDisplayLabel(InputBinding::key(GLFW_KEY_W)),           "W");
    EXPECT_EQ(bindingDisplayLabel(InputBinding::key(GLFW_KEY_SPACE)),       "Space");
    EXPECT_EQ(bindingDisplayLabel(InputBinding::key(GLFW_KEY_LEFT_SHIFT)),  "Left Shift");
    EXPECT_EQ(bindingDisplayLabel(InputBinding::key(GLFW_KEY_ESCAPE)),      "Escape");
    EXPECT_EQ(bindingDisplayLabel(InputBinding::key(GLFW_KEY_F1)),          "F1");
}

TEST(BindingDisplayLabel, MouseNamesAreReadable)
{
    EXPECT_EQ(bindingDisplayLabel(InputBinding::mouse(GLFW_MOUSE_BUTTON_LEFT)),   "Left Mouse");
    EXPECT_EQ(bindingDisplayLabel(InputBinding::mouse(GLFW_MOUSE_BUTTON_RIGHT)),  "Right Mouse");
    EXPECT_EQ(bindingDisplayLabel(InputBinding::mouse(GLFW_MOUSE_BUTTON_MIDDLE)), "Middle Mouse");
}

TEST(BindingDisplayLabel, GamepadNamesUseXboxVocabulary)
{
    EXPECT_EQ(bindingDisplayLabel(InputBinding::gamepad(GLFW_GAMEPAD_BUTTON_A)),           "A");
    EXPECT_EQ(bindingDisplayLabel(InputBinding::gamepad(GLFW_GAMEPAD_BUTTON_LEFT_BUMPER)), "LB");
    EXPECT_EQ(bindingDisplayLabel(InputBinding::gamepad(GLFW_GAMEPAD_BUTTON_DPAD_UP)),     "D-Pad Up");
    EXPECT_EQ(bindingDisplayLabel(InputBinding::gamepad(GLFW_GAMEPAD_BUTTON_START)),       "Start");
}

TEST(BindingDisplayLabel, UnboundRendersEmDash)
{
    EXPECT_EQ(bindingDisplayLabel(InputBinding::none()), "\u2014");
}

// -- Pure-function query with injected binding checker --

TEST(IsActionDown, TrueWhenAnySlotIsDown)
{
    InputActionMap m = sampleMap();

    // Simulate: only Space (Jump primary) is down.
    auto isDown = [](const InputBinding& b) {
        return b == InputBinding::key(GLFW_KEY_SPACE);
    };

    EXPECT_TRUE(isActionDown(m, "Jump", isDown));
    EXPECT_FALSE(isActionDown(m, "Fire", isDown));
    EXPECT_FALSE(isActionDown(m, "MoveForward", isDown));
}

TEST(IsActionDown, AnyOfThreeSlotsIsSufficient)
{
    InputActionMap m = sampleMap();

    // Only the gamepad slot (Fire.gamepad = RB) is down.
    auto isDown = [](const InputBinding& b) {
        return b == InputBinding::gamepad(GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER);
    };
    EXPECT_TRUE(isActionDown(m, "Fire", isDown));
    EXPECT_FALSE(isActionDown(m, "Jump", isDown));
}

TEST(IsActionDown, FalseForUnknownAction)
{
    InputActionMap m = sampleMap();
    auto isDown = [](const InputBinding&) { return true; };
    EXPECT_FALSE(isActionDown(m, "Unknown", isDown));
}

TEST(IsActionDown, FalseWhenNoSlotsBound)
{
    InputActionMap m;
    InputAction a;
    a.id = "Orphan";
    m.addAction(a);

    auto isDown = [](const InputBinding&) { return true; };
    EXPECT_FALSE(isActionDown(m, "Orphan", isDown));
}

TEST(IsActionDown, HandlesNullBindingCheckerGracefully)
{
    InputActionMap m = sampleMap();
    std::function<bool(const InputBinding&)> nullChecker;
    EXPECT_FALSE(isActionDown(m, "Jump", nullChecker));
}

// ---------------------------------------------------------------------------
// AUDIT I5 — re-registering an action whose live bindings differ from the
// new defaults must surface a warning. This catches the silent-nuke flow
// where an addAction() call after Settings::load() clobbers user rebinds.
// ---------------------------------------------------------------------------

namespace
{
size_t countWarningsContaining(const std::string& needle)
{
    size_t n = 0;
    for (const auto& entry : Logger::getEntries())
    {
        if (entry.message.find(needle) != std::string::npos)
        {
            ++n;
        }
    }
    return n;
}
}

TEST(InputActionMap, ReRegisterDivergentBindingsWarns_I5)
{
    Logger::clearEntries();

    InputActionMap m;
    InputAction original;
    original.id      = "Jump";
    original.primary = InputBinding::key(GLFW_KEY_SPACE);
    m.addAction(original);

    // Simulate Settings::load — user rebound Jump to F.
    m.setPrimary("Jump", InputBinding::key(GLFW_KEY_F));

    // Game code re-registers Jump (silent-nuke path pre-I5).
    Logger::clearEntries();
    m.addAction(original);

    EXPECT_GE(countWarningsContaining("InputActionMap::addAction"), 1u);
    EXPECT_GE(countWarningsContaining("\"Jump\""), 1u);
    EXPECT_GE(countWarningsContaining("user rebinds"), 1u);

    // Live binding is whatever addAction supplied (this is the documented
    // behaviour) — the warning surfaces the regression so callers can fix
    // the registration order.
    const InputAction* live = m.findAction("Jump");
    ASSERT_NE(live, nullptr);
    EXPECT_EQ(live->primary, InputBinding::key(GLFW_KEY_SPACE));
}

TEST(InputActionMap, ReRegisterIdenticalBindingsIsSilent_I5)
{
    Logger::clearEntries();

    InputActionMap m;
    InputAction original;
    original.id      = "Pause";
    original.primary = InputBinding::key(GLFW_KEY_ESCAPE);
    m.addAction(original);

    // No user rebind — re-registering with the same defaults is a hot-reload
    // no-op that must NOT warn.
    Logger::clearEntries();
    m.addAction(original);

    EXPECT_EQ(countWarningsContaining("InputActionMap::addAction"), 0u);
}

TEST(InputActionMap, FirstRegistrationIsSilent_I5)
{
    Logger::clearEntries();

    InputActionMap m;
    InputAction action;
    action.id      = "Crouch";
    action.primary = InputBinding::key(GLFW_KEY_LEFT_CONTROL);
    m.addAction(action);

    EXPECT_EQ(countWarningsContaining("InputActionMap::addAction"), 0u);
}

// ---------------------------------------------------------------------------
// AUDIT I6 — bindingDisplayLabel covers numpad / Pause / ScrollLock / NumLock
// / extended-F / world / menu keys. Pre-I6 these all fell through to the
// "Key 320…" debug fallback in the rebind UI.
// ---------------------------------------------------------------------------

TEST(InputActionMap, KeyboardDisplayCoversNumpadAndSystemKeys_I6)
{
    EXPECT_EQ(bindingDisplayLabel(InputBinding::key(GLFW_KEY_KP_0)),    "Numpad 0");
    EXPECT_EQ(bindingDisplayLabel(InputBinding::key(GLFW_KEY_KP_9)),    "Numpad 9");
    EXPECT_EQ(bindingDisplayLabel(InputBinding::key(GLFW_KEY_KP_ADD)),  "Numpad +");
    EXPECT_EQ(bindingDisplayLabel(InputBinding::key(GLFW_KEY_KP_ENTER)),"Numpad Enter");
    EXPECT_EQ(bindingDisplayLabel(InputBinding::key(GLFW_KEY_KP_DECIMAL)),"Numpad .");

    EXPECT_EQ(bindingDisplayLabel(InputBinding::key(GLFW_KEY_PAUSE)),       "Pause");
    EXPECT_EQ(bindingDisplayLabel(InputBinding::key(GLFW_KEY_PRINT_SCREEN)),"Print Screen");
    EXPECT_EQ(bindingDisplayLabel(InputBinding::key(GLFW_KEY_SCROLL_LOCK)), "Scroll Lock");
    EXPECT_EQ(bindingDisplayLabel(InputBinding::key(GLFW_KEY_NUM_LOCK)),    "Num Lock");
    EXPECT_EQ(bindingDisplayLabel(InputBinding::key(GLFW_KEY_MENU)),        "Menu");

    EXPECT_EQ(bindingDisplayLabel(InputBinding::key(GLFW_KEY_F13)), "F13");
    EXPECT_EQ(bindingDisplayLabel(InputBinding::key(GLFW_KEY_F25)), "F25");

    EXPECT_EQ(bindingDisplayLabel(InputBinding::key(GLFW_KEY_WORLD_1)), "World 1");
    EXPECT_EQ(bindingDisplayLabel(InputBinding::key(GLFW_KEY_WORLD_2)), "World 2");
}
