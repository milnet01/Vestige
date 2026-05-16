// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file inspector_panel.cpp
/// @brief Inspector panel implementation — property editing for selected entities.
#include "editor/panels/inspector_panel.h"
#include "editor/command_history.h"
#include "editor/commands/transform_command.h"
#include "editor/commands/entity_property_command.h"
#include "editor/selection.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "scene/mesh_renderer.h"
#include "scene/light_component.h"
#include "scene/camera_component.h"
#include "scene/particle_emitter.h"
#include "scene/water_surface.h"
#include "audio/audio_source_component.h"
#include "physics/cloth_component.h"
#include "scene/pressure_plate_component.h"
#include "physics/rigid_body.h"
#include "physics/fabric_material.h"
#include "editor/commands/component_property_command.h"
#include "editor/commands/particle_property_command.h"
#include "editor/widgets/curve_editor_widget.h"
#include "editor/widgets/gradient_editor_widget.h"
#include "renderer/material.h"
#include "renderer/texture.h"
#include "renderer/light_utils.h"
#include "utils/material_library.h"

#include <imgui.h>
#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Vestige
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// @brief Phase 10.9 Slice 12 Ed1 — per-widget activation tracker.
///
/// Pre-Ed1 the inspector blocks used
///     `if (changed && ImGui::IsItemDeactivatedAfterEdit())`
/// at the *end* of a multi-widget block. `IsItemDeactivatedAfterEdit()`
/// only queries the most recently submitted item, so drag-release
/// events on every widget except the last were silently dropped from
/// the undo history — releasing a drag on "Rate" recorded nothing,
/// only the final widget's release fired the push.
///
/// This tracker is called immediately after each widget so per-widget
/// activation/deactivation states are aggregated correctly across the
/// whole block. `shouldCommit()` then triggers undo on either:
///   - drag end (anyDeactivated → push at end-of-drag, one entry per drag), or
///   - instant change with no drag (changed && !anyActivated → checkbox/
///     combo / slider-tap that doesn't go through an active state).
///
/// Ed2 extends the same tracker to the water / cloth / rigid-body /
/// emissive-light / material inspectors — moved to the top of the file
/// because those `draw*` methods are defined above the original Ed1
/// location.
struct EditTracker
{
    bool anyActivated   = false;
    bool anyDeactivated = false;
    bool changed        = false;

    /// Track the just-submitted ImGui item.
    void track(bool widgetChanged)
    {
        changed |= widgetChanged;
        if (ImGui::IsItemActivated())             anyActivated   = true;
        if (ImGui::IsItemDeactivatedAfterEdit())  anyDeactivated = true;
    }

    /// True when the block should record a single undo entry this frame.
    bool shouldCommit() const
    {
        return anyDeactivated || (changed && !anyActivated);
    }
};

/// @brief Draws a labelled separator for component sections.
static bool drawComponentHeader(const char* label, bool canRemove = false)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 4.0f));
    bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleVar();
    (void)canRemove; // reserved for future "Remove Component" button
    return open;
}

/// @brief Return state from drawVec3Control for undo bracketing.
struct Vec3EditState
{
    bool changed = false;              ///< Any value was modified this frame.
    bool dragActivated = false;        ///< A DragFloat started being edited.
    bool dragDeactivatedAfterEdit = false; ///< A DragFloat finished editing with changes.
};

/// @brief Draws a vec3 with 3 coloured drag fields (red/green/blue labels).
static Vec3EditState drawVec3Control(const char* label, glm::vec3& values,
                                     float resetValue = 0.0f, float speed = 0.1f)
{
    Vec3EditState state;

    ImGui::PushID(label);

    // Label on left — use narrow column so XYZ fields have room
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, 60.0f);
    ImGui::Text("%s", label);
    ImGui::NextColumn();

    float lineHeight = ImGui::GetFrameHeight();
    ImVec2 buttonSize = { lineHeight, lineHeight };
    float spacing = 4.0f;

    // Compute drag field widths dynamically: subtract button and gap space from available width
    float totalWidth = ImGui::GetContentRegionAvail().x;
    float buttonsSpace = 3.0f * lineHeight;
    float gapsSpace = 5.0f * spacing;  // 3 button-drag gaps + 2 between-group gaps
    float perDragWidth = std::max(20.0f, (totalWidth - buttonsSpace - gapsSpace) / 3.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, spacing));

    // --- X (red) ---
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.85f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
    if (ImGui::Button("X", buttonSize))
    {
        values.x = resetValue;
        state.changed = true;
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(perDragWidth);
    if (ImGui::DragFloat("##X", &values.x, speed))
    {
        state.changed = true;
    }
    if (ImGui::IsItemActivated()) state.dragActivated = true;
    if (ImGui::IsItemDeactivatedAfterEdit()) state.dragDeactivatedAfterEdit = true;
    ImGui::SameLine();

    // --- Y (green) ---
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.15f, 0.6f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.2f, 0.75f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.15f, 0.6f, 0.15f, 1.0f));
    if (ImGui::Button("Y", buttonSize))
    {
        values.y = resetValue;
        state.changed = true;
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(perDragWidth);
    if (ImGui::DragFloat("##Y", &values.y, speed))
    {
        state.changed = true;
    }
    if (ImGui::IsItemActivated()) state.dragActivated = true;
    if (ImGui::IsItemDeactivatedAfterEdit()) state.dragDeactivatedAfterEdit = true;
    ImGui::SameLine();

    // --- Z (blue) ---
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.15f, 0.15f, 0.7f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.2f, 0.2f, 0.85f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.15f, 0.15f, 0.7f, 1.0f));
    if (ImGui::Button("Z", buttonSize))
    {
        values.z = resetValue;
        state.changed = true;
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(20.0f, ImGui::GetContentRegionAvail().x));
    if (ImGui::DragFloat("##Z", &values.z, speed))
    {
        state.changed = true;
    }
    if (ImGui::IsItemActivated()) state.dragActivated = true;
    if (ImGui::IsItemDeactivatedAfterEdit()) state.dragDeactivatedAfterEdit = true;

    ImGui::PopStyleVar();
    ImGui::Columns(1);
    ImGui::PopID();

    return state;
}

// ---------------------------------------------------------------------------
// Main draw
// ---------------------------------------------------------------------------

void InspectorPanel::initialize(const std::string& assetPath)
{
    m_assetPath = assetPath;
    m_materialPreview.initialize(assetPath);
}

void InspectorPanel::draw(Scene* scene, Selection& selection)
{
    m_currentScene = scene;

    if (!scene || !selection.hasSelection())
    {
        ImGui::TextWrapped("Select an entity to inspect its properties.");
        return;
    }

    Entity* entity = selection.getPrimaryEntity(*scene);
    if (!entity)
    {
        ImGui::Text("Selected entity not found (ID %u)", selection.getPrimaryId());
        return;
    }

    // --- Entity header: name + active toggle ---
    {
        bool oldActive = entity->isActive();
        bool isActive = oldActive;
        if (ImGui::Checkbox("##Active", &isActive))
        {
            if (m_commandHistory)
            {
                m_commandHistory->execute(
                    std::make_unique<EntityPropertyCommand>(
                        *scene, entity->getId(),
                        EntityProperty::ACTIVE, oldActive, isActive));
            }
            else
            {
                entity->setActive(isActive);
            }
        }
        ImGui::SameLine();

        char nameBuf[256];
        std::strncpy(nameBuf, entity->getName().c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##EntityName", nameBuf, sizeof(nameBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue))
        {
            std::string oldName = entity->getName();
            std::string newName(nameBuf);
            if (!newName.empty() && newName != oldName)
            {
                if (m_commandHistory)
                {
                    m_commandHistory->execute(
                        std::make_unique<EntityPropertyCommand>(
                            *scene, entity->getId(),
                            EntityProperty::NAME, oldName, newName));
                }
                else
                {
                    entity->setName(newName);
                }
            }
        }
    }

    ImGui::Spacing();
    ImGui::Text("ID: %u", entity->getId());
    ImGui::Separator();
    ImGui::Spacing();

    // --- Transform ---
    drawTransform(*entity);

    // --- MeshRenderer ---
    if (entity->hasComponent<MeshRenderer>())
    {
        drawMeshRenderer(*entity);
    }

    // --- Camera ---
    if (entity->hasComponent<CameraComponent>())
    {
        drawCameraComponent(*entity);
    }

    // --- Light components ---
    if (entity->hasComponent<DirectionalLightComponent>())
    {
        drawDirectionalLight(*entity);
    }
    if (entity->hasComponent<PointLightComponent>())
    {
        drawPointLight(*entity);
    }
    if (entity->hasComponent<SpotLightComponent>())
    {
        drawSpotLight(*entity);
    }
    if (entity->hasComponent<EmissiveLightComponent>())
    {
        drawEmissiveLight(*entity);
    }

    // --- Particle Emitter ---
    if (entity->hasComponent<ParticleEmitterComponent>())
    {
        drawParticleEmitter(*entity);
    }

    // --- Audio Source (Phase 10.9 W15 — closes F3's round-trip gap visibly) ---
    if (entity->hasComponent<AudioSourceComponent>())
    {
        drawAudioSource(*entity);
    }

    // --- Water Surface ---
    if (entity->hasComponent<WaterSurfaceComponent>())
    {
        drawWaterSurface(*entity);
    }

    // --- Rigid Body ---
    if (entity->hasComponent<RigidBody>())
    {
        drawRigidBody(*entity);
    }

    // --- Cloth Simulation ---
    if (entity->hasComponent<ClothComponent>())
    {
        drawClothComponent(*entity);
    }

    if (entity->hasComponent<PressurePlateComponent>())
    {
        drawPressurePlate(*entity);
    }

    // --- Multi-selection info ---
    const auto& ids = selection.getSelectedIds();
    if (ids.size() > 1)
    {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("(%zu entities selected — showing primary)",
                            ids.size());
    }
}

// ---------------------------------------------------------------------------
// Transform
// ---------------------------------------------------------------------------

void InspectorPanel::drawTransform(Entity& entity)
{
    if (!drawComponentHeader("Transform"))
    {
        return;
    }

    if (entity.transform.hasMatrixOverride())
    {
        ImGui::TextWrapped("Transform is overridden by a direct matrix "
                           "(e.g. from glTF). Clear override to edit TRS.");
        if (ImGui::Button("Clear Matrix Override"))
        {
            entity.transform.clearMatrixOverride();
        }
        ImGui::Spacing();
        return;
    }

    // Reset bracket if entity changed
    if (entity.getId() != m_editEntityId)
    {
        m_transformEditActive = false;
        m_editEntityId = entity.getId();
    }

    // Capture transform before any controls modify it
    glm::vec3 prePos = entity.transform.position;
    glm::vec3 preRot = entity.transform.rotation;
    glm::vec3 preScale = entity.transform.scale;

    auto posState = drawVec3Control("Position", entity.transform.position, 0.0f, 0.05f);
    auto rotState = drawVec3Control("Rotation", entity.transform.rotation, 0.0f, 1.0f);
    auto scaleState = drawVec3Control("Scale",  entity.transform.scale,    1.0f, 0.02f);

    // Aggregate activation/deactivation across all 3 vec3 controls
    bool anyActivated = posState.dragActivated
                     || rotState.dragActivated
                     || scaleState.dragActivated;
    bool anyDeactivated = posState.dragDeactivatedAfterEdit
                       || rotState.dragDeactivatedAfterEdit
                       || scaleState.dragDeactivatedAfterEdit;

    // --- End bracket first (handles rapid switch between fields) ---
    if (anyDeactivated && m_transformEditActive)
    {
        m_transformEditActive = false;
        bool changed = (entity.transform.position != m_editOldPos)
                    || (entity.transform.rotation != m_editOldRot)
                    || (entity.transform.scale != m_editOldScale);
        if (changed && m_commandHistory && m_currentScene)
        {
            m_commandHistory->execute(std::make_unique<TransformCommand>(
                *m_currentScene, m_editEntityId,
                m_editOldPos, m_editOldRot, m_editOldScale,
                entity.transform.position, entity.transform.rotation,
                entity.transform.scale));
        }
    }

    // --- Start new bracket ---
    if (anyActivated && !m_transformEditActive)
    {
        m_transformEditActive = true;
        m_editOldPos = prePos;
        m_editOldRot = preRot;
        m_editOldScale = preScale;
        m_editEntityId = entity.getId();
    }

    // --- Handle instant changes (reset buttons) when no drag is active ---
    if (!m_transformEditActive)
    {
        bool changed = (entity.transform.position != prePos)
                    || (entity.transform.rotation != preRot)
                    || (entity.transform.scale != preScale);
        if (changed && m_commandHistory && m_currentScene)
        {
            m_commandHistory->execute(std::make_unique<TransformCommand>(
                *m_currentScene, entity.getId(),
                prePos, preRot, preScale,
                entity.transform.position, entity.transform.rotation,
                entity.transform.scale));
        }
    }

    // --- Dimension display (bounding box size in meters) ---
    if (auto* mr = entity.getComponent<MeshRenderer>())
    {
        if (mr->getMesh())
        {
            AABB bounds = mr->getMesh()->getLocalBounds();
            glm::vec3 size = bounds.getSize() * entity.transform.scale;
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                               "Dimensions: %.2f x %.2f x %.2f m",
                               static_cast<double>(size.x),
                               static_cast<double>(size.y),
                               static_cast<double>(size.z));
        }
    }

    ImGui::Spacing();
}

