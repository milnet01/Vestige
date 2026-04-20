// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file sprite_panel.cpp
/// @brief SpritePanel implementation.
#include "editor/panels/sprite_panel.h"

#include "editor/selection.h"
#include "renderer/sprite_atlas.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "scene/sprite_component.h"

#include <imgui.h>

namespace Vestige
{

bool SpritePanel::loadAtlasFromPath(const std::string& jsonPath)
{
    auto atlas = SpriteAtlas::loadFromJson(jsonPath);
    if (!atlas)
    {
        m_lastLoadError = "Failed to parse " + jsonPath;
        return false;
    }
    m_atlas = std::move(atlas);
    m_lastLoadedPath = jsonPath;
    m_lastLoadError.clear();
    return true;
}

void SpritePanel::draw(Scene* scene, Selection* selection)
{
    if (!m_visible)
    {
        return;
    }

    if (!ImGui::Begin("Sprite Atlas", &m_visible))
    {
        ImGui::End();
        return;
    }

    // --- Load row ---
    static char pathBuf[512] = {0};
    ImGui::InputText("JSON path", pathBuf, sizeof(pathBuf));
    ImGui::SameLine();
    if (ImGui::Button("Load"))
    {
        loadAtlasFromPath(pathBuf);
    }

    if (!m_lastLoadError.empty())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s",
                           m_lastLoadError.c_str());
    }

    if (!m_atlas)
    {
        ImGui::TextDisabled("No atlas loaded. TexturePacker JSON-Array "
                            "or JSON-Hash both work.");
        ImGui::End();
        return;
    }

    // --- Loaded atlas info ---
    ImGui::Separator();
    ImGui::Text("Image: %s", m_atlas->imageName().c_str());
    ImGui::Text("Size:  %d × %d",
                static_cast<int>(m_atlas->atlasSize().x),
                static_cast<int>(m_atlas->atlasSize().y));
    ImGui::Text("Frames: %zu", m_atlas->frameCount());

    if (ImGui::CollapsingHeader("Frames", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::BeginChild("##framelist", ImVec2(0, 200), true))
        {
            for (const auto& name : m_atlas->frameNames())
            {
                if (ImGui::Selectable(name.c_str()))
                {
                    m_selectedFrame = name;
                }
            }
        }
        ImGui::EndChild();
    }

    // --- Assign to selected entity ---
    ImGui::Separator();
    Entity* selectedEntity = nullptr;
    if (selection && scene)
    {
        selectedEntity = selection->getPrimaryEntity(*scene);
    }
    if (!selectedEntity)
    {
        ImGui::TextDisabled("Select an entity to assign the atlas to.");
    }
    else
    {
        ImGui::Text("Selected: %s", selectedEntity->getName().c_str());
        auto* sprite = selectedEntity->getComponent<SpriteComponent>();
        if (!sprite)
        {
            if (ImGui::Button("Add SpriteComponent"))
            {
                sprite = selectedEntity->addComponent<SpriteComponent>();
            }
        }
        if (sprite)
        {
            if (ImGui::Button("Assign atlas to selected"))
            {
                sprite->atlas = m_atlas;
                if (sprite->frameName.empty() && !m_atlas->frameNames().empty())
                {
                    sprite->frameName = m_atlas->frameNames().front();
                }
            }
            if (!m_selectedFrame.empty() && sprite->atlas == m_atlas)
            {
                ImGui::SameLine();
                if (ImGui::Button("Use selected frame"))
                {
                    sprite->frameName = m_selectedFrame;
                }
            }
        }
    }

    ImGui::End();
}

} // namespace Vestige
