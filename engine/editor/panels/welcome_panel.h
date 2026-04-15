// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file welcome_panel.h
/// @brief Welcome / getting started panel shown on first launch.
#pragma once

#include <string>

namespace Vestige
{

/// @brief Displays a welcome screen with keyboard shortcuts and workflow tips.
///
/// Shows automatically on first launch. Can be re-opened via Help > Welcome Screen.
class WelcomePanel
{
public:
    /// @brief Initializes the panel, checking if it should auto-show.
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