// ---------------------------------------------------------------------------
// MeshRenderer
// ---------------------------------------------------------------------------

void InspectorPanel::drawMeshRenderer(Entity& entity)
{
    auto* mr = entity.getComponent<MeshRenderer>();
    if (!mr)
    {
        return;
    }

    if (!drawComponentHeader("Mesh Renderer"))
    {
        return;
    }

    // Mesh info
    if (mr->getMesh())
    {
        ImGui::Text("Mesh: loaded");
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Mesh: (none)");
    }

    // Casts shadow
    bool casts = mr->castsShadow();
    if (ImGui::Checkbox("Casts Shadow", &casts))
    {
        mr->setCastsShadow(casts);
    }

    // Material
    if (mr->getMaterial())
    {
        ImGui::Spacing();

        // Ed2 — bracket the drawMaterial call for undo. The sub-methods
        // call setters directly inside drawMaterialBlinnPhong / drawMaterialPbr /
        // drawMaterialTextures / drawMaterialTransparency; threading an
        // EditTracker through those signatures would touch five methods.
        // Instead snapshot before the call and detect "any-item-active just
        // released" after — coarser than per-widget but correct for the
        // common case (one drag = one undo entry).
        auto& mat = *mr->getMaterial();
        const bool sameEntityAsLast =
            (m_materialEditingEntity == entity.getId());
        if (!m_materialEditing || !sameEntityAsLast)
        {
            m_materialBefore = mat;
            m_materialEditingEntity = entity.getId();
        }

        drawMaterial(mat);

        const bool isAnyActiveNow = ImGui::IsAnyItemActive();
        const bool wasEditing = m_materialEditing;
        m_materialEditing = isAnyActiveNow;

        if (wasEditing && !isAnyActiveNow && m_commandHistory && m_currentScene)
        {
            const uint32_t editedEntityId = m_materialEditingEntity;
            Material beforeSnapshot = m_materialBefore;
            Material afterSnapshot  = mat;
            m_commandHistory->execute(
                std::make_unique<ComponentPropertyCommand<Material>>(
                    std::move(beforeSnapshot), std::move(afterSnapshot),
                    "Change Material property",
                    [scene = m_currentScene, editedEntityId](const Material& s)
                    {
                        if (auto* e = scene->findEntityById(editedEntityId))
                        {
                            if (auto* mrComp = e->getComponent<MeshRenderer>())
                            {
                                if (auto matPtr = mrComp->getMaterial())
                                {
                                    *matPtr = s;
                                }
                            }
                        }
                    }));
            m_materialBefore = mat;  // baseline for the next edit cycle
        }
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Material: (none)");
    }

    ImGui::Spacing();
}

// ---------------------------------------------------------------------------
// Material
// ---------------------------------------------------------------------------

/// @brief Draws a texture slot with thumbnail preview.
/// Shows a 48x48 thumbnail of the texture if loaded, or a placeholder if not.
static void drawTextureSlot(const char* label,
                            const std::shared_ptr<Texture>& texture)
{
    ImGui::PushID(label);

    constexpr float THUMB_SIZE = 48.0f;

    if (texture && texture->isLoaded())
    {
        ImTextureID texId = static_cast<ImTextureID>(
            static_cast<uintptr_t>(texture->getId()));
        // UV flipped: OpenGL textures are bottom-up
        ImGui::Image(texId, ImVec2(THUMB_SIZE, THUMB_SIZE),
                     ImVec2(0, 1), ImVec2(1, 0));
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s\n%dx%d", label,
                              texture->getWidth(), texture->getHeight());
        }
    }
    else
    {
        // Draw placeholder rectangle
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(pos,
            ImVec2(pos.x + THUMB_SIZE, pos.y + THUMB_SIZE),
            IM_COL32(40, 40, 40, 255));
        drawList->AddRect(pos,
            ImVec2(pos.x + THUMB_SIZE, pos.y + THUMB_SIZE),
            IM_COL32(80, 80, 80, 255));

        const char* noneText = "None";
        ImVec2 textSize = ImGui::CalcTextSize(noneText);
        drawList->AddText(
            ImVec2(pos.x + (THUMB_SIZE - textSize.x) * 0.5f,
                   pos.y + (THUMB_SIZE - textSize.y) * 0.5f),
            IM_COL32(128, 128, 128, 255), noneText);

        ImGui::Dummy(ImVec2(THUMB_SIZE, THUMB_SIZE));
    }

    ImGui::SameLine();
    ImGui::Text("%s", label);

    // Texture filter mode dropdown (only when texture is loaded)
    if (texture && texture->isLoaded())
    {
        static const char* filterNames[] = {
            "Nearest", "Linear", "Trilinear",
            "Anisotropic 4x", "Anisotropic 8x", "Anisotropic 16x"
        };
        int filterIndex = static_cast<int>(texture->getFilterMode());
        ImGui::SetNextItemWidth(140.0f);
        char comboId[64];
        std::snprintf(comboId, sizeof(comboId), "Filter##%s", label);
        if (ImGui::Combo(comboId, &filterIndex, filterNames, 6))
        {
            // const_cast is safe here: we're modifying a shared texture's GL state
            const_cast<Texture*>(texture.get())
                ->setFilterMode(static_cast<TextureFilterMode>(filterIndex));
        }
    }

    ImGui::PopID();
}

