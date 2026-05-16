// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file i_panel.h
/// @brief Minimal interface for togglable editor panels.
#pragma once

namespace Vestige
{

/// @brief Common surface for editor panels that appear in the Window menu.
///
/// Phase 10.9 Slice 12 Ed5 — `PanelRegistry::drawMenuToggle` calls these four
/// hooks to emit the Window-menu entry for a panel without the call site
/// knowing the concrete type. Panels that aren't togglable from the Window
/// menu (welcome dialog, settings dialog, template dialog, first-run wizard,
/// asset import dialog) intentionally do not inherit `IPanel` — they have
/// open()/close() driven from elsewhere and don't need a polymorphic toggle.
///
/// `displayName()` is the menu label. `shortcut()` is an optional accelerator
/// hint shown in the menu — return nullptr if the panel has none.
class IPanel
{
public:
    virtual ~IPanel() = default;

    virtual const char* displayName() const = 0;
    virtual bool isOpen() const = 0;
    virtual void setOpen(bool open) = 0;

    virtual const char* shortcut() const { return nullptr; }
};

} // namespace Vestige
