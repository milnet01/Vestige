// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file hierarchy_panel.cpp
/// @brief Scene hierarchy panel implementation.
#include "editor/panels/hierarchy_panel.h"
#include "editor/command_history.h"
#include "editor/commands/create_entity_command.h"
#include "editor/commands/delete_entity_command.h"
#include "editor/commands/composite_command.h"
#include "editor/commands/entity_property_command.h"
#include "editor/commands/reparent_command.h"
#include "editor/entity_actions.h"
#include "editor/selection.h"
#include "scene/scene.h"
#include "scene/entity.h"
#include "scene/mesh_renderer.h"
#include "scene/light_component.h"
#include "core/logger.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace Vestige
{

void HierarchyPanel::setCommandHistory(CommandHistory* history)
{
    m_commandHistory = history;
}

void HierarchyPanel::draw(Scene* scene, Selection& selection)
{
    if (!scene)
    {
        ImGui::TextDisabled("No active scene");
        return;
    }

    // --- Search/filter bar ---
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##HierarchySearch", "Search entities...",
        m_searchBuffer, sizeof(m_searchBuffer));

    bool hasFilter = (m_searchBuffer[0] != '\0');
    ImGui::Separator();

    // Scene name header
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", scene->getName().c_str());
    ImGui::Separator();

    // Reset deferred actions each frame
    m_pendingDeleteId = 0;
    m_pendingDeleteSelected = false;
    m_pendingDuplicateId = 0;
    m_pendingReparentEntityId = 0;
    m_pendingReparentTargetId = 0;
    m_wantCreateEntity = false;
    m_createParentId = 0;
    m_wantGroupSelected = false;

    // --- Draw entity tree (root is hidden, show its children) ---
    Entity* root = scene->getRoot();
    for (auto& child : root->getChildren())
    {
        if (hasFilter && !matchesFilter(*child))
        {
            continue;
        }
        drawEntityNode(*child, *scene, selection);
    }

    // --- Empty space: drop target + click-to-deselect + context menu ---
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float dropHeight = (avail.y > 4.0f) ? avail.y : 4.0f;
    ImGui::InvisibleButton("##HierarchyDropArea", ImVec2(avail.x, dropHeight));

    // Click empty space to deselect
    if (ImGui::IsItemClicked(0))
    {
        selection.clearSelection();
    }

    // Drop target: reparent to root
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_ID"))
        {
            m_pendingReparentEntityId = *reinterpret_cast<const uint32_t*>(payload->Data);
            m_pendingReparentTargetId = 0;
        }
        ImGui::EndDragDropTarget();
    }

    // Right-click on empty area: create at root
    if (ImGui::BeginPopupContextItem("##HierarchyEmptyContext"))
    {
        if (ImGui::MenuItem("Create Empty Entity"))
        {
            m_wantCreateEntity = true;
            m_createParentId = 0;
        }
        ImGui::EndPopup();
    }

    // --- F2 hotkey: rename selected entity ---
    if (selection.hasSelection()
        && ImGui::IsWindowFocused()
        && ImGui::IsKeyPressed(ImGuiKey_F2))
    {
        m_renamingEntityId = selection.getPrimaryId();
        Entity* entity = scene->findEntityById(m_renamingEntityId);
        if (entity)
        {
            std::strncpy(m_renameBuffer, entity->getName().c_str(), sizeof(m_renameBuffer) - 1);
            m_renameBuffer[sizeof(m_renameBuffer) - 1] = '\0';
            m_renameFocusSet = false;
            m_wantOpenRename = true;
        }
    }

    // --- Delete hotkey (multi-select aware) ---
    if (selection.hasSelection()
        && ImGui::IsWindowFocused()
        && ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        m_pendingDeleteSelected = true;
    }

    // --- Ctrl+D: duplicate primary selected ---
    if (selection.hasSelection()
        && ImGui::IsWindowFocused()
        && !ImGui::GetIO().WantTextInput
        && ImGui::GetIO().KeyCtrl
        && !ImGui::GetIO().KeyShift
        && ImGui::IsKeyPressed(ImGuiKey_D))
    {
        m_pendingDuplicateId = selection.getPrimaryId();
    }

    // --- Ctrl+G: group selected entities ---
    if (selection.hasSelection()
        && ImGui::IsWindowFocused()
        && !ImGui::GetIO().WantTextInput
        && ImGui::GetIO().KeyCtrl
        && !ImGui::GetIO().KeyShift
        && ImGui::IsKeyPressed(ImGuiKey_G))
    {
        m_wantGroupSelected = true;
    }

    // --- H: toggle visibility of primary selected ---
    if (selection.hasSelection()
        && ImGui::IsWindowFocused()
        && !ImGui::GetIO().WantTextInput
        && !ImGui::GetIO().KeyCtrl
        && ImGui::IsKeyPressed(ImGuiKey_H))
    {
        Entity* entity = scene->findEntityById(selection.getPrimaryId());
        if (entity)
        {
            if (m_commandHistory)
            {
                bool oldVis = entity->isVisible();
                m_commandHistory->execute(
                    std::make_unique<EntityPropertyCommand>(
                        *scene, entity->getId(),
                        EntityProperty::VISIBLE, oldVis, !oldVis));
            }
            else
            {
                entity->setVisible(!entity->isVisible());
            }
        }
    }

    // --- Rename popup (opened from context menu or F2) ---
    if (m_wantOpenRename)
    {
        ImGui::OpenPopup("RenameEntity");
        m_wantOpenRename = false;
    }

    if (ImGui::BeginPopup("RenameEntity"))
    {
        ImGui::Text("Rename:");
        ImGui::SetNextItemWidth(200.0f);
        bool confirmed = ImGui::InputText("##RenameInput", m_renameBuffer, sizeof(m_renameBuffer),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

        if (!m_renameFocusSet)
        {
            ImGui::SetKeyboardFocusHere(-1);
            m_renameFocusSet = true;
        }

        if (confirmed)
        {
            if (m_renamingEntityId != 0 && m_renameBuffer[0] != '\0')
            {
                Entity* entity = scene->findEntityById(m_renamingEntityId);
                if (entity)
                {
                    std::string oldName = entity->getName();
                    std::string newName(m_renameBuffer);
                    if (newName != oldName)
                    {
                        if (m_commandHistory)
                        {
                            m_commandHistory->execute(
                                std::make_unique<EntityPropertyCommand>(
                                    *scene, m_renamingEntityId,
                                    EntityProperty::NAME, oldName, newName));
                        }
                        else
                        {
                            entity->setName(newName);
                        }
                        Logger::info("Renamed entity to '" + newName + "'");
                    }
                }
            }
            m_renamingEntityId = 0;
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            m_renamingEntityId = 0;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // --- Save-as-Prefab popup ---
    if (m_wantOpenPrefabSave)
    {
        ImGui::OpenPopup("SavePrefab");
        m_wantOpenPrefabSave = false;
    }

    if (ImGui::BeginPopup("SavePrefab"))
    {
        ImGui::Text("Prefab Name:");
        ImGui::SetNextItemWidth(200.0f);
        bool confirmed = ImGui::InputText("##PrefabNameInput",
            m_prefabNameBuffer, sizeof(m_prefabNameBuffer),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

        if (!m_prefabNameFocusSet)
        {
            ImGui::SetKeyboardFocusHere(-1);
            m_prefabNameFocusSet = true;
        }

        if (confirmed && m_prefabNameBuffer[0] != '\0')
        {
            m_prefabSaveConfirmed = true;
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            m_pendingSavePrefabId = 0;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // --- Process deferred actions (after tree iteration is complete) ---

    // Multi-select delete (Delete key)
    if (m_pendingDeleteSelected)
    {
        if (m_commandHistory)
        {
            auto ids = selection.getSelectedIds();
            selection.clearSelection();

            if (ids.size() == 1)
            {
                m_commandHistory->execute(
                    std::make_unique<DeleteEntityCommand>(*scene, ids[0]));
            }
            else if (ids.size() > 1)
            {
                std::vector<std::unique_ptr<EditorCommand>> cmds;
                for (uint32_t id : ids)
                {
                    cmds.push_back(
                        std::make_unique<DeleteEntityCommand>(*scene, id));
                }
                m_commandHistory->execute(
                    std::make_unique<CompositeCommand>(
                        "Delete " + std::to_string(ids.size()) + " entities",
                        std::move(cmds)));
            }
        }
        else
        {
            EntityActions::deleteSelectedEntities(*scene, selection);
        }
    }
    // Single-entity delete (context menu)
    else if (m_pendingDeleteId != 0)
    {
        Entity* toDelete = scene->findEntityById(m_pendingDeleteId);
        if (toDelete)
        {
            // Remove deleted entity and all descendants from selection
            std::vector<uint32_t> idsToRemove;
            collectDescendantIds(*toDelete, idsToRemove);
            for (uint32_t id : idsToRemove)
            {
                if (selection.isSelected(id))
                {
                    selection.toggleSelection(id);
                }
            }

            if (m_commandHistory)
            {
                m_commandHistory->execute(
                    std::make_unique<DeleteEntityCommand>(*scene, m_pendingDeleteId));
            }
            else
            {
                scene->removeEntity(m_pendingDeleteId);
            }
            Logger::info("Deleted entity ID " + std::to_string(m_pendingDeleteId));
        }
    }

    // Duplicate (Ctrl+D or context menu)
    if (m_pendingDuplicateId != 0)
    {
        Entity* clone = EntityActions::duplicateEntity(*scene, selection, m_pendingDuplicateId);
        if (clone && m_commandHistory)
        {
            m_commandHistory->execute(
                std::make_unique<CreateEntityCommand>(*scene, clone->getId()));
        }
    }

    if (m_pendingReparentEntityId != 0)
    {
        if (m_commandHistory)
        {
            m_commandHistory->execute(
                std::make_unique<ReparentCommand>(
                    *scene, m_pendingReparentEntityId, m_pendingReparentTargetId));
        }
        else
        {
            scene->reparentEntity(m_pendingReparentEntityId, m_pendingReparentTargetId);
        }
        Logger::info("Reparented entity ID " + std::to_string(m_pendingReparentEntityId));
    }

    if (m_wantCreateEntity)
    {
        Entity* parent = nullptr;
        if (m_createParentId != 0)
        {
            parent = scene->findEntityById(m_createParentId);
        }
        if (!parent)
        {
            parent = scene->getRoot();
        }

        auto newEntity = std::make_unique<Entity>("New Entity");
        uint32_t newId = newEntity->getId();
        parent->addChild(std::move(newEntity));
        selection.select(newId);

        if (m_commandHistory)
        {
            m_commandHistory->execute(
                std::make_unique<CreateEntityCommand>(*scene, newId));
        }
        Logger::info("Created new entity under '" + parent->getName() + "'");
    }

    // Group selected entities (Ctrl+G)
    if (m_wantGroupSelected)
    {
        EntityActions::groupEntities(*scene, selection);
    }
}

void HierarchyPanel::drawEntityNode(Entity& entity, Scene& scene, Selection& selection)
{
    uint32_t id = entity.getId();
    bool isSelected = selection.isSelected(id);
    bool hasChildren = !entity.getChildren().empty();
    bool isActive = entity.isActive();

    // Tree node flags
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                             | ImGuiTreeNodeFlags_SpanAvailWidth
                             | ImGuiTreeNodeFlags_FramePadding;

    if (isSelected)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    if (!hasChildren)
    {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    // Auto-expand when searching
    if (m_searchBuffer[0] != '\0')
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }

    // Color code by entity type
    bool isLight = entity.hasComponent<DirectionalLightComponent>()
                || entity.hasComponent<PointLightComponent>()
                || entity.hasComponent<SpotLightComponent>();
    bool hasMesh = entity.hasComponent<MeshRenderer>();
    bool isEmissive = entity.hasComponent<EmissiveLightComponent>();

    bool isEntityVisible = entity.isVisible();

    ImVec4 textColor;
    if (!isActive)
    {
        textColor = ImVec4(0.45f, 0.45f, 0.45f, 1.0f);
    }
    else if (!isEntityVisible)
    {
        textColor = ImVec4(0.50f, 0.50f, 0.50f, 1.0f);
    }
    else if (isLight)
    {
        textColor = ImVec4(1.0f, 0.85f, 0.4f, 1.0f);
    }
    else if (isEmissive)
    {
        textColor = ImVec4(1.0f, 0.6f, 0.3f, 1.0f);
    }
    else if (hasMesh)
    {
        textColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    else
    {
        textColor = ImVec4(0.7f, 0.7f, 0.8f, 1.0f);
    }

    ImGui::PushStyleColor(ImGuiCol_Text, textColor);
    ImGui::SetNextItemAllowOverlap();
    bool isOpen = ImGui::TreeNodeEx(
        reinterpret_cast<void*>(static_cast<uintptr_t>(id)),
        flags,
        "%s",
        entity.getName().c_str());
    ImGui::PopStyleColor();

    // Save tree node interaction state before drawing overlapping buttons
    bool treeNodeClicked = ImGui::IsItemClicked(0) && !ImGui::IsItemToggledOpen();

    // --- Visibility / Lock icon buttons (right-aligned on the row) ---
    bool visClicked = false;
    bool lockClicked = false;
    {
        ImGuiStyle& style = ImGui::GetStyle();
        float buttonHeight = ImGui::GetFrameHeight();
        float buttonWidth = buttonHeight;  // Square buttons
        float spacing = style.ItemInnerSpacing.x;
        float rightEdge = ImGui::GetWindowContentRegionMax().x;
        float buttonsX = rightEdge - (buttonWidth * 2 + spacing);

        ImGui::SameLine(buttonsX);
        ImGui::PushID(static_cast<int>(id));

        // Transparent button background
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));

        // Visibility toggle
        if (isEntityVisible)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.75f, 0.75f, 1.0f));
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
        }
        visClicked = ImGui::Button(isEntityVisible ? "V##vis" : "-##vis",
            ImVec2(buttonWidth, buttonHeight));
        ImGui::PopStyleColor();

        ImGui::SameLine(0.0f, spacing);

        // Lock toggle
        if (entity.isLocked())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.2f, 1.0f));
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
        }
        lockClicked = ImGui::Button(entity.isLocked() ? "L##lock" : "U##lock",
            ImVec2(buttonWidth, buttonHeight));
        ImGui::PopStyleColor();

        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::PopID();
    }

    // Handle visibility/lock toggles
    if (visClicked)
    {
        if (m_commandHistory)
        {
            bool oldVis = entity.isVisible();
            m_commandHistory->execute(
                std::make_unique<EntityPropertyCommand>(
                    scene, entity.getId(),
                    EntityProperty::VISIBLE, oldVis, !oldVis));
        }
        else
        {
            entity.setVisible(!entity.isVisible());
        }
    }
    if (lockClicked)
    {
        if (m_commandHistory)
        {
            bool oldLocked = entity.isLocked();
            m_commandHistory->execute(
                std::make_unique<EntityPropertyCommand>(
                    scene, entity.getId(),
                    EntityProperty::LOCKED, oldLocked, !oldLocked));
        }
        else
        {
            entity.setLocked(!entity.isLocked());
        }
    }

    // --- Click to select (suppress if button was clicked) ---
    if (treeNodeClicked && !visClicked && !lockClicked)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyShift)
        {
            selection.addToSelection(id);
        }
        else if (io.KeyCtrl)
        {
            selection.toggleSelection(id);
        }
        else
        {
            selection.select(id);
        }
    }

    // --- Drag source ---
    if (ImGui::BeginDragDropSource())
    {
        ImGui::SetDragDropPayload("ENTITY_ID", &id, sizeof(uint32_t));
        ImGui::Text("%s", entity.getName().c_str());
        ImGui::EndDragDropSource();
    }

    // --- Drop target ---
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_ID"))
        {
            m_pendingReparentEntityId = *reinterpret_cast<const uint32_t*>(payload->Data);
            m_pendingReparentTargetId = id;
        }
        ImGui::EndDragDropTarget();
    }

    // --- Right-click context menu ---
    if (ImGui::BeginPopupContextItem())
    {
        if (ImGui::MenuItem("Create Empty Child"))
        {
            m_wantCreateEntity = true;
            m_createParentId = id;
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Rename", "F2"))
        {
            m_renamingEntityId = id;
            std::strncpy(m_renameBuffer, entity.getName().c_str(), sizeof(m_renameBuffer) - 1);
            m_renameBuffer[sizeof(m_renameBuffer) - 1] = '\0';
            m_renameFocusSet = false;
            m_wantOpenRename = true;
        }

        if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
        {
            m_pendingDuplicateId = id;
        }

        if (ImGui::MenuItem("Save as Prefab..."))
        {
            m_pendingSavePrefabId = id;
            std::strncpy(m_prefabNameBuffer, entity.getName().c_str(),
                sizeof(m_prefabNameBuffer) - 1);
            m_prefabNameBuffer[sizeof(m_prefabNameBuffer) - 1] = '\0';
            m_prefabNameFocusSet = false;
            m_wantOpenPrefabSave = true;
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Delete", "Del"))
        {
            m_pendingDeleteId = id;
        }

        ImGui::Separator();

        if (ImGui::MenuItem(entity.isVisible() ? "Hide" : "Show", "H"))
        {
            if (m_commandHistory)
            {
                bool oldVis = entity.isVisible();
                m_commandHistory->execute(
                    std::make_unique<EntityPropertyCommand>(
                        scene, entity.getId(),
                        EntityProperty::VISIBLE, oldVis, !oldVis));
            }
            else
            {
                entity.setVisible(!entity.isVisible());
            }
        }

        if (ImGui::MenuItem(entity.isLocked() ? "Unlock" : "Lock"))
        {
            if (m_commandHistory)
            {
                bool oldLocked = entity.isLocked();
                m_commandHistory->execute(
                    std::make_unique<EntityPropertyCommand>(
                        scene, entity.getId(),
                        EntityProperty::LOCKED, oldLocked, !oldLocked));
            }
            else
            {
                entity.setLocked(!entity.isLocked());
            }
        }

        ImGui::EndPopup();
    }

    // --- Recurse into children ---
    if (isOpen && hasChildren)
    {
        for (auto& child : entity.getChildren())
        {
            if (m_searchBuffer[0] != '\0' && !matchesFilter(*child))
            {
                continue;
            }
            drawEntityNode(*child, scene, selection);
        }
        ImGui::TreePop();
    }
}

bool HierarchyPanel::matchesFilter(const Entity& entity) const
{
    if (m_searchBuffer[0] == '\0')
    {
        return true;
    }

    // Case-insensitive search in entity name
    const std::string& name = entity.getName();
    std::string lowerName = name;
    std::string lowerFilter(m_searchBuffer);
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lowerName.find(lowerFilter) != std::string::npos)
    {
        return true;
    }

    // Check children recursively (show ancestors of matching entities)
    for (const auto& child : entity.getChildren())
    {
        if (matchesFilter(*child))
        {
            return true;
        }
    }

    return false;
}

void HierarchyPanel::collectDescendantIds(const Entity& entity, std::vector<uint32_t>& ids) const
{
    ids.push_back(entity.getId());
    for (const auto& child : entity.getChildren())
    {
        collectDescendantIds(*child, ids);
    }
}

} // namespace Vestige
