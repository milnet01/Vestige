// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_accessible.h
/// @brief Phase 10 accessibility — ARIA-like semantic layer on every
///        `UIElement` so a future screen-reader / TTS bridge can
///        enumerate interactive widgets with role + label + value.
///
/// **Status (Phase 10.9 W4):** This file is *infrastructure-only*. The
/// `UIElement::collectAccessible` / `UICanvas::collectAccessible`
/// walkers, the `UIAccessibilitySnapshot` flat-list output, and every
/// widget's role-setting constructor work as designed and are pinned
/// by 13 unit tests. **What is not wired:** the platform-side bridge
/// that hands those snapshots to a real screen reader (AT-SPI on
/// Linux / UIA on Windows). Until that bridge lands — currently
/// targeted for Phase 11+ once the engine has actual shipping games
/// with end users running screen readers — the collector has no
/// consumer outside tests.
///
/// **Why we kept the collector instead of relocating to
/// `engine/experimental/`:** the collector is platform-agnostic data
/// extraction (~50 lines of pure tree walking). Only the *consumer*
/// is platform-specific. Keeping it in `engine/ui/` lets game / editor
/// code add accessibility metadata to their custom widgets today, so
/// the work isn't lost when the bridge eventually arrives. Compare
/// with the W12 / W13 zombies — those are full subsystems whose
/// experimental relocation made sense; `UIAccessibility` is an API
/// surface, not a runtime, and demoting it would create more churn
/// than it would prevent.
///
/// **What the bridge will need:** an event-stream consumer that calls
/// `UICanvas::collectAccessible()` on every announce-relevant
/// transition (focus change, value change, modal open / close), maps
/// `UIAccessibleRole` → AT-SPI `Atspi.Role` / UIA
/// `ControlTypeIdentifiers`, and pushes the role + label + value over
/// the OS-IPC bus. That's the chunk of work explicitly deferred to
/// Phase 11+.
///
/// The design mirrors the WAI-ARIA 1.2 vocabulary (role, name /
/// `aria-label`, description / `aria-describedby`, value /
/// `aria-valuetext`, keyboard-hint / `aria-keyshortcuts`) collapsed
/// into a minimal, stringly-typed struct that every `UIElement`
/// carries. Rendering-side widgets are unaware of this metadata — it
/// exists purely as an annotation the TTS bridge (or a future
/// accessibility inspector panel) iterates on demand.
///
/// *Why a flat struct rather than per-role subclasses?*  A screen
/// reader needs exactly five things: what is it, what's it called,
/// what does it mean, what's its current state, and how do I
/// activate it. Those five fields map 1:1 to
/// `role / label / description / value / hint`. Adding a sixth
/// (position) can be computed from the element at enumeration time,
/// so we don't bake screen geometry into the metadata.
#pragma once

#include <string>

namespace Vestige
{

/// @brief ARIA-style role tag for a `UIElement`.
///
/// A screen reader uses this to decide how to announce the widget
/// ("Button — Play Game", "Slider — Master Volume, 75%"). Set by
/// each widget's constructor; consumers treat `Unknown` as
/// "do not announce" unless the element also has a label.
enum class UIAccessibleRole
{
    Unknown,       ///< Default — omitted from announcements unless a label is set.
    Label,         ///< Static text read verbatim.
    Panel,         ///< Container; announced as a region / group.
    Image,         ///< Decorative or semantic image.
    Button,        ///< Push-to-activate control.
    Checkbox,      ///< Binary on/off toggle.
    Slider,        ///< Continuous numeric control with current value.
    Dropdown,      ///< Menu / combo box selection.
    KeybindRow,    ///< Settings row showing an action and its bound keys.
    ProgressBar,   ///< Non-interactive fill indicator (health, loading).
    Crosshair,     ///< HUD reticle; usually omitted by screen readers.
};

/// @brief Returns a stable, human-readable label for a role — used by
///        tests, debug panels, and as the default TTS prefix.
const char* uiAccessibleRoleLabel(UIAccessibleRole role);

/// @brief Semantic metadata attached to every `UIElement`.
///
/// Fields mirror the five ARIA attributes a screen reader cares about.
/// All strings default to empty; widgets are expected to set `role`
/// in their constructors and callers set the context-specific strings
/// at the point the widget is wired into a menu or HUD.
struct UIAccessibleInfo
{
    UIAccessibleRole role  = UIAccessibleRole::Unknown;
    std::string label;        ///< Human-readable name ("Play Game", "Master Volume").
    std::string description;  ///< Longer explanation ("Start a new campaign").
    std::string hint;         ///< Interaction hint ("press Enter to activate").
    std::string value;        ///< Current state ("75%", "Checked", "High").
};

/// @brief One entry in a flat accessibility enumeration of a UI tree.
///
/// Produced by `UIElement::collectAccessible()` and
/// `UICanvas::collectAccessible()`. Carries the metadata plus the
/// widget's `interactive` flag so a TTS bridge can decide whether
/// to announce Tab-reachable order or just passive content.
struct UIAccessibilitySnapshot
{
    UIAccessibleInfo info;
    bool interactive = false;
};

} // namespace Vestige
