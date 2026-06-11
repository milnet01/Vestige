// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file localization_debug_panel.cpp
/// @brief Phase 10 Localization L6 — dev-only "missing keys" overlay.
#include "editor/panels/localization_debug_panel.h"

#include "localization/localization_service.h"

#include <imgui.h>

#include <string>
#include <vector>

namespace Vestige
{

void LocalizationDebugPanel::draw(const LocalizationService* service)
{
    if (!m_open)
    {
        return;
    }

    if (!ImGui::Begin("Localization Keys", &m_open))
    {
        ImGui::End();
        return;
    }

    if (!service)
    {
        ImGui::TextDisabled("No LocalizationService registered.");
        ImGui::End();
        return;
    }

    ImGui::Text("Active language: %s", service->languageCode().c_str());
    ImGui::Separator();

    const std::vector<std::string> missing = service->missingKeys();
    if (missing.empty())
    {
        ImGui::TextDisabled("All reference keys are translated.");
    }
    else
    {
        ImGui::Text("%zu key(s) missing — rendering English fallback:",
                    missing.size());
        ImGui::BeginChild("##missing_keys");
        for (const std::string& key : missing)
        {
            ImGui::TextUnformatted(key.c_str());
        }
        ImGui::EndChild();
    }

    ImGui::End();
}

} // namespace Vestige
