// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file panel_registry.h
/// @brief Registry of togglable editor panels for centralised Window-menu wiring.
#pragma once

#include <cstddef>
#include <vector>

namespace Vestige
{

class IPanel;

/// @brief Collects togglable panels and centralises the Window-menu toggle wiring.
///
/// Phase 10.9 Slice 12 Ed5 — pre-Ed5 every togglable panel needed a 4-line
/// `bool xxxOpen = m_xxxPanel.isOpen(); if (ImGui::MenuItem(...)) m_xxxPanel.setOpen(xxxOpen);`
/// block in `editor.cpp`. With the registry, panels register themselves at editor
/// initialise time and the Window-menu loop is one `drawMenuToggle(panel)` call
/// per entry. The loop is not written yet: each Window-menu entry is a
/// hand-written `drawMenuToggle` line in `editor.cpp`. A new togglable panel
/// inherits `IPanel`, is registered in `Editor::initialize`, and also needs
/// its own `drawMenuToggle` line there to appear in the menu.
///
/// The registry stores raw pointers — panels are owned by `Editor` as members,
/// so their lifetimes outlive the registry. Registering a panel twice is a no-op.
class PanelRegistry
{
public:
    /// @brief Adds a panel to the registry. Idempotent; null panels are ignored.
    void registerPanel(IPanel* panel);

    /// @brief Emits one `ImGui::MenuItem` entry for the panel and toggles its
    ///        open state on click. Returns true when the user clicked the item
    ///        this frame (mirrors `ImGui::MenuItem`'s return contract).
    bool drawMenuToggle(IPanel& panel) const;

    /// @brief Number of registered panels. For tests + diagnostics.
    std::size_t panelCount() const { return m_panels.size(); }

    /// @brief Returns the panel at index `i`, or nullptr if out of range.
    IPanel* panelAt(std::size_t i) const;

    /// @brief Removes all registered panels. For tests + editor shutdown.
    void clear() { m_panels.clear(); }

private:
    std::vector<IPanel*> m_panels;
};

} // namespace Vestige
