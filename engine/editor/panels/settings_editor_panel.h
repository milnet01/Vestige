// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file settings_editor_panel.h
/// @brief Phase 10 slice 13.5b — ImGui editor panel wrapping
///        `SettingsEditor`.
///
/// Five category tabs (Display / Audio / Controls / Gameplay /
/// Accessibility) + a footer with per-category restore buttons,
/// Restore All, Revert, Apply, and a dirty indicator.
///
/// Live-apply: every widget mutation goes through
/// `SettingsEditor::mutate()` so subsystems update immediately.
/// Apply persists to disk. Revert rolls back the live preview.
///
/// Keybinding rebind capture (click-to-rebind) lands in slice 13.5c —
/// this panel shows the binding table as read-only with a "Rebind…"
/// button per slot that opens a follow-on modal.
#pragma once

#include <filesystem>

namespace Vestige
{

class SettingsEditor;
class InputActionMap;

class SettingsEditorPanel
{
public:
    /// @brief Attach to a live `SettingsEditor`. Caller keeps
    ///        ownership. Optional `inputMap` enables the Controls
    ///        tab's rebind table; if null, Controls renders a
    ///        one-line "no input map attached" notice.
    void initialize(SettingsEditor* editor,
                     InputActionMap* inputMap,
                     std::filesystem::path settingsPath);

    /// @brief Opens the panel. Invoked by the Help menu entry.
    void open() { m_open = true; }

    /// @brief Renders the panel. Call each editor frame.
    void draw();

    bool isOpen() const { return m_open; }

private:
    void drawDisplayTab();
    void drawAudioTab();
    void drawControlsTab();
    void drawGameplayTab();
    void drawAccessibilityTab();
    void drawFooter();

    SettingsEditor*         m_editor       = nullptr;
    InputActionMap*         m_inputMap     = nullptr;
    std::filesystem::path   m_settingsPath;
    bool                    m_open         = false;
    int                     m_activeTab    = 0;
};

} // namespace Vestige
