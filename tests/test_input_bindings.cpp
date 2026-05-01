// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_input_bindings.cpp
/// @brief Phase 10 accessibility coverage for the remappable-controls
///        action-map data model and pure-function query path.

#include <gtest/gtest.h>

#include "core/logger.h"
#include "input/input_bindings.h"
#include "input/input_bindings_wire.h"

#include <nlohmann/json.hpp>

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
    jump.primary  = InputBinding::scancode(GLFW_KEY_SPACE);
    jump.gamepad  = InputBinding::gamepad(GLFW_GAMEPAD_BUTTON_A);
    m.addAction(jump);

    InputAction fire;
    fire.id        = "Fire";
    fire.label     = "Fire Weapon";
    fire.category  = "Gameplay";
    fire.primary   = InputBinding::mouse(GLFW_MOUSE_BUTTON_LEFT);
    fire.secondary = InputBinding::scancode(GLFW_KEY_ENTER);
    fire.gamepad   = InputBinding::gamepad(GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER);
    m.addAction(fire);

    InputAction moveFwd;
    moveFwd.id       = "MoveForward";
    moveFwd.label    = "Move Forward";
    moveFwd.category = "Gameplay";
    moveFwd.primary  = InputBinding::scancode(GLFW_KEY_W);
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
    EXPECT_TRUE(InputBinding::scancode(GLFW_KEY_W).isBound());
    EXPECT_TRUE(InputBinding::mouse(GLFW_MOUSE_BUTTON_LEFT).isBound());
    EXPECT_TRUE(InputBinding::gamepad(GLFW_GAMEPAD_BUTTON_A).isBound());
}

TEST(InputBinding, NoneFactoryIsUnbound)
{
    EXPECT_FALSE(InputBinding::none().isBound());
}

TEST(InputBinding, EqualityMatchesDeviceAndCode)
{
    EXPECT_EQ(InputBinding::scancode(GLFW_KEY_W), InputBinding::scancode(GLFW_KEY_W));
    EXPECT_NE(InputBinding::scancode(GLFW_KEY_W), InputBinding::scancode(GLFW_KEY_A));
    // Same code, different device is NOT equal.
    InputBinding kb{InputDevice::Keyboard, 32};
    InputBinding mb{InputDevice::Mouse,    32};
    EXPECT_NE(kb, mb);
}

// -- InputAction matches() --

TEST(InputAction, MatchesAnyOfTheThreeSlots)
{
    InputAction a;
    a.primary   = InputBinding::scancode(GLFW_KEY_SPACE);
    a.secondary = InputBinding::mouse(GLFW_MOUSE_BUTTON_MIDDLE);
    a.gamepad   = InputBinding::gamepad(GLFW_GAMEPAD_BUTTON_A);

    EXPECT_TRUE(a.matches(InputBinding::scancode(GLFW_KEY_SPACE)));
    EXPECT_TRUE(a.matches(InputBinding::mouse(GLFW_MOUSE_BUTTON_MIDDLE)));
    EXPECT_TRUE(a.matches(InputBinding::gamepad(GLFW_GAMEPAD_BUTTON_A)));
    EXPECT_FALSE(a.matches(InputBinding::scancode(GLFW_KEY_W)));
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
    v1.primary = InputBinding::scancode(GLFW_KEY_SPACE);
    m.addAction(v1);

    InputAction v2;
    v2.id = "Jump";
    v2.label = "Hop";
    v2.primary = InputBinding::scancode(GLFW_KEY_J);
    m.addAction(v2);

    ASSERT_EQ(m.actions().size(), 1u);
    EXPECT_EQ(m.findAction("Jump")->label, "Hop");
    EXPECT_EQ(m.findAction("Jump")->primary, InputBinding::scancode(GLFW_KEY_J));
}

// -- Reverse lookup / conflict detection --

TEST(InputActionMap, FindActionBoundToReturnsFirstMatch)
{
    InputActionMap m = sampleMap();
    const InputAction* a = m.findActionBoundTo(InputBinding::scancode(GLFW_KEY_SPACE));
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->id, "Jump");

    EXPECT_EQ(m.findActionBoundTo(InputBinding::scancode(GLFW_KEY_Q)), nullptr);
    EXPECT_EQ(m.findActionBoundTo(InputBinding::none()), nullptr);
}

