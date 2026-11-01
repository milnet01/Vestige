// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file welcome_panel.h
/// @brief Welcome / getting started panel, opened from Help > Welcome Screen.
#pragma once

#include <string>

namespace Vestige
{

/// @brief Displays a welcome screen with keyboard shortcuts and workflow tips.
///
/// Does not open automatically (the first-run wizard owns first launch).
/// Opened via Help > Welcome Screen. Closing it writes the legacy
/// `welcome_shown` flag file, which Settings promotes to
/// `onboarding.hasCompletedFirstRun` on the next load.
class WelcomePanel
{
public:
    /// @brief Initializes the panel and reads the legacy flag file's presence.
    /// @param configDir Directory for config files (e.g., ~/.config/vestige).
    void initialize(const std::string& configDir);

    /// @brief Draws the welcome panel.
    void draw();

    /// @brief Opens the welcome panel (e.g., from Help menu).
    void open() { m_open = true; }

    /// @brief Returns true if the panel is currently visible.
    bool isOpen() const { return m_open; }

private:
    void drawShortcutsSection();
    void drawModesSection();
    void drawToolsSection();
    void markAsShown();

    bool m_open = false;
    bool m_shownBefore = false;
    std::string m_configDir;
    std::string m_flagPath;
};

} // namespace Vestige
