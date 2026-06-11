// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file localization_debug_panel.h
/// @brief Phase 10 Localization L6 — dev-only "missing keys" overlay.
///        See docs/phases/phase_10_localization_design.md § 5.7.
#pragma once

#include "editor/panels/i_panel.h"

namespace Vestige
{

class LocalizationService;

/// @brief Lists keys present in the reference language (en.json) but missing
///        from the active language — the translator worklist. These render the
///        English fallback at runtime (design decision 1), so they are not
///        runtime bugs, just untranslated strings.
///
/// Developer-facing, English-only: the editor itself is out of i18n scope
/// (design § 6 deferral 6). Togglable from the editor Window menu via the
/// shared `IPanel` registry.
class LocalizationDebugPanel final : public IPanel
{
public:
    const char* displayName() const override { return "Localization Keys"; }
    bool isOpen() const override { return m_open; }
    void setOpen(bool open) override { m_open = open; }

    /// @brief Renders the panel. `service` may be null (no registry wired,
    ///        e.g. a bare editor preview) — a one-line notice is shown then.
    ///        Call each editor frame.
    void draw(const LocalizationService* service);

private:
    bool m_open = false;
};

} // namespace Vestige