TEST(InputActionMap, ConflictDetectionReportsColliders)
{
    InputActionMap m = sampleMap();

    // Rebinding Fire.primary to Space would conflict with Jump.primary.
    auto cs = m.findConflicts(InputBinding::scancode(GLFW_KEY_SPACE), "Fire");
    ASSERT_EQ(cs.size(), 1u);
    EXPECT_EQ(cs[0], "Jump");
}

TEST(InputActionMap, ConflictDetectionExcludesSelf)
{
    // Rebinding Jump to its own existing key — self-conflict doesn't
    // surface, otherwise the UI would light up every time the user
    // opens the rebind dialog.
    InputActionMap m = sampleMap();
    auto cs = m.findConflicts(InputBinding::scancode(GLFW_KEY_SPACE), "Jump");
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
    EXPECT_TRUE(m.setPrimary("Jump", InputBinding::scancode(GLFW_KEY_J)));
    EXPECT_EQ(m.findAction("Jump")->primary, InputBinding::scancode(GLFW_KEY_J));
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
    EXPECT_FALSE(m.setPrimary("Unknown",   InputBinding::scancode(GLFW_KEY_W)));
    EXPECT_FALSE(m.setSecondary("Unknown", InputBinding::scancode(GLFW_KEY_W)));
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
    m.setPrimary("Jump", InputBinding::scancode(GLFW_KEY_J));
    m.setPrimary("Fire", InputBinding::scancode(GLFW_KEY_F));

    m.resetToDefaults();

    EXPECT_EQ(m.findAction("Jump")->primary, InputBinding::scancode(GLFW_KEY_SPACE));
    EXPECT_EQ(m.findAction("Fire")->primary, InputBinding::mouse(GLFW_MOUSE_BUTTON_LEFT));
}

TEST(InputActionMap, ResetSingleActionLeavesOthersAlone)
{
    InputActionMap m = sampleMap();
    m.setPrimary("Jump", InputBinding::scancode(GLFW_KEY_J));
    m.setPrimary("Fire", InputBinding::scancode(GLFW_KEY_F));

    EXPECT_TRUE(m.resetActionToDefaults("Jump"));

    EXPECT_EQ(m.findAction("Jump")->primary, InputBinding::scancode(GLFW_KEY_SPACE));  // restored
    EXPECT_EQ(m.findAction("Fire")->primary, InputBinding::scancode(GLFW_KEY_F));      // user rebind kept
}

TEST(InputActionMap, ResetSingleActionUnknownIdReturnsFalse)
{
    InputActionMap m = sampleMap();
    EXPECT_FALSE(m.resetActionToDefaults("Unknown"));
}

// -- Display labels --
//
// Phase 10.9 Slice 9 I1 moved keyboard display from a static keycode→name
// table to a glfwGetKeyName + lazy-scancode-fallback path that requires
// live GLFW state. Headless tests below pin only the device-static labels
// (mouse + gamepad + em-dash); the GLFW-init-fenced keyboard display
// coverage lives in the I1 block at the end of this file.

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
        return b == InputBinding::scancode(GLFW_KEY_SPACE);
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
    original.primary = InputBinding::scancode(GLFW_KEY_SPACE);
    m.addAction(original);

    // Simulate Settings::load — user rebound Jump to F.
    m.setPrimary("Jump", InputBinding::scancode(GLFW_KEY_F));

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
    EXPECT_EQ(live->primary, InputBinding::scancode(GLFW_KEY_SPACE));
}

TEST(InputActionMap, ReRegisterIdenticalBindingsIsSilent_I5)
{
    Logger::clearEntries();

    InputActionMap m;
    InputAction original;
    original.id      = "Pause";
    original.primary = InputBinding::scancode(GLFW_KEY_ESCAPE);
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
    action.primary = InputBinding::scancode(GLFW_KEY_LEFT_CONTROL);
    m.addAction(action);

    EXPECT_EQ(countWarningsContaining("InputActionMap::addAction"), 0u);
}

// ---------------------------------------------------------------------------
// AUDIT I6 — bindingDisplayLabel covers numpad / Pause / ScrollLock / NumLock
// / extended-F / world / menu keys. Pre-I6 these all fell through to the
// "Key 320…" debug fallback in the rebind UI. Phase 10.9 Slice 9 I1 moved
// keyboard codes from keycodes to scancodes; the same coverage now requires
// live GLFW state because the lazy scancode→name table is populated via
// glfwGetKeyScancode. The fenced test below preserves the I6 surface.
// ---------------------------------------------------------------------------