void InspectorPanel::drawMaterial(Material& material)
{
    ImGui::PushID("Material");

    if (!drawComponentHeader("Material"))
    {
        ImGui::PopID();
        return;
    }

    // Material name
    if (!material.name.empty())
    {
        ImGui::Text("Name: %s", material.name.c_str());
    }

    // --- Material library: save/load presets ---
    if (m_resourceManager && !m_assetPath.empty())
    {
        const std::string& assetPath = m_assetPath;

        // Save button with name input
        static char saveNameBuf[128] = "";
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80.0f);
        ImGui::InputTextWithHint("##MatSaveName", "preset name", saveNameBuf, sizeof(saveNameBuf));
        ImGui::SameLine();
        if (ImGui::Button("Save", ImVec2(70, 0)))
        {
            std::string saveName(saveNameBuf);
            if (!saveName.empty())
            {
                MaterialLibrary::saveMaterial(saveName, material, *m_resourceManager, assetPath);
            }
        }

        // Load combo from existing presets
        auto presets = MaterialLibrary::listPresets(assetPath);
        if (!presets.empty())
        {
            static int selectedPreset = -1;
            std::vector<const char*> items;
            items.reserve(presets.size());
            for (const auto& p : presets)
            {
                items.push_back(p.c_str());
            }
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80.0f);
            ImGui::Combo("##MatLoadPreset", &selectedPreset, items.data(),
                         static_cast<int>(items.size()));
            ImGui::SameLine();
            if (ImGui::Button("Load", ImVec2(70, 0)) && selectedPreset >= 0
                && selectedPreset < static_cast<int>(presets.size()))
            {
                MaterialLibrary::loadMaterial(presets[static_cast<size_t>(selectedPreset)], material,
                                              *m_resourceManager, assetPath);
            }
        }
        ImGui::Spacing();
        ImGui::Separator();
    }

    // Material type selector
    const char* typeLabels[] = { "Blinn-Phong", "PBR" };
    int typeIndex = static_cast<int>(material.getType());
    if (ImGui::Combo("Type", &typeIndex, typeLabels, 2))
    {
        material.setType(static_cast<MaterialType>(typeIndex));
    }

    ImGui::Spacing();

    if (material.getType() == MaterialType::BLINN_PHONG)
    {
        drawMaterialBlinnPhong(material);
    }
    else
    {
        drawMaterialPbr(material);
    }

    drawMaterialTextures(material);
    drawMaterialTransparency(material);

    // --- Material Preview ---
    // Mark preview dirty if any inspector widget was modified this frame
    if (ImGui::IsAnyItemActive() || ImGui::IsItemDeactivated())
    {
        m_materialPreview.markDirty();
    }

    if (ImGui::CollapsingHeader("Preview", ImGuiTreeNodeFlags_DefaultOpen))
    {
        m_materialPreview.render(material);

        GLuint previewTex = m_materialPreview.getTextureId();
        if (previewTex != 0)
        {
            float previewSize = std::min(128.0f, ImGui::GetContentRegionAvail().x);
            // Center the preview
            float avail = ImGui::GetContentRegionAvail().x;
            if (avail > previewSize)
            {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX()
                    + (avail - previewSize) * 0.5f);
            }
            ImGui::Image(
                static_cast<ImTextureID>(static_cast<uintptr_t>(previewTex)),
                ImVec2(previewSize, previewSize),
                ImVec2(0, 1), ImVec2(1, 0));
        }
    }

    ImGui::PopID();
}

void InspectorPanel::drawMaterialBlinnPhong(Material& material)
{
    // --- Base Color section ---
    if (ImGui::CollapsingHeader("Base Color", ImGuiTreeNodeFlags_DefaultOpen))
    {
        drawTextureSlot("Diffuse", material.getDiffuseTexture());

        glm::vec3 diffuse = material.getDiffuseColor();
        if (ImGui::ColorEdit3("Diffuse Color", &diffuse.x))
        {
            material.setDiffuseColor(diffuse);
        }

        glm::vec3 specular = material.getSpecularColor();
        if (ImGui::ColorEdit3("Specular Color", &specular.x))
        {
            material.setSpecularColor(specular);
        }

        float shininess = material.getShininess();
        if (ImGui::DragFloat("Shininess", &shininess, 1.0f, 1.0f, 512.0f))
        {
            material.setShininess(shininess);
        }
    }

    ImGui::Spacing();
}

void InspectorPanel::drawMaterialPbr(Material& material)
{
    // --- Base Color section ---
    if (ImGui::CollapsingHeader("Base Color", ImGuiTreeNodeFlags_DefaultOpen))
    {
        drawTextureSlot("Albedo", material.getDiffuseTexture());

        glm::vec3 albedo = material.getAlbedo();
        if (ImGui::ColorEdit3("Albedo Color", &albedo.x))
        {
            material.setAlbedo(albedo);
        }
    }

    // --- Surface Properties section ---
    if (ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen))
    {
        float metallic = material.getMetallic();
        if (ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f))
        {
            material.setMetallic(metallic);
        }

        float roughness = material.getRoughness();
        if (ImGui::SliderFloat("Roughness", &roughness, 0.04f, 1.0f))
        {
            material.setRoughness(roughness);
        }

        drawTextureSlot("Metallic-Roughness", material.getMetallicRoughnessTexture());

        float ao = material.getAo();
        if (ImGui::SliderFloat("AO", &ao, 0.0f, 1.0f))
        {
            material.setAo(ao);
        }

        drawTextureSlot("AO Map", material.getAoTexture());
    }

    // --- Clearcoat section ---
    float clearcoat = material.getClearcoat();
    if (clearcoat > 0.0f || ImGui::CollapsingHeader("Clearcoat"))
    {
        if (clearcoat > 0.0f)
        {
            // Auto-open if clearcoat is active — section drawn inline
        }
        if (ImGui::SliderFloat("Clearcoat", &clearcoat, 0.0f, 1.0f))
        {
            material.setClearcoat(clearcoat);
        }

        if (clearcoat > 0.0f)
        {
            float ccRoughness = material.getClearcoatRoughness();
            if (ImGui::SliderFloat("CC Roughness", &ccRoughness, 0.0f, 1.0f))
            {
                material.setClearcoatRoughness(ccRoughness);
            }
        }
    }

    // --- Emission section ---
    if (ImGui::CollapsingHeader("Emission"))
    {
        glm::vec3 emissive = material.getEmissive();
        if (ImGui::ColorEdit3("Emissive Color", &emissive.x,
                              ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float))
        {
            material.setEmissive(emissive);
        }

        float emissiveStrength = material.getEmissiveStrength();
        if (ImGui::DragFloat("Strength", &emissiveStrength, 0.1f, 0.0f, 100.0f))
        {
            material.setEmissiveStrength(emissiveStrength);
        }

        drawTextureSlot("Emissive Map", material.getEmissiveTexture());
    }

    // --- UV & Tiling ---
    if (ImGui::CollapsingHeader("UV & Tiling"))
    {
        float uvScale = material.getUvScale();
        if (ImGui::DragFloat("UV Scale", &uvScale, 0.05f, 0.01f, 50.0f))
        {
            material.setUvScale(uvScale);
        }

        bool stochastic = material.isStochasticTiling();
        if (ImGui::Checkbox("Stochastic Tiling", &stochastic))
        {
            material.setStochasticTiling(stochastic);
        }
    }

    ImGui::Spacing();
}

void InspectorPanel::drawMaterialTextures(Material& material)
{
    if (ImGui::CollapsingHeader("Normal & Height", ImGuiTreeNodeFlags_DefaultOpen))
    {
        drawTextureSlot("Normal Map", material.getNormalMap());
        drawTextureSlot("Height Map", material.getHeightMap());

        // POM settings (only relevant if height map present)
        if (material.hasHeightMap())
        {
            ImGui::Spacing();
            bool pomEnabled = material.isPomEnabled();
            if (ImGui::Checkbox("Parallax Occlusion", &pomEnabled))
            {
                material.setPomEnabled(pomEnabled);
            }

            if (pomEnabled)
            {
                float heightScale = material.getHeightScale();
                if (ImGui::SliderFloat("Height Scale", &heightScale, 0.0f, 0.2f))
                {
                    material.setHeightScale(heightScale);
                }
            }
        }

        // Stochastic tiling (for Blinn-Phong — PBR has it in UV section)
        if (material.getType() == MaterialType::BLINN_PHONG)
        {
            bool stochastic = material.isStochasticTiling();
            if (ImGui::Checkbox("Stochastic Tiling", &stochastic))
            {
                material.setStochasticTiling(stochastic);
            }
        }
    }
}

void InspectorPanel::drawMaterialTransparency(Material& material)
{
    if (ImGui::CollapsingHeader("Transparency"))
    {
        const char* modeLabels[] = { "Opaque", "Mask", "Blend" };
        int modeIndex = static_cast<int>(material.getAlphaMode());
        if (ImGui::Combo("Alpha Mode", &modeIndex, modeLabels, 3))
        {
            material.setAlphaMode(static_cast<AlphaMode>(modeIndex));
        }

        if (material.getAlphaMode() == AlphaMode::MASK)
        {
            float cutoff = material.getAlphaCutoff();
            if (ImGui::SliderFloat("Alpha Cutoff", &cutoff, 0.0f, 1.0f))
            {
                material.setAlphaCutoff(cutoff);
            }
        }

        if (material.getAlphaMode() == AlphaMode::BLEND)
        {
            float alpha = material.getBaseColorAlpha();
            if (ImGui::SliderFloat("Opacity", &alpha, 0.0f, 1.0f))
            {
                material.setBaseColorAlpha(alpha);
            }
        }

        bool doubleSided = material.isDoubleSided();
        if (ImGui::Checkbox("Double Sided", &doubleSided))
        {
            material.setDoubleSided(doubleSided);
        }
    }
}

// ---------------------------------------------------------------------------
// Light components
// ---------------------------------------------------------------------------

/// @brief Draws a small attenuation curve plot.
static void drawAttenuationCurve(float constant, float linear, float quadratic,
                                 float range)
{
    constexpr int SAMPLES = 64;
    float values[SAMPLES];
    for (int i = 0; i < SAMPLES; ++i)
    {
        float d = (static_cast<float>(i) / (SAMPLES - 1)) * range;
        float atten = 1.0f / (constant + linear * d + quadratic * d * d);
        values[i] = std::clamp(atten, 0.0f, 1.0f);
    }

    char overlay[64];
    std::snprintf(overlay, sizeof(overlay), "Range: %.1f m", static_cast<double>(range));
    ImGui::PlotLines("##Falloff", values, SAMPLES, 0, overlay,
                     0.0f, 1.0f, ImVec2(0, 50));
}

