// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file validation_panel.cpp
/// @brief Validation panel implementation — scene issue detection.
#include "editor/panels/validation_panel.h"
#include "editor/selection.h"
#include "scene/scene.h"
#include "scene/entity.h"
#include "scene/mesh_renderer.h"

#include <imgui.h>

#include <cmath>

namespace Vestige
{

void ValidationPanel::draw(Scene* scene, Selection& selection)
{
    if (!m_open)
    {
        return;
    }

    if (!ImGui::Begin("Scene Validation", &m_open))
    {
        ImGui::End();
        return;
    }

    if (!scene)
    {
        ImGui::TextDisabled("No active scene");
        ImGui::End();
        return;
    }

    if (ImGui::Button("Validate Scene"))
    {
        validate(*scene);
    }

    ImGui::SameLine();
    if (m_hasValidated)
    {
        if (m_entries.empty())
        {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "No issues found");
        }
        else
        {
            ImGui::Text("%zu issue(s) found", m_entries.size());
        }
    }

    ImGui::Separator();

    // Display results
    for (size_t i = 0; i < m_entries.size(); i++)
    {
        const auto& entry = m_entries[i];

        ImVec4 color;
        const char* icon;
        switch (entry.severity)
        {
            case ValidationSeverity::ERROR_SEV:
                color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                icon = "[ERROR]";
                break;
            case ValidationSeverity::WARNING:
                color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
                icon = "[WARN] ";
                break;
            default:
                color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
                icon = "[INFO] ";
                break;
        }

        ImGui::PushID(static_cast<int>(i));
        ImGui::TextColored(color, "%s", icon);
        ImGui::SameLine();

        // Make the message clickable to select the entity
        if (entry.entityId != 0)
        {
            if (ImGui::Selectable(entry.message.c_str(), false))
            {
                selection.select(entry.entityId);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Click to select entity");
            }
        }
        else
        {
            ImGui::Text("%s", entry.message.c_str());
        }

        ImGui::PopID();
    }

    ImGui::End();
}

void ValidationPanel::validate(Scene& scene)
{
    m_entries.clear();
    m_hasValidated = true;

    scene.forEachEntity([&](Entity& entity)
    {
        glm::vec3 pos = entity.getWorldPosition();

        // Check: entity far from origin
        float dist = std::sqrt(pos.x * pos.x + pos.y * pos.y + pos.z * pos.z);
        if (dist > 1000.0f)
        {
            m_entries.push_back({
                ValidationSeverity::WARNING,
                entity.getName() + " is " + std::to_string(static_cast<int>(dist))
                    + "m from origin",
                entity.getId()
            });
        }

        // Check: MeshRenderer with null mesh
        if (auto* mr = entity.getComponent<MeshRenderer>())
        {
            if (!mr->getMesh())
            {
                m_entries.push_back({
                    ValidationSeverity::ERROR_SEV,
                    entity.getName() + " has MeshRenderer but no mesh assigned",
                    entity.getId()
                });
            }

            // Check: material with no textures
            auto material = mr->getMaterial();
            if (material)
            {
                if (!material->getDiffuseTexture())
                {
                    m_entries.push_back({
                        ValidationSeverity::INFO,
                        entity.getName() + " uses a material with no textures",
                        entity.getId()
                    });
                }
            }
        }
    });
}

} // namespace Vestige