TEST(InputActionMap, KeyboardDisplayCoversNumpadAndSystemKeys_I6)
{
    if (glfwInit() == GLFW_FALSE)
    {
        GTEST_SKIP() << "GLFW init failed — display-label coverage runs at engine launch.";
    }
    auto labelOf = [](int kc) {
        const int sc = glfwGetKeyScancode(kc);
        return std::pair<int, std::string>(
            sc, sc < 0 ? std::string{} : bindingDisplayLabel(InputBinding::scancode(sc)));
    };
    auto expectLabel = [&](int kc, const char* expected) {
        const auto [sc, label] = labelOf(kc);
        if (sc < 0) return;  // Some keys are platform-absent — silent skip.
        EXPECT_EQ(label, expected) << "keycode=" << kc << " scancode=" << sc;
    };
    expectLabel(GLFW_KEY_KP_0, "Numpad 0");
    expectLabel(GLFW_KEY_KP_9, "Numpad 9");
    expectLabel(GLFW_KEY_KP_ADD, "Numpad +");
    expectLabel(GLFW_KEY_KP_ENTER, "Numpad Enter");
    expectLabel(GLFW_KEY_KP_DECIMAL, "Numpad .");

    expectLabel(GLFW_KEY_PAUSE, "Pause");
    expectLabel(GLFW_KEY_PRINT_SCREEN, "Print Screen");
    expectLabel(GLFW_KEY_SCROLL_LOCK, "Scroll Lock");
    expectLabel(GLFW_KEY_NUM_LOCK, "Num Lock");
    expectLabel(GLFW_KEY_MENU, "Menu");

    expectLabel(GLFW_KEY_F13, "F13");
    expectLabel(GLFW_KEY_F25, "F25");

    expectLabel(GLFW_KEY_WORLD_1, "World 1");
    expectLabel(GLFW_KEY_WORLD_2, "World 2");
    glfwTerminate();
}

// ---------------------------------------------------------------------------
// Phase 10.9 Slice 9 I2 — wire-format helpers relocated to engine/input/.
// These tests pin the round-trip in the new home so a future move back to
// settings.cpp would have to update both files.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Phase 10.9 Slice 9 I4 — findConflicts only returns same-device collisions.
// ---------------------------------------------------------------------------

TEST(InputActionMap, FindConflictsSameKeycodeDifferentDevicesNoConflict_I4)
{
    // Jump.gamepad bound to gamepad button 0; querying keyboard scan-code
    // 0 must not surface Jump as a conflict — the two are on different
    // hardware and physically can't collide.
    InputActionMap m;
    InputAction jump;
    jump.id      = "Jump";
    jump.gamepad = InputBinding::gamepad(0);
    m.addAction(jump);

    auto cs = m.findConflicts(InputBinding::scancode(0));
    EXPECT_TRUE(cs.empty());
}

TEST(InputActionMap, FindConflictsKeyboardConflictsOnlyWithKeyboardSlot_I4)
{
    // Crouch.primary on keyboard:C; Fire.gamepad on gamepad:C-equivalent
    // (deliberately same code value, different device). Querying
    // keyboard:C should surface only Crouch.
    InputActionMap m;
    InputAction crouch;
    crouch.id      = "Crouch";
    crouch.primary = InputBinding::scancode(67);  // arbitrary scan code
    m.addAction(crouch);

    InputAction fire;
    fire.id      = "Fire";
    fire.gamepad = InputBinding::gamepad(67);  // same code, different device
    m.addAction(fire);

    auto cs = m.findConflicts(InputBinding::scancode(67));
    ASSERT_EQ(cs.size(), 1u);
    EXPECT_EQ(cs[0], "Crouch");
}

TEST(InputActionMap, FindConflictsGamepadIgnoresKeyboardAndMouseSlots_I4)
{
    // Mirror of the prior test: querying gamepad:0 must skip a keyboard
    // slot bound at the same code value.
    InputActionMap m;
    InputAction crouch;
    crouch.id      = "Crouch";
    crouch.primary = InputBinding::scancode(0);  // keyboard scan code 0
    m.addAction(crouch);

    InputAction fire;
    fire.id      = "Fire";
    fire.gamepad = InputBinding::gamepad(0);
    m.addAction(fire);

    auto cs = m.findConflicts(InputBinding::gamepad(0));
    ASSERT_EQ(cs.size(), 1u);
    EXPECT_EQ(cs[0], "Fire");
}