/// @brief Draws the shared attenuation/range controls for point and spot lights.
/// Returns true if anything changed.
static bool drawAttenuationControls(float& constant, float& linear, float& quadratic,
                                    float& range)
{
    bool changed = false;

    // Compute range from current attenuation if it hasn't been set yet
    if (range <= 0.0f)
    {
        range = calculateLightRange(constant, linear, quadratic);
    }

    // Range slider (auto-calculates attenuation)
    if (ImGui::DragFloat("Range", &range, 0.1f, 0.1f, 200.0f, "%.1f m"))
    {
        setAttenuationFromRange(range, constant, linear, quadratic);
        changed = true;
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Distance where light intensity reaches ~0%%.\n"
                          "Auto-calculates attenuation coefficients.");
    }

    // Attenuation curve visualization
    drawAttenuationCurve(constant, linear, quadratic, range);

    // Advanced section: raw attenuation coefficients
    if (ImGui::TreeNode("Advanced Attenuation"))
    {
        if (ImGui::DragFloat("Constant",  &constant,  0.01f, 0.0f, 10.0f))
        {
            range = calculateLightRange(constant, linear, quadratic);
            changed = true;
        }
        if (ImGui::DragFloat("Linear",    &linear,    0.001f, 0.0f, 2.0f))
        {
            range = calculateLightRange(constant, linear, quadratic);
            changed = true;
        }
        if (ImGui::DragFloat("Quadratic", &quadratic, 0.001f, 0.0f, 2.0f))
        {
            range = calculateLightRange(constant, linear, quadratic);
            changed = true;
        }
        ImGui::TreePop();
    }

    return changed;
}

void InspectorPanel::drawCameraComponent(Entity& entity)
{
    auto* comp = entity.getComponent<CameraComponent>();
    if (!comp)
    {
        return;
    }

    if (!drawComponentHeader("Camera"))
    {
        return;
    }

    // Projection type
    int projType = static_cast<int>(comp->projectionType);
    const char* projNames[] = {"Perspective", "Orthographic"};
    if (ImGui::Combo("Projection", &projType, projNames, 2))
    {
        comp->projectionType = static_cast<ProjectionType>(projType);
    }

    if (comp->projectionType == ProjectionType::PERSPECTIVE)
    {
        ImGui::SliderFloat("FOV", &comp->fov, 1.0f, 120.0f, "%.1f deg");
    }
    else
    {
        ImGui::DragFloat("Ortho Size", &comp->orthoSize, 0.1f, 0.1f, 100.0f, "%.1f m");
    }

    ImGui::DragFloat("Near Plane", &comp->nearPlane, 0.01f, 0.001f, 10.0f, "%.3f m");
    ImGui::DragFloat("Far Plane (Cull)", &comp->farPlane, 10.0f, 10.0f, 10000.0f, "%.0f m");

    // Active camera toggle
    auto* scene = m_currentScene;
    if (scene)
    {
        bool isActive = (scene->getActiveCamera() == comp);
        if (ImGui::Checkbox("Active Camera", &isActive))
        {
            scene->setActiveCamera(isActive ? comp : nullptr);
        }
    }

    ImGui::Spacing();
}

void InspectorPanel::drawDirectionalLight(Entity& entity)
{
    auto* comp = entity.getComponent<DirectionalLightComponent>();
    if (!comp)
    {
        return;
    }

    if (!drawComponentHeader("Directional Light"))
    {
        return;
    }

    auto& light = comp->light;

    drawVec3Control("Direction", light.direction, 0.0f, 0.01f);

    // Simplified color model: single color, ambient/specular derived
    ImGui::ColorEdit3("Color", &light.diffuse.x);
    light.specular = light.diffuse;
    light.ambient = light.diffuse * 0.1f;

    if (ImGui::TreeNode("Advanced Colors"))
    {
        ImGui::ColorEdit3("Ambient",  &light.ambient.x);
        ImGui::ColorEdit3("Specular", &light.specular.x);
        ImGui::TreePop();
    }

    ImGui::Spacing();
}

void InspectorPanel::drawPointLight(Entity& entity)
{
    auto* comp = entity.getComponent<PointLightComponent>();
    if (!comp)
    {
        return;
    }

    if (!drawComponentHeader("Point Light"))
    {
        return;
    }

    auto& light = comp->light;

    // Simplified color model: single color, ambient/specular derived
    ImGui::ColorEdit3("Color", &light.diffuse.x);
    light.specular = light.diffuse;
    light.ambient = light.diffuse * 0.05f;

    if (ImGui::TreeNode("Advanced Colors"))
    {
        ImGui::ColorEdit3("Ambient",  &light.ambient.x);
        ImGui::ColorEdit3("Specular", &light.specular.x);
        ImGui::TreePop();
    }

    ImGui::Spacing();

    // Range and attenuation controls
    drawAttenuationControls(light.constant, light.linear, light.quadratic, light.range);

    ImGui::Spacing();
    ImGui::Checkbox("Casts Shadow", &light.castsShadow);

    ImGui::Spacing();
}

void InspectorPanel::drawSpotLight(Entity& entity)
{
    auto* comp = entity.getComponent<SpotLightComponent>();
    if (!comp)
    {
        return;
    }

    if (!drawComponentHeader("Spot Light"))
    {
        return;
    }

    auto& light = comp->light;

    drawVec3Control("Direction", light.direction, 0.0f, 0.01f);

    // Simplified color model
    ImGui::ColorEdit3("Color", &light.diffuse.x);
    light.specular = light.diffuse;
    light.ambient = light.diffuse * 0.0f; // Spot lights typically have no ambient

    if (ImGui::TreeNode("Advanced Colors"))
    {
        ImGui::ColorEdit3("Ambient",  &light.ambient.x);
        ImGui::ColorEdit3("Specular", &light.specular.x);
        ImGui::TreePop();
    }

    ImGui::Spacing();

    // Cone angles
    float innerDeg = glm::degrees(std::acos(std::clamp(light.innerCutoff, -1.0f, 1.0f)));
    float outerDeg = glm::degrees(std::acos(std::clamp(light.outerCutoff, -1.0f, 1.0f)));
    if (ImGui::DragFloat("Inner Angle", &innerDeg, 0.5f, 0.0f, 89.0f, "%.1f deg"))
    {
        light.innerCutoff = std::cos(glm::radians(innerDeg));
    }
    if (ImGui::DragFloat("Outer Angle", &outerDeg, 0.5f, 0.0f, 89.0f, "%.1f deg"))
    {
        light.outerCutoff = std::cos(glm::radians(outerDeg));
    }

    ImGui::Spacing();

    // Range and attenuation controls
    drawAttenuationControls(light.constant, light.linear, light.quadratic, light.range);

    ImGui::Spacing();
}

// ---------------------------------------------------------------------------
// Ed2 — Emissive Light: snapshot + push helper
// ---------------------------------------------------------------------------

/// @brief Snapshot of the three EmissiveLightComponent fields the inspector edits.
struct EmissiveLightSnapshot
{
    float lightRadius    = 0.0f;
    float lightIntensity = 0.0f;
    glm::vec3 overrideColor{0.0f};

    static EmissiveLightSnapshot capture(const EmissiveLightComponent& c)
    {
        return {c.lightRadius, c.lightIntensity, c.overrideColor};
    }

    static void apply(EmissiveLightComponent& c, const EmissiveLightSnapshot& s)
    {
        c.lightRadius    = s.lightRadius;
        c.lightIntensity = s.lightIntensity;
        c.overrideColor  = s.overrideColor;
    }

    bool operator==(const EmissiveLightSnapshot& other) const
    {
        return lightRadius == other.lightRadius
            && lightIntensity == other.lightIntensity
            && overrideColor == other.overrideColor;
    }
};

static void pushEmissiveLightUndo(CommandHistory* history, Scene* scene,
                                  uint32_t entityId,
                                  const EmissiveLightSnapshot& before,
                                  const EmissiveLightSnapshot& after)
{
    if (!history || !scene || before == after)
    {
        return;
    }
    history->execute(std::make_unique<ComponentPropertyCommand<EmissiveLightSnapshot>>(
        before, after, "Change Emissive Light property",
        [scene, entityId](const EmissiveLightSnapshot& s)
        {
            if (auto* entity = scene->findEntityById(entityId))
            {
                if (auto* comp = entity->getComponent<EmissiveLightComponent>())
                {
                    EmissiveLightSnapshot::apply(*comp, s);
                }
            }
        }));
}

void InspectorPanel::drawEmissiveLight(Entity& entity)
{
    auto* comp = entity.getComponent<EmissiveLightComponent>();
    if (!comp)
    {
        return;
    }

    if (!drawComponentHeader("Emissive Light"))
    {
        return;
    }

    EmissiveLightSnapshot before = EmissiveLightSnapshot::capture(*comp);
    EditTracker tr;

    tr.track(ImGui::DragFloat("Radius", &comp->lightRadius, 0.1f, 0.1f, 100.0f));
    tr.track(ImGui::DragFloat("Intensity", &comp->lightIntensity, 0.05f, 0.0f, 50.0f,
                              "%.3f", ImGuiSliderFlags_Logarithmic));

    tr.track(ImGui::ColorEdit3("Override Color", &comp->overrideColor.x));
    ImGui::SameLine();
    ImGui::TextDisabled("(0,0,0 = derive from material)");

    if (tr.shouldCommit())
    {
        pushEmissiveLightUndo(m_commandHistory, m_currentScene, entity.getId(),
                              before, EmissiveLightSnapshot::capture(*comp));
    }

    ImGui::Spacing();
}

// ---------------------------------------------------------------------------
// Particle Emitter
// ---------------------------------------------------------------------------

/// @brief Helper that captures config before edit, then pushes an undo command if changed.
/// Usage: call before ImGui widget, then after, check if configBefore != configAfter.
static void pushParticleUndo(CommandHistory* history, Scene* scene, uint32_t entityId,
                             const ParticleEmitterConfig& oldConfig,
                             const ParticleEmitterConfig& newConfig,
                             const char* propertyName)
{
    if (!history || !scene)
    {
        return;
    }
    // Only push if something actually changed (compare key fields for the given property)
    history->execute(
        std::make_unique<ParticlePropertyCommand>(
            *scene, entityId, oldConfig, newConfig, propertyName));
}

