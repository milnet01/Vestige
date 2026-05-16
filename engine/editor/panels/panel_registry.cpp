// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file panel_registry.cpp
/// @brief Implementation of the editor PanelRegistry.

#include "editor/panels/panel_registry.h"
#include "editor/panels/i_panel.h"

#include <imgui.h>

#include <algorithm>

namespace Vestige
{

void PanelRegistry::registerPanel(IPanel* panel)
{
    if (panel == nullptr)
    {
        return;
    }
    if (std::find(m_panels.begin(), m_panels.end(), panel) != m_panels.end())
    {
        return;
    }
    m_panels.push_back(panel);
}

IPanel* PanelRegistry::panelAt(std::size_t i) const
{
    if (i >= m_panels.size())
    {
        return nullptr;
    }
    return m_panels[i];
}

bool PanelRegistry::drawMenuToggle(IPanel& panel) const
{
    bool open = panel.isOpen();
    const bool clicked = ImGui::MenuItem(panel.displayName(), panel.shortcut(), &open);
    if (clicked)
    {
        panel.setOpen(open);
    }
    return clicked;
}

} // namespace Vestige