TEST(InputBindingsWire, BindingRoundTripIsSymmetric_I2)
{
    InputBindingWire b;
    b.device   = "keyboard";
    b.scancode = 38;  // some scan code

    const nlohmann::json j = bindingToJson(b);
    EXPECT_EQ(j["device"],   "keyboard");
    EXPECT_EQ(j["scancode"], 38);

    const InputBindingWire b2 = bindingFromJson(j);
    EXPECT_EQ(b2, b);
}

TEST(InputBindingsWire, BindingFromJsonNoneClearsScancode_I2)
{
    // When device is "none", scancode collapses to -1 even if the
    // input JSON tries to record one. Pinned because the contract is
    // load-bearing: a deserialised "none" binding must compare equal
    // to InputBinding::none() regardless of which scancode the file
    // happened to carry.
    nlohmann::json j;
    j["device"]   = "none";
    j["scancode"] = 99;

    const InputBindingWire b = bindingFromJson(j);
    EXPECT_EQ(b.device, "none");
    EXPECT_EQ(b.scancode, -1);
}

TEST(InputBindingsWire, ActionBindingRoundTripIsSymmetric_I2)
{
    ActionBindingWire ab;
    ab.id              = "Jump";
    ab.primary.device  = "keyboard";
    ab.primary.scancode = 65;
    ab.secondary.device = "none";
    ab.gamepad.device   = "gamepad";
    ab.gamepad.scancode = 0;

    const nlohmann::json j = actionBindingToJson(ab);
    EXPECT_EQ(j["id"], "Jump");

    const ActionBindingWire ab2 = actionBindingFromJson(j);
    EXPECT_EQ(ab2, ab);
}

TEST(InputBindingsWire, ActionBindingFromJsonMissingFieldsTakeDefaults_I2)
{
    // An older settings file might lack the secondary or gamepad slot
    // entirely. The from-JSON helper must default-construct them rather
    // than throw or silently leave the slot in an undefined state.
    nlohmann::json j;
    j["id"]      = "Fire";
    j["primary"] = nlohmann::json{{"device", "mouse"}, {"scancode", 0}};

    const ActionBindingWire ab = actionBindingFromJson(j);
    EXPECT_EQ(ab.id, "Fire");
    EXPECT_EQ(ab.primary.device, "mouse");
    EXPECT_EQ(ab.secondary, InputBindingWire{});
    EXPECT_EQ(ab.gamepad, InputBindingWire{});
}

// ---------------------------------------------------------------------------
// Phase 10.9 Slice 9 I1 — `InputBinding::code` stores a GLFW scancode (physical
// key position) for keyboard bindings, not a GLFW keycode (layout-mapped).
// Pre-I1 a QWERTY user's saved settings.json had `code = GLFW_KEY_W = 87`;
// loaded on AZERTY, glfwGetKey polled "the key that produces 'W' on AZERTY"
// (physically the QWERTY-Z position) and WASD silently flipped. After I1 the
// binding is an OS-level physical-position id that survives layout swaps.
// ---------------------------------------------------------------------------

TEST(InputBinding, ScancodeFactoryProducesBoundKeyboardBinding_I1)
{
    InputBinding b = InputBinding::scancode(42);
    EXPECT_TRUE(b.isBound());
    EXPECT_EQ(b.device, InputDevice::Keyboard);
    EXPECT_EQ(b.code, 42);
}

TEST(InputBinding, ScancodeFactoryEqualityIsValueBased_I1)
{
    EXPECT_EQ(InputBinding::scancode(17), InputBinding::scancode(17));
    EXPECT_NE(InputBinding::scancode(17), InputBinding::scancode(31));
    // A keyboard scancode never collides with a mouse / gamepad button at
    // the same numeric value — device gate is the I4 contract, retained.
    EXPECT_NE(InputBinding::scancode(0), InputBinding::mouse(0));
    EXPECT_NE(InputBinding::scancode(0), InputBinding::gamepad(0));
}