void InspectorPanel::drawParticleEmitter(Entity& entity)
{
    auto* comp = entity.getComponent<ParticleEmitterComponent>();
    if (!comp)
    {
        return;
    }

    if (!drawComponentHeader("Particle Emitter"))
    {
        return;
    }

    auto& cfg = comp->getConfig();
    ParticleEmitterConfig before = cfg;  // Snapshot for undo

    // --- Playback controls ---
    {
        bool paused = comp->isPaused();
        if (paused)
        {
            if (ImGui::Button("Play"))
            {
                comp->setPaused(false);
            }
        }
        else
        {
            if (ImGui::Button("Pause"))
            {
                comp->setPaused(true);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Restart"))
        {
            comp->restart();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(%d alive)", comp->getData().count);
    }

    ImGui::Spacing();

    // --- Emission ---
    if (ImGui::TreeNodeEx("Emission", ImGuiTreeNodeFlags_DefaultOpen))
    {
        EditTracker tr;
        tr.track(ImGui::DragFloat("Rate", &cfg.emissionRate, 1.0f, 0.0f, 10000.0f, "%.0f/s"));
        tr.track(ImGui::DragInt("Max Particles", &cfg.maxParticles, 10.0f, 1, 100000));
        tr.track(ImGui::Checkbox("Looping", &cfg.looping));
        if (!cfg.looping)
        {
            tr.track(ImGui::DragFloat("Duration", &cfg.duration, 0.1f, 0.1f, 60.0f, "%.1f s"));
        }

        if (tr.shouldCommit())
        {
            pushParticleUndo(m_commandHistory, m_currentScene, entity.getId(),
                             before, cfg, "emission");
            before = cfg;
        }
        ImGui::TreePop();
    }

    // --- Start Properties ---
    if (ImGui::TreeNodeEx("Start Properties", ImGuiTreeNodeFlags_DefaultOpen))
    {
        EditTracker tr;
        tr.track(ImGui::DragFloatRange2("Lifetime", &cfg.startLifetimeMin, &cfg.startLifetimeMax,
                                         0.05f, 0.01f, 30.0f, "%.2f s", "%.2f s"));
        tr.track(ImGui::DragFloatRange2("Speed", &cfg.startSpeedMin, &cfg.startSpeedMax,
                                         0.1f, 0.0f, 100.0f, "%.1f", "%.1f"));
        tr.track(ImGui::DragFloatRange2("Size", &cfg.startSizeMin, &cfg.startSizeMax,
                                         0.01f, 0.001f, 10.0f, "%.3f", "%.3f"));
        tr.track(ImGui::ColorEdit4("Start Color", &cfg.startColor.x));

        if (tr.shouldCommit())
        {
            pushParticleUndo(m_commandHistory, m_currentScene, entity.getId(),
                             before, cfg, "start properties");
            before = cfg;
        }
        ImGui::TreePop();
    }

    // --- Shape ---
    if (ImGui::TreeNodeEx("Shape", ImGuiTreeNodeFlags_DefaultOpen))
    {
        EditTracker tr;
        const char* shapeNames[] = {"Point", "Sphere", "Cone", "Box"};
        int shapeIdx = static_cast<int>(cfg.shape);
        bool comboChanged = ImGui::Combo("Type", &shapeIdx, shapeNames, 4);
        if (comboChanged)
        {
            cfg.shape = static_cast<ParticleEmitterConfig::Shape>(shapeIdx);
        }
        tr.track(comboChanged);

        switch (cfg.shape)
        {
            case ParticleEmitterConfig::Shape::SPHERE:
                tr.track(ImGui::DragFloat("Radius", &cfg.shapeRadius, 0.1f, 0.0f, 50.0f));
                break;
            case ParticleEmitterConfig::Shape::CONE:
                tr.track(ImGui::DragFloat("Radius", &cfg.shapeRadius, 0.1f, 0.0f, 50.0f));
                tr.track(ImGui::DragFloat("Cone Angle", &cfg.shapeConeAngle, 1.0f, 1.0f, 89.0f, "%.0f deg"));
                break;
            case ParticleEmitterConfig::Shape::BOX:
                tr.track(ImGui::DragFloat3("Box Size", &cfg.shapeBoxSize.x, 0.1f, 0.0f, 50.0f));
                break;
            case ParticleEmitterConfig::Shape::POINT:
            default:
                break;
        }

        if (tr.shouldCommit())
        {
            pushParticleUndo(m_commandHistory, m_currentScene, entity.getId(),
                             before, cfg, "shape");
            before = cfg;
        }
        ImGui::TreePop();
    }

    // --- Over Lifetime ---
    if (ImGui::TreeNodeEx("Over Lifetime", ImGuiTreeNodeFlags_DefaultOpen))
    {
        EditTracker tr;

        // Color over lifetime — checkbox + custom gradient widget. The
        // gradient editor's internal drag-state isn't observable through
        // IsItemActivated, so we treat its `changed` return as
        // instant-only (best we can do without rewriting the widget).
        tr.track(ImGui::Checkbox("Color Over Lifetime", &cfg.useColorOverLifetime));
        if (cfg.useColorOverLifetime)
        {
            ImGui::Indent();
            tr.changed |= drawGradientEditor("Color Gradient", cfg.colorOverLifetime);
            ImGui::Unindent();
        }

        ImGui::Spacing();

        // Size over lifetime
        tr.track(ImGui::Checkbox("Size Over Lifetime", &cfg.useSizeOverLifetime));
        if (cfg.useSizeOverLifetime)
        {
            ImGui::Indent();
            tr.changed |= drawCurveEditor("Size Curve", cfg.sizeOverLifetime);
            ImGui::Unindent();
        }

        ImGui::Spacing();

        // Speed over lifetime
        tr.track(ImGui::Checkbox("Speed Over Lifetime", &cfg.useSpeedOverLifetime));
        if (cfg.useSpeedOverLifetime)
        {
            ImGui::Indent();
            tr.changed |= drawCurveEditor("Speed Curve", cfg.speedOverLifetime);
            ImGui::Unindent();
        }

        if (tr.shouldCommit())
        {
            pushParticleUndo(m_commandHistory, m_currentScene, entity.getId(),
                             before, cfg, "over lifetime");
            before = cfg;
        }
        ImGui::TreePop();
    }

    // --- Forces ---
    if (ImGui::TreeNodeEx("Forces"))
    {
        EditTracker tr;
        tr.track(ImGui::DragFloat3("Gravity", &cfg.gravity.x, 0.1f, -50.0f, 50.0f));

        if (tr.shouldCommit())
        {
            pushParticleUndo(m_commandHistory, m_currentScene, entity.getId(),
                             before, cfg, "gravity");
            before = cfg;
        }
        ImGui::TreePop();
    }

    // --- Renderer ---
    if (ImGui::TreeNodeEx("Renderer"))
    {
        EditTracker tr;
        const char* blendNames[] = {"Additive", "Alpha Blend"};
        int blendIdx = static_cast<int>(cfg.blendMode);
        bool comboChanged = ImGui::Combo("Blend Mode", &blendIdx, blendNames, 2);
        if (comboChanged)
        {
            cfg.blendMode = static_cast<ParticleEmitterConfig::BlendMode>(blendIdx);
        }
        tr.track(comboChanged);

        // Texture path (read-only display for now, browse in 5E-3)
        if (!cfg.texturePath.empty())
        {
            ImGui::Text("Texture: %s", cfg.texturePath.c_str());
        }
        else
        {
            ImGui::TextDisabled("No texture (using default circle)");
        }

        if (tr.shouldCommit())
        {
            pushParticleUndo(m_commandHistory, m_currentScene, entity.getId(),
                             before, cfg, "renderer");
            before = cfg;
        }
        ImGui::TreePop();
    }

    // --- Light Coupling ---
    if (ImGui::TreeNodeEx("Light Coupling", ImGuiTreeNodeFlags_DefaultOpen))
    {
        EditTracker tr;
        tr.track(ImGui::Checkbox("Emits Light", &cfg.emitsLight));

        if (cfg.emitsLight)
        {
            tr.track(ImGui::ColorEdit3("Light Color", &cfg.lightColor.x));
            tr.track(ImGui::DragFloat("Light Range", &cfg.lightRange, 0.1f, 0.5f, 50.0f, "%.1f m"));
            tr.track(ImGui::DragFloat("Light Intensity", &cfg.lightIntensity, 0.05f, 0.0f, 10.0f, "%.2f"));
            tr.track(ImGui::DragFloat("Flicker Speed", &cfg.flickerSpeed, 0.5f, 1.0f, 30.0f, "%.1f"));
        }

        if (tr.shouldCommit())
        {
            pushParticleUndo(m_commandHistory, m_currentScene, entity.getId(),
                             before, cfg, "lightCoupling");
            before = cfg;
        }
        ImGui::TreePop();
    }

    ImGui::Spacing();
}

// ---------------------------------------------------------------------------
// Audio Source (Phase 10.9 W15 — closes F3's round-trip gap visibly)
// ---------------------------------------------------------------------------

void InspectorPanel::drawAudioSource(Entity& entity)
{
    auto* comp = entity.getComponent<AudioSourceComponent>();
    if (!comp)
    {
        return;
    }

    if (!drawComponentHeader("Audio Source"))
    {
        return;
    }

    // --- Clip + playback ---
    {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "%s", comp->clipPath.c_str());
        if (ImGui::InputText("Clip Path", buf, sizeof(buf)))
        {
            comp->clipPath = buf;
        }
        ImGui::Checkbox("Auto Play", &comp->autoPlay);
        ImGui::SameLine();
        ImGui::Checkbox("Loop", &comp->loop);
        ImGui::Checkbox("Spatial", &comp->spatial);
    }

    ImGui::Spacing();

    // --- Bus / volume / pitch ---
    {
        static const char* busLabels[] = {
            "Master", "Music", "Voice", "Sfx", "Ambient", "Ui"
        };
        int busIdx = static_cast<int>(comp->bus);
        if (busIdx < 0 || busIdx >= IM_ARRAYSIZE(busLabels)) busIdx = 3; // Sfx default
        if (ImGui::Combo("Bus", &busIdx, busLabels, IM_ARRAYSIZE(busLabels)))
        {
            comp->bus = static_cast<AudioBus>(busIdx);
        }
        ImGui::SliderFloat("Volume", &comp->volume, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Pitch", &comp->pitch, 0.5f, 2.0f, "%.2f");
    }

    ImGui::Spacing();

    // --- Spatialisation (only meaningful when `spatial` is on) ---
    if (comp->spatial && ImGui::TreeNodeEx("Spatial",
                                            ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::DragFloat("Min Distance", &comp->minDistance, 0.1f, 0.0f, 1000.0f, "%.2f m");
        ImGui::DragFloat("Max Distance", &comp->maxDistance, 0.1f, 0.0f, 5000.0f, "%.2f m");
        ImGui::DragFloat("Rolloff Factor", &comp->rolloffFactor, 0.05f, 0.0f, 10.0f, "%.2f");

        static const char* attenuationLabels[] = {
            "None", "Linear", "InverseDistance", "Exponential"
        };
        int attIdx = static_cast<int>(comp->attenuationModel);
        if (attIdx < 0 || attIdx >= IM_ARRAYSIZE(attenuationLabels)) attIdx = 2;
        if (ImGui::Combo("Attenuation", &attIdx, attenuationLabels,
                         IM_ARRAYSIZE(attenuationLabels)))
        {
            comp->attenuationModel = static_cast<AttenuationModel>(attIdx);
        }

        ImGui::DragFloat3("Velocity (Doppler)", &comp->velocity.x, 0.05f, -100.0f, 100.0f);
        ImGui::TreePop();
    }

    // --- Occlusion ---
    if (ImGui::TreeNodeEx("Occlusion"))
    {
        static const char* occLabels[] = {
            "Air", "Cloth", "Wood", "Glass", "Stone", "Concrete", "Metal", "Water"
        };
        int occIdx = static_cast<int>(comp->occlusionMaterial);
        if (occIdx < 0 || occIdx >= IM_ARRAYSIZE(occLabels)) occIdx = 0;
        if (ImGui::Combo("Material", &occIdx, occLabels, IM_ARRAYSIZE(occLabels)))
        {
            comp->occlusionMaterial = static_cast<AudioOcclusionMaterialPreset>(occIdx);
        }
        ImGui::SliderFloat("Fraction", &comp->occlusionFraction, 0.0f, 1.0f, "%.2f");
        ImGui::TreePop();
    }

    // --- Priority ---
    {
        static const char* priorityLabels[] = { "Low", "Normal", "High", "Critical" };
        int prIdx = static_cast<int>(comp->priority);
        if (prIdx < 0 || prIdx >= IM_ARRAYSIZE(priorityLabels)) prIdx = 1;
        if (ImGui::Combo("Priority", &prIdx, priorityLabels,
                         IM_ARRAYSIZE(priorityLabels)))
        {
            comp->priority = static_cast<SoundPriority>(prIdx);
        }
    }

    // Note: this panel does not currently push undo entries — the
    // Phase 10.5 editor-undo polish (Slice 12 Ed1 / Ed2) covers the
    // shared undo retrofit across multiple inspector surfaces; the
    // AudioSource path joins that batch when it lands.
}

// ---------------------------------------------------------------------------
// Water Surface
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Ed2 — Water Surface: push helper
// ---------------------------------------------------------------------------

static void pushWaterSurfaceUndo(CommandHistory* history, Scene* scene,
                                 uint32_t entityId,
                                 const WaterSurfaceConfig& before,
                                 const WaterSurfaceConfig& after)
{
    if (!history || !scene)
    {
        return;
    }
    history->execute(std::make_unique<ComponentPropertyCommand<WaterSurfaceConfig>>(
        before, after, "Change Water Surface property",
        [scene, entityId](const WaterSurfaceConfig& s)
        {
            if (auto* entity = scene->findEntityById(entityId))
            {
                if (auto* water = entity->getComponent<WaterSurfaceComponent>())
                {
                    water->getConfig() = s;
                }
            }
        }));
}

void InspectorPanel::drawWaterSurface(Entity& entity)
{
    auto* water = entity.getComponent<WaterSurfaceComponent>();
    if (!water) return;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
    if (ImGui::CollapsingHeader("Water Surface", ImGuiTreeNodeFlags_DefaultOpen))
    {
        auto& config = water->getConfig();
        WaterSurfaceConfig before = config;
        EditTracker tr;

        // --- Presets ---
        const char* presets[] = {"Custom", "Still Bath", "Gentle Pool", "Flowing Stream", "Ocean Swell"};
        static int currentPreset = 0;
        bool presetChanged = ImGui::Combo("Preset", &currentPreset, presets, 5);
        tr.track(presetChanged);
        if (presetChanged)
        {
            if (currentPreset == 1) // Still Bath
            {
                config.numWaves = 1;
                config.waves[0] = {0.001f, 4.0f, 0.1f, 0.0f};
                config.flowSpeed = 0.05f;
                config.normalStrength = 0.3f;
                config.dudvStrength = 0.005f;
                config.specularPower = 256.0f;
                config.shallowColor = {0.15f, 0.35f, 0.45f, 0.7f};
                config.deepColor = {0.02f, 0.08f, 0.15f, 1.0f};
            }
            else if (currentPreset == 2) // Gentle Pool
            {
                config.numWaves = 2;
                config.waves[0] = {0.005f, 3.0f, 0.2f, 10.0f};
                config.waves[1] = {0.003f, 2.0f, 0.15f, 75.0f};
                config.flowSpeed = 0.15f;
                config.normalStrength = 0.8f;
                config.dudvStrength = 0.015f;
                config.specularPower = 256.0f;
                config.shallowColor = {0.1f, 0.4f, 0.5f, 0.8f};
                config.deepColor = {0.02f, 0.1f, 0.25f, 1.0f};
            }
            else if (currentPreset == 3) // Flowing Stream
            {
                config.numWaves = 3;
                config.waves[0] = {0.015f, 1.5f, 0.8f, 0.0f};
                config.waves[1] = {0.008f, 1.0f, 0.6f, 15.0f};
                config.waves[2] = {0.004f, 0.8f, 1.0f, -10.0f};
                config.flowSpeed = 0.6f;
                config.normalStrength = 1.5f;
                config.dudvStrength = 0.03f;
                config.specularPower = 128.0f;
                config.shallowColor = {0.12f, 0.38f, 0.42f, 0.75f};
                config.deepColor = {0.03f, 0.12f, 0.2f, 1.0f};
            }
            else if (currentPreset == 4) // Ocean Swell
            {
                config.numWaves = 4;
                config.waves[0] = {0.04f, 8.0f, 0.3f, 0.0f};
                config.waves[1] = {0.025f, 5.0f, 0.25f, 30.0f};
                config.waves[2] = {0.015f, 3.0f, 0.4f, -20.0f};
                config.waves[3] = {0.008f, 1.5f, 0.6f, 60.0f};
                config.flowSpeed = 0.3f;
                config.normalStrength = 2.0f;
                config.dudvStrength = 0.04f;
                config.specularPower = 64.0f;
                config.shallowColor = {0.05f, 0.3f, 0.4f, 0.85f};
                config.deepColor = {0.01f, 0.05f, 0.15f, 1.0f};
            }
        }
        ImGui::Spacing();
        ImGui::Separator();

        // --- Reflection mode ---
        const char* reflModes[] = {"None", "Planar", "Cubemap"};
        int reflMode = static_cast<int>(config.reflectionMode);
        bool reflChanged = ImGui::Combo("Reflection Mode", &reflMode, reflModes, 3);
        tr.track(reflChanged);
        if (reflChanged)
        {
            config.reflectionMode = static_cast<WaterReflectionMode>(reflMode);
        }
        ImGui::SetItemTooltip("None: cheapest (no reflection)\n"
                              "Planar: accurate (re-renders scene)\n"
                              "Cubemap: cheaper (1 face/frame, lower quality)");

        if (config.reflectionMode != WaterReflectionMode::NONE)
        {
            float resScale = config.reflectionResolutionScale;
            bool resChanged = ImGui::SliderFloat("Reflection Resolution", &resScale, 0.1f, 1.0f, "%.0f%%");
            tr.track(resChanged);
            if (resChanged)
            {
                config.reflectionResolutionScale = resScale;
            }
        }

        // --- Refraction ---
        tr.track(ImGui::Checkbox("Refraction (Beer's Law)", &config.refractionEnabled));
        ImGui::SetItemTooltip("Depth-based underwater coloring.\n"
                              "Disabling saves a full scene render pass.");

        ImGui::Spacing();
        ImGui::Separator();

        // --- Colors ---
        tr.track(ImGui::ColorEdit4("Shallow Color", &config.shallowColor.x));
        tr.track(ImGui::ColorEdit4("Deep Color", &config.deepColor.x));

        ImGui::Spacing();
        ImGui::Separator();

        // --- Surface detail ---
        tr.track(ImGui::SliderFloat("Normal Strength", &config.normalStrength, 0.0f, 3.0f));
        tr.track(ImGui::SliderFloat("DuDv Strength", &config.dudvStrength, 0.0f, 0.1f, "%.4f"));
        tr.track(ImGui::SliderFloat("Flow Speed", &config.flowSpeed, 0.0f, 2.0f));
        tr.track(ImGui::SliderFloat("Specular Power", &config.specularPower, 1.0f, 512.0f));

        ImGui::Spacing();
        ImGui::Separator();

        // --- Waves ---
        tr.track(ImGui::SliderInt("Wave Count", &config.numWaves, 0, WaterSurfaceConfig::MAX_WAVES));
        for (int i = 0; i < config.numWaves; ++i)
        {
            ImGui::PushID(i);
            if (ImGui::TreeNode("Wave", "Wave %d", i + 1))
            {
                tr.track(ImGui::SliderFloat("Amplitude", &config.waves[i].amplitude, 0.0f, 0.1f, "%.4f"));
                tr.track(ImGui::SliderFloat("Wavelength", &config.waves[i].wavelength, 0.1f, 10.0f));
                tr.track(ImGui::SliderFloat("Speed", &config.waves[i].speed, 0.0f, 2.0f));
                tr.track(ImGui::SliderFloat("Direction", &config.waves[i].direction, 0.0f, 360.0f, "%.0f deg"));
                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        // --- Caustics ---
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Caustics");
        tr.track(ImGui::Checkbox("Enable Caustics", &config.causticsEnabled));
        if (config.causticsEnabled)
        {
            tr.track(ImGui::SliderFloat("Caustics Intensity", &config.causticsIntensity, 0.0f, 1.0f));
            tr.track(ImGui::SliderFloat("Caustics Scale", &config.causticsScale, 0.01f, 0.5f));
        }

        // --- Quality ---
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Quality");
        const char* qualityNames[] = {"Full", "Approximate", "Simple"};
        tr.track(ImGui::Combo("Quality Tier", &config.qualityTier, qualityNames, 3));
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Full: best visuals (6 caustic samples, 3-octave noise)\n"
                              "Approximate: balanced (2 caustic samples, 2-octave noise)\n"
                              "Simple: fastest (1 caustic sample, texture normals)");
        }

        // --- Geometry ---
        ImGui::Spacing();
        ImGui::Separator();
        tr.track(ImGui::SliderFloat("Width", &config.width, 1.0f, 100.0f));
        tr.track(ImGui::SliderFloat("Depth", &config.depth, 1.0f, 100.0f));
        tr.track(ImGui::SliderInt("Grid Resolution", &config.gridResolution, 16, 256));

        if (tr.shouldCommit())
        {
            pushWaterSurfaceUndo(m_commandHistory, m_currentScene, entity.getId(),
                                 before, config);
        }
    }
    ImGui::PopStyleVar();
    ImGui::Spacing();
}

// ---------------------------------------------------------------------------
// Rigid Body
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Ed2 — Rigid Body: snapshot + push helper
// ---------------------------------------------------------------------------

struct RigidBodySnapshot
{
    CollisionShapeType shapeType = CollisionShapeType::BOX;
    glm::vec3 shapeSize{1.0f};
    BodyMotionType motionType = BodyMotionType::STATIC;
    float mass = 1.0f;
    float friction = 0.5f;
    float restitution = 0.0f;

    static RigidBodySnapshot capture(const RigidBody& rb)
    {
        return {rb.shapeType, rb.shapeSize, rb.motionType,
                rb.mass, rb.friction, rb.restitution};
    }

    static void apply(RigidBody& rb, const RigidBodySnapshot& s)
    {
        rb.shapeType   = s.shapeType;
        rb.shapeSize   = s.shapeSize;
        rb.motionType  = s.motionType;
        rb.mass        = s.mass;
        rb.friction    = s.friction;
        rb.restitution = s.restitution;
    }

    bool operator==(const RigidBodySnapshot& o) const
    {
        return shapeType == o.shapeType
            && shapeSize == o.shapeSize
            && motionType == o.motionType
            && mass == o.mass
            && friction == o.friction
            && restitution == o.restitution;
    }
};

static void pushRigidBodyUndo(CommandHistory* history, Scene* scene,
                              uint32_t entityId,
                              const RigidBodySnapshot& before,
                              const RigidBodySnapshot& after)
{
    if (!history || !scene || before == after)
    {
        return;
    }
    history->execute(std::make_unique<ComponentPropertyCommand<RigidBodySnapshot>>(
        before, after, "Change Rigid Body property",
        [scene, entityId](const RigidBodySnapshot& s)
        {
            if (auto* entity = scene->findEntityById(entityId))
            {
                if (auto* rb = entity->getComponent<RigidBody>())
                {
                    RigidBodySnapshot::apply(*rb, s);
                }
            }
        }));
}

void InspectorPanel::drawRigidBody(Entity& entity)
{
    auto* rb = entity.getComponent<RigidBody>();
    if (!rb) return;

    if (!drawComponentHeader("Rigid Body")) return;

    RigidBodySnapshot before = RigidBodySnapshot::capture(*rb);
    EditTracker tr;

    // --- Shape type ---
    const char* shapeNames[] = {"Box", "Sphere", "Capsule", "Convex Hull", "Mesh"};
    int shapeIdx = static_cast<int>(rb->shapeType);
    bool shapeChanged = ImGui::Combo("Shape", &shapeIdx, shapeNames, 5);
    tr.track(shapeChanged);
    if (shapeChanged)
    {
        rb->shapeType = static_cast<CollisionShapeType>(shapeIdx);
    }

    // --- Shape size (only for primitive shapes) ---
    switch (rb->shapeType)
    {
    case CollisionShapeType::BOX:
        tr.track(ImGui::DragFloat3("Half Extents", &rb->shapeSize.x, 0.01f, 0.01f, 100.0f, "%.2f"));
        break;
    case CollisionShapeType::SPHERE:
        tr.track(ImGui::DragFloat("Radius", &rb->shapeSize.x, 0.01f, 0.01f, 100.0f, "%.2f"));
        break;
    case CollisionShapeType::CAPSULE:
        tr.track(ImGui::DragFloat("Radius##cap", &rb->shapeSize.x, 0.01f, 0.01f, 50.0f, "%.2f"));
        tr.track(ImGui::DragFloat("Half Height", &rb->shapeSize.y, 0.01f, 0.01f, 50.0f, "%.2f"));
        break;
    case CollisionShapeType::CONVEX_HULL:
        ImGui::Text("Vertices: %zu", rb->collisionVertices.size());
        break;
    case CollisionShapeType::MESH:
        ImGui::Text("Vertices: %zu", rb->collisionVertices.size());
        ImGui::Text("Triangles: %zu", rb->collisionIndices.size() / 3);
        break;
    }

    // --- Motion type ---
    const char* motionNames[] = {"Static", "Dynamic", "Kinematic"};
    int motionIdx = static_cast<int>(rb->motionType);
    bool motionChanged = ImGui::Combo("Motion Type", &motionIdx, motionNames, 3);
    tr.track(motionChanged);
    if (motionChanged)
    {
        rb->motionType = static_cast<BodyMotionType>(motionIdx);
    }

    if (rb->shapeType == CollisionShapeType::MESH && rb->motionType != BodyMotionType::STATIC)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Mesh shapes must be Static");
    }

    // --- Physics properties ---
    if (rb->motionType == BodyMotionType::DYNAMIC)
    {
        tr.track(ImGui::DragFloat("Mass", &rb->mass, 0.1f, 0.01f, 10000.0f, "%.2f kg"));
    }
    tr.track(ImGui::DragFloat("Friction", &rb->friction, 0.01f, 0.0f, 2.0f, "%.2f"));
    tr.track(ImGui::DragFloat("Restitution", &rb->restitution, 0.01f, 0.0f, 1.0f, "%.2f"));

    if (tr.shouldCommit())
    {
        pushRigidBodyUndo(m_commandHistory, m_currentScene, entity.getId(),
                          before, RigidBodySnapshot::capture(*rb));
    }

    // --- Status ---
    if (rb->hasBody())
    {
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "Body active");
    }
    else
    {
        ImGui::TextDisabled("No body (call createBody)");
    }

    ImGui::Spacing();
}

// ---------------------------------------------------------------------------
// Cloth Simulation
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Ed2 — Cloth: snapshot + push helper
// ---------------------------------------------------------------------------

struct ClothInspectorSnapshot
{
    ClothPresetType presetType = ClothPresetType::CUSTOM;
    float particleMass    = 0.0f;
    int substeps          = 1;
    float damping         = 0.0f;
    float stretchCompliance = 0.0f;
    float shearCompliance   = 0.0f;
    float bendCompliance    = 0.0f;
    glm::vec3 windDir{0.0f};
    float windStrength    = 0.0f;
    float dragCoefficient = 0.0f;
    ClothWindQuality windQuality = ClothWindQuality::FULL;

    static ClothInspectorSnapshot capture(const ClothComponent& cloth)
    {
        const auto& sim = cloth.getSimulator();
        const auto& cfg = sim.getConfig();
        ClothInspectorSnapshot s;
        s.presetType        = cloth.getPresetType();
        s.particleMass      = cfg.particleMass;
        s.substeps          = cfg.substeps;
        s.damping           = cfg.damping;
        s.stretchCompliance = cfg.stretchCompliance;
        s.shearCompliance   = cfg.shearCompliance;
        s.bendCompliance    = cfg.bendCompliance;
        s.windDir           = sim.getWindDirection();
        s.windStrength      = sim.getWindStrength();
        s.dragCoefficient   = sim.getDragCoefficient();
        s.windQuality       = sim.getWindQuality();
        return s;
    }

    static void apply(ClothComponent& cloth, const ClothInspectorSnapshot& s)
    {
        auto& sim = cloth.getSimulator();
        sim.setParticleMass(s.particleMass);
        sim.setSubsteps(s.substeps);
        sim.setDamping(s.damping);
        sim.setStretchCompliance(s.stretchCompliance);
        sim.setShearCompliance(s.shearCompliance);
        sim.setBendCompliance(s.bendCompliance);
        sim.setWind(s.windDir, s.windStrength);
        sim.setDragCoefficient(s.dragCoefficient);
        sim.setWindQuality(s.windQuality);
        cloth.setPresetType(s.presetType);
    }

    bool operator==(const ClothInspectorSnapshot& o) const
    {
        return presetType == o.presetType
            && particleMass == o.particleMass
            && substeps == o.substeps
            && damping == o.damping
            && stretchCompliance == o.stretchCompliance
            && shearCompliance == o.shearCompliance
            && bendCompliance == o.bendCompliance
            && windDir == o.windDir
            && windStrength == o.windStrength
            && dragCoefficient == o.dragCoefficient
            && windQuality == o.windQuality;
    }
};

static void pushClothUndo(CommandHistory* history, Scene* scene,
                          uint32_t entityId,
                          const ClothInspectorSnapshot& before,
                          const ClothInspectorSnapshot& after)
{
    if (!history || !scene || before == after)
    {
        return;
    }
    history->execute(std::make_unique<ComponentPropertyCommand<ClothInspectorSnapshot>>(
        before, after, "Change Cloth property",
        [scene, entityId](const ClothInspectorSnapshot& s)
        {
            if (auto* entity = scene->findEntityById(entityId))
            {
                if (auto* cloth = entity->getComponent<ClothComponent>())
                {
                    if (cloth->isReady())
                    {
                        ClothInspectorSnapshot::apply(*cloth, s);
                    }
                }
            }
        }));
}

void InspectorPanel::drawClothComponent(Entity& entity)
{
    auto* cloth = entity.getComponent<ClothComponent>();
    if (!cloth || !cloth->isReady()) return;

    if (!drawComponentHeader("Cloth Simulation")) return;

    auto& sim = cloth->getSimulator();
    const auto& config = sim.getConfig();

    ClothInspectorSnapshot before = ClothInspectorSnapshot::capture(*cloth);
    EditTracker tr;

    // --- Preset selector ---
    ClothPresetType currentPreset = cloth->getPresetType();
    int presetIdx = static_cast<int>(currentPreset);

    const char* presetNames[] = {
        "Custom", "Linen Curtain", "Tent Fabric", "Banner", "Heavy Drape", "Stiff Fence"
    };
    static_assert(static_cast<int>(ClothPresetType::COUNT) == 6,
                  "Update presetNames if ClothPresetType changes");

    bool presetChanged = ImGui::Combo("Preset", &presetIdx, presetNames,
                                       static_cast<int>(ClothPresetType::COUNT));
    tr.track(presetChanged);
    if (presetChanged)
    {
        auto type = static_cast<ClothPresetType>(presetIdx);
        cloth->applyPreset(type);
    }

    // --- Fabric material selector ---
    if (ImGui::TreeNode("Fabric Material"))
    {
        int fabricCount = FabricDatabase::getCount();

        // Common fabrics
        ImGui::TextDisabled("Common:");
        for (int i = 0; i < static_cast<int>(FabricType::FINE_LINEN); ++i)
        {
            auto ftype = static_cast<FabricType>(i);
            const auto& mat = FabricDatabase::get(ftype);
            bool fabricClicked = ImGui::Selectable(mat.name);
            tr.track(fabricClicked);
            if (fabricClicked)
            {
                auto presetCfg = FabricDatabase::toPresetConfig(ftype);
                sim.setParticleMass(presetCfg.solver.particleMass);
                sim.setSubsteps(presetCfg.solver.substeps);
                sim.setStretchCompliance(presetCfg.solver.stretchCompliance);
                sim.setShearCompliance(presetCfg.solver.shearCompliance);
                sim.setBendCompliance(presetCfg.solver.bendCompliance);
                sim.setDamping(presetCfg.solver.damping);
                sim.setDragCoefficient(presetCfg.dragCoefficient);
                cloth->setPresetType(ClothPresetType::CUSTOM);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s\n%.0f GSM, friction %.2f",
                                  mat.description, static_cast<double>(mat.densityGSM),
                                  static_cast<double>(mat.friction));
            }
        }

        // Biblical fabrics
        ImGui::Spacing();
        ImGui::TextDisabled("Biblical:");
        for (int i = static_cast<int>(FabricType::FINE_LINEN); i < fabricCount; ++i)
        {
            auto ftype = static_cast<FabricType>(i);
            const auto& mat = FabricDatabase::get(ftype);
            bool fabricClicked = ImGui::Selectable(mat.name);
            tr.track(fabricClicked);
            if (fabricClicked)
            {
                auto presetCfg = FabricDatabase::toPresetConfig(ftype);
                sim.setParticleMass(presetCfg.solver.particleMass);
                sim.setSubsteps(presetCfg.solver.substeps);
                sim.setStretchCompliance(presetCfg.solver.stretchCompliance);
                sim.setShearCompliance(presetCfg.solver.shearCompliance);
                sim.setBendCompliance(presetCfg.solver.bendCompliance);
                sim.setDamping(presetCfg.solver.damping);
                sim.setDragCoefficient(presetCfg.dragCoefficient);
                cloth->setPresetType(ClothPresetType::CUSTOM);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s\n%.0f GSM, friction %.2f",
                                  mat.description, static_cast<double>(mat.densityGSM),
                                  static_cast<double>(mat.friction));
            }
        }
        ImGui::TreePop();
    }

    // --- Reset button ---
    // Reset Simulation is a transient action (zeros velocities, repins) — not
    // a parameter mutation. Excluded from undo on purpose: tracking it would
    // need to snapshot all particle positions/velocities, which is a far
    // bigger lift than the rest of Ed2.
    if (ImGui::Button("Reset Simulation"))
    {
        cloth->reset();
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Track whether user manually edits any parameter (switches preset to Custom)
    bool paramChanged = false;

    // --- Solver section ---
    if (ImGui::TreeNodeEx("Solver", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // Read-only grid info
        ImGui::Text("Grid: %u x %u", sim.getGridWidth(), sim.getGridHeight());
        ImGui::Text("Spacing: %.3f m", static_cast<double>(config.spacing));

        // Editable parameters
        float mass = config.particleMass;
        bool massChanged = ImGui::DragFloat("Mass", &mass, 0.001f, 0.001f, 1.0f, "%.3f kg");
        tr.track(massChanged);
        if (massChanged)
        {
            sim.setParticleMass(mass);
            paramChanged = true;
        }

        int substeps = config.substeps;
        bool subChanged = ImGui::SliderInt("Substeps", &substeps, 1, 20);
        tr.track(subChanged);
        if (subChanged)
        {
            sim.setSubsteps(substeps);
            paramChanged = true;
        }

        float damping = config.damping;
        bool dampChanged = ImGui::DragFloat("Damping", &damping, 0.001f, 0.0f, 0.5f, "%.3f");
        tr.track(dampChanged);
        if (dampChanged)
        {
            sim.setDamping(damping);
            paramChanged = true;
        }

        ImGui::TreePop();
    }

    // --- Compliance section ---
    if (ImGui::TreeNodeEx("Compliance", ImGuiTreeNodeFlags_DefaultOpen))
    {
        float stretch = config.stretchCompliance;
        bool stretchChanged = ImGui::DragFloat("Stretch", &stretch, 0.00001f, 0.0f, 0.01f, "%.5f");
        tr.track(stretchChanged);
        if (stretchChanged)
        {
            sim.setStretchCompliance(stretch);
            paramChanged = true;
        }

        float shear = config.shearCompliance;
        bool shearChanged = ImGui::DragFloat("Shear", &shear, 0.0001f, 0.0f, 0.1f, "%.4f");
        tr.track(shearChanged);
        if (shearChanged)
        {
            sim.setShearCompliance(shear);
            paramChanged = true;
        }

        float bend = config.bendCompliance;
        bool bendChanged = ImGui::DragFloat("Bend", &bend, 0.01f, 0.0f, 2.0f, "%.3f");
        tr.track(bendChanged);
        if (bendChanged)
        {
            sim.setBendCompliance(bend);
            paramChanged = true;
        }

        ImGui::TreePop();
    }

    // --- Wind section ---
    if (ImGui::TreeNodeEx("Wind", ImGuiTreeNodeFlags_DefaultOpen))
    {
        glm::vec3 windDir = sim.getWindDirection();
        float windStrength = sim.getWindStrength();

        bool dirChanged = ImGui::DragFloat3("Direction", &windDir.x, 0.01f, -1.0f, 1.0f, "%.2f");
        tr.track(dirChanged);
        if (dirChanged)
        {
            float dirLen = glm::length(windDir);
            if (dirLen > 0.001f)
            {
                windDir /= dirLen;
            }
            sim.setWind(windDir, windStrength);
            paramChanged = true;
        }

        bool strengthChanged = ImGui::DragFloat("Strength", &windStrength, 0.1f, 0.0f, 30.0f, "%.1f");
        tr.track(strengthChanged);
        if (strengthChanged)
        {
            sim.setWind(windDir, windStrength);
            paramChanged = true;
        }

        float drag = sim.getDragCoefficient();
        bool dragChanged = ImGui::DragFloat("Drag", &drag, 0.1f, 0.0f, 10.0f, "%.1f");
        tr.track(dragChanged);
        if (dragChanged)
        {
            sim.setDragCoefficient(drag);
            paramChanged = true;
        }

        const char* windQualityItems[] = { "Full", "Approximate", "Simple" };
        int windQuality = static_cast<int>(sim.getWindQuality());
        bool wqChanged = ImGui::Combo("Wind Quality", &windQuality, windQualityItems, 3);
        tr.track(wqChanged);
        if (wqChanged)
        {
            sim.setWindQuality(static_cast<ClothWindQuality>(windQuality));
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Full: per-particle noise + per-triangle drag (best quality)\n"
                              "Approximate: uniform wind + per-triangle drag (good quality)\n"
                              "Simple: no wind simulation (fastest)");
        }

        ImGui::TreePop();
    }

    // --- Info section ---
    if (ImGui::TreeNode("Info"))
    {
        ImGui::Text("Particles: %u", sim.getParticleCount());
        ImGui::Text("Constraints: %u", sim.getConstraintCount());
        ImGui::Text("Pinned: %u", sim.getPinnedCount());
        ImGui::TreePop();
    }

    // If any parameter was manually changed, switch preset to Custom
    if (paramChanged && cloth->getPresetType() != ClothPresetType::CUSTOM)
    {
        cloth->setPresetType(ClothPresetType::CUSTOM);
    }

    if (tr.shouldCommit())
    {
        pushClothUndo(m_commandHistory, m_currentScene, entity.getId(),
                      before, ClothInspectorSnapshot::capture(*cloth));
    }

    ImGui::Spacing();
}

void InspectorPanel::drawPressurePlate(Entity& entity)
{
    auto* plate = entity.getComponent<PressurePlateComponent>();
    if (!plate) return;

    if (!drawComponentHeader("Pressure Plate")) return;

    ImGui::DragFloat("Detection Radius", &plate->detectionRadius, 0.05f, 0.1f, 20.0f, "%.2f m");
    ImGui::DragFloat("Detection Height", &plate->detectionHeight, 0.05f, 0.0f, 5.0f, "%.2f m");
    ImGui::DragFloat("Query Interval", &plate->queryInterval, 0.01f, 0.02f, 1.0f, "%.2f s");
    ImGui::Checkbox("Inverted", &plate->inverted);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("When inverted, plate activates when NO bodies are present");
    }

    ImGui::Separator();
    if (plate->isActivated())
    {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "ACTIVATED (%zu bodies)",
                           plate->getOverlapCount());
    }
    else
    {
        ImGui::TextDisabled("Inactive");
    }

    ImGui::Spacing();
}

} // namespace Vestige
