/// @file hierarchy_panel.cpp
/// @brief Scene hierarchy panel implementation.
#include "editor/panels/hierarchy_panel.h"
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
    m_pendingReparentEntityId = 0;
    m_pendingReparentTargetId = 0;
    m_wantCreateEntity = false;
    m_createParentId = 0;

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
            m_pendingReparentEntityId = *(const uint32_t*)payload->Data;
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

    // --- Delete hotkey ---
    if (selection.hasSelection()
        && ImGui::IsWindowFocused()
        && ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        m_pendingDeleteId = selection.getPrimaryId();
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
                    entity->setName(m_renameBuffer);
                    Logger::info("Renamed entity to '" + std::string(m_renameBuffer) + "'");
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

    // --- Process deferred actions (after tree iteration is complete) ---

    if (m_pendingDeleteId != 0)
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

            scene->removeEntity(m_pendingDeleteId);
            Logger::info("Deleted entity ID " + std::to_string(m_pendingDeleteId));
        }
    }

    if (m_pendingReparentEntityId != 0)
    {
        if (scene->reparentEntity(m_pendingReparentEntityId, m_pendingReparentTargetId))
        {
            Logger::info("Reparented entity ID " + std::to_string(m_pendingReparentEntityId));
        }
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
        Logger::info("Created new entity under '" + parent->getName() + "'");
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

    ImVec4 textColor;
    if (!isActive)
    {
        textColor = ImVec4(0.45f, 0.45f, 0.45f, 1.0f);
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
    bool isOpen = ImGui::TreeNodeEx(
        reinterpret_cast<void*>(static_cast<uintptr_t>(id)),
        flags,
        "%s",
        entity.getName().c_str());
    ImGui::PopStyleColor();

    // --- Click to select ---
    if (ImGui::IsItemClicked(0) && !ImGui::IsItemToggledOpen())
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
            m_pendingReparentEntityId = *(const uint32_t*)payload->Data;
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

        if (ImGui::MenuItem("Duplicate", nullptr, false, false))
        {
            // TODO: Phase 5B — requires deep entity cloning
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Delete", "Del"))
        {
            m_pendingDeleteId = id;
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