TEST(InputBinding, ScancodeFactoryRejectsNegativeAsUnbound_I1)
{
    // A capture path that calls glfwGetKeyScancode on an unknown key gets
    // -1 back. The factory still returns a Keyboard-device binding but the
    // isBound() guard catches it so it never enters the action map.
    InputBinding b = InputBinding::scancode(-1);
    EXPECT_FALSE(b.isBound());
}

TEST(InputBindingsWire, ScancodeRoundTripPreservesPhysicalIdentity_I1)
{
    // The headline regression: a QWERTY-authored binding stores its
    // platform scancode (e.g. 17 for the W-position key on Linux X11);
    // the wire round-trip must preserve the integer exactly so an AZERTY
    // load reads the same physical-position id.
    InputBindingWire w;
    w.device   = "keyboard";
    w.scancode = 17;

    const nlohmann::json j = bindingToJson(w);
    const InputBindingWire w2 = bindingFromJson(j);

    EXPECT_EQ(w2.device, "keyboard");
    EXPECT_EQ(w2.scancode, 17);
    // The contract is a layout-independent integer round-trip — there is
    // no place in the wire path where a layout-aware translation could
    // sneak in.
    EXPECT_EQ(w2, w);
}

// glfwInit-dependent display tests: glfwGetKeyName / glfwGetKeyScancode
// require live GLFW state. Local dev (and CI hosts with a display) get
// the coverage; headless runners skip cleanly per project precedent for
// runtime-only verification (test_gpu_cloth_simulator.cpp doc).

TEST(BindingDisplayLabel, KeyboardScancodePrintableUsesGlfwKeyName_I1)
{
    if (glfwInit() == GLFW_FALSE)
    {
        GTEST_SKIP() << "GLFW init failed — display-label coverage runs at engine launch.";
    }
    const int wScan = glfwGetKeyScancode(GLFW_KEY_W);
    if (wScan < 0)
    {
        glfwTerminate();
        GTEST_SKIP() << "GLFW_KEY_W has no scancode in this environment.";
    }
    const std::string label = bindingDisplayLabel(InputBinding::scancode(wScan));
    // Layout-aware: on QWERTY this is "W"; on AZERTY it'd be "Z" for the
    // same physical key. Either way the label is non-empty and not the
    // debug fallback, which is the contract that matters here.
    EXPECT_FALSE(label.empty());
    EXPECT_EQ(label.find("Key "), std::string::npos)
        << "Printable key should not fall through to the debug 'Key NN' label.";
    glfwTerminate();
}

TEST(BindingDisplayLabel, KeyboardScancodeNonPrintableUsesFallbackTable_I1)
{
    if (glfwInit() == GLFW_FALSE)
    {
        GTEST_SKIP() << "GLFW init failed — display-label coverage runs at engine launch.";
    }
    // Non-printable keys (Space, Shift, F1, numpad, ...) — glfwGetKeyName
    // returns nullptr for these, so the lazy scancode→fallback table
    // must catch them. F1 is the canonical non-printable hotkey used by
    // the engine's default bindings (engine.cpp:321).
    const int f1Scan = glfwGetKeyScancode(GLFW_KEY_F1);
    if (f1Scan < 0)
    {
        glfwTerminate();
        GTEST_SKIP() << "GLFW_KEY_F1 has no scancode in this environment.";
    }
    EXPECT_EQ(bindingDisplayLabel(InputBinding::scancode(f1Scan)), "F1");

    const int spcScan = glfwGetKeyScancode(GLFW_KEY_SPACE);
    if (spcScan >= 0)
    {
        EXPECT_EQ(bindingDisplayLabel(InputBinding::scancode(spcScan)), "Space");
    }
    glfwTerminate();
}

TEST(BindingDisplayLabel, MouseGamepadEmDashUnaffectedByScancodeMove_I1)
{
    // Regression guard: only the keyboard display path changes under I1.
    // Mouse + gamepad + unbound labels stay byte-identical to pre-I1.
    EXPECT_EQ(bindingDisplayLabel(InputBinding::mouse(GLFW_MOUSE_BUTTON_LEFT)), "Left Mouse");
    EXPECT_EQ(bindingDisplayLabel(InputBinding::gamepad(GLFW_GAMEPAD_BUTTON_A)), "A");
    EXPECT_EQ(bindingDisplayLabel(InputBinding::none()), "—");
}

