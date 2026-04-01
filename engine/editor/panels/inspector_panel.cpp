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
#include "scene/particle_emitter.h"
#include "scene/water_surface.h"
#include "physics/cloth_component.h"
#include "editor/commands/particle_property_command.h"
#include "editor/widgets/curve_editor_widget.h"
#include "editor/widgets/gradient_editor_widget.h"
#include "renderer/material.h"
#include "renderer/texture.h"
#include "renderer/light_utils.h"
#include "resource/resource_manager.h"
#include "utils/material_library.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Vestige
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

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

    // --- Water Surface ---
    if (entity->hasComponent<WaterSurfaceComponent>())
    {
        drawWaterSurface(*entity);
    }

    // --- Cloth Simulation ---
    if (entity->hasComponent<ClothComponent>())
    {
        drawClothComponent(*entity);
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
        drawMaterial(*mr->getMaterial());
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
                MaterialLibrary::loadMaterial(presets[selectedPreset], material,
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

    ImGui::DragFloat("Radius",    &comp->lightRadius,    0.1f, 0.1f, 100.0f);
    ImGui::DragFloat("Intensity", &comp->lightIntensity, 0.05f, 0.0f, 50.0f,
                     "%.3f", ImGuiSliderFlags_Logarithmic);

    ImGui::ColorEdit3("Override Color", &comp->overrideColor.x);
    ImGui::SameLine();
    ImGui::TextDisabled("(0,0,0 = derive from material)");

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
        bool changed = false;
        changed |= ImGui::DragFloat("Rate", &cfg.emissionRate, 1.0f, 0.0f, 10000.0f, "%.0f/s");
        changed |= ImGui::DragInt("Max Particles", &cfg.maxParticles, 10.0f, 1, 100000);
        changed |= ImGui::Checkbox("Looping", &cfg.looping);
        if (!cfg.looping)
        {
            changed |= ImGui::DragFloat("Duration", &cfg.duration, 0.1f, 0.1f, 60.0f, "%.1f s");
        }

        if (changed && ImGui::IsItemDeactivatedAfterEdit())
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
        bool changed = false;
        changed |= ImGui::DragFloatRange2("Lifetime", &cfg.startLifetimeMin, &cfg.startLifetimeMax,
                                           0.05f, 0.01f, 30.0f, "%.2f s", "%.2f s");
        changed |= ImGui::DragFloatRange2("Speed", &cfg.startSpeedMin, &cfg.startSpeedMax,
                                           0.1f, 0.0f, 100.0f, "%.1f", "%.1f");
        changed |= ImGui::DragFloatRange2("Size", &cfg.startSizeMin, &cfg.startSizeMax,
                                           0.01f, 0.001f, 10.0f, "%.3f", "%.3f");
        changed |= ImGui::ColorEdit4("Start Color", &cfg.startColor.x);

        if (changed && ImGui::IsItemDeactivatedAfterEdit())
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
        bool changed = false;
        const char* shapeNames[] = {"Point", "Sphere", "Cone", "Box"};
        int shapeIdx = static_cast<int>(cfg.shape);
        if (ImGui::Combo("Type", &shapeIdx, shapeNames, 4))
        {
            cfg.shape = static_cast<ParticleEmitterConfig::Shape>(shapeIdx);
            changed = true;
        }

        switch (cfg.shape)
        {
            case ParticleEmitterConfig::Shape::SPHERE:
                changed |= ImGui::DragFloat("Radius", &cfg.shapeRadius, 0.1f, 0.0f, 50.0f);
                break;
            case ParticleEmitterConfig::Shape::CONE:
                changed |= ImGui::DragFloat("Radius", &cfg.shapeRadius, 0.1f, 0.0f, 50.0f);
                changed |= ImGui::DragFloat("Cone Angle", &cfg.shapeConeAngle, 1.0f, 1.0f, 89.0f, "%.0f deg");
                break;
            case ParticleEmitterConfig::Shape::BOX:
                changed |= ImGui::DragFloat3("Box Size", &cfg.shapeBoxSize.x, 0.1f, 0.0f, 50.0f);
                break;
            case ParticleEmitterConfig::Shape::POINT:
            default:
                break;
        }

        if (changed && ImGui::IsItemDeactivatedAfterEdit())
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
        bool changed = false;

        // Color over lifetime
        changed |= ImGui::Checkbox("Color Over Lifetime", &cfg.useColorOverLifetime);
        if (cfg.useColorOverLifetime)
        {
            ImGui::Indent();
            changed |= drawGradientEditor("Color Gradient", cfg.colorOverLifetime);
            ImGui::Unindent();
        }

        ImGui::Spacing();

        // Size over lifetime
        changed |= ImGui::Checkbox("Size Over Lifetime", &cfg.useSizeOverLifetime);
        if (cfg.useSizeOverLifetime)
        {
            ImGui::Indent();
            changed |= drawCurveEditor("Size Curve", cfg.sizeOverLifetime);
            ImGui::Unindent();
        }

        ImGui::Spacing();

        // Speed over lifetime
        changed |= ImGui::Checkbox("Speed Over Lifetime", &cfg.useSpeedOverLifetime);
        if (cfg.useSpeedOverLifetime)
        {
            ImGui::Indent();
            changed |= drawCurveEditor("Speed Curve", cfg.speedOverLifetime);
            ImGui::Unindent();
        }

        if (changed)
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
        bool changed = ImGui::DragFloat3("Gravity", &cfg.gravity.x, 0.1f, -50.0f, 50.0f);

        if (changed && ImGui::IsItemDeactivatedAfterEdit())
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
        bool changed = false;
        const char* blendNames[] = {"Additive", "Alpha Blend"};
        int blendIdx = static_cast<int>(cfg.blendMode);
        if (ImGui::Combo("Blend Mode", &blendIdx, blendNames, 2))
        {
            cfg.blendMode = static_cast<ParticleEmitterConfig::BlendMode>(blendIdx);
            changed = true;
        }

        // Texture path (read-only display for now, browse in 5E-3)
        if (!cfg.texturePath.empty())
        {
            ImGui::Text("Texture: %s", cfg.texturePath.c_str());
        }
        else
        {
            ImGui::TextDisabled("No texture (using default circle)");
        }

        if (changed)
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
        ParticleEmitterConfig lightBefore = cfg;
        bool lightChanged = false;

        if (ImGui::Checkbox("Emits Light", &cfg.emitsLight))
        {
            lightChanged = true;
        }

        if (cfg.emitsLight)
        {
            if (ImGui::ColorEdit3("Light Color", &cfg.lightColor.x))
            {
                lightChanged = true;
            }
            if (ImGui::DragFloat("Light Range", &cfg.lightRange, 0.1f, 0.5f, 50.0f, "%.1f m"))
            {
                lightChanged = true;
            }
            if (ImGui::DragFloat("Light Intensity", &cfg.lightIntensity, 0.05f, 0.0f, 10.0f, "%.2f"))
            {
                lightChanged = true;
            }
            if (ImGui::DragFloat("Flicker Speed", &cfg.flickerSpeed, 0.5f, 1.0f, 30.0f, "%.1f"))
            {
                lightChanged = true;
            }
        }

        if (lightChanged)
        {
            pushParticleUndo(m_commandHistory, m_currentScene, entity.getId(),
                             lightBefore, cfg, "lightCoupling");
        }
        ImGui::TreePop();
    }

    ImGui::Spacing();
}

// ---------------------------------------------------------------------------
// Water Surface
// ---------------------------------------------------------------------------

void InspectorPanel::drawWaterSurface(Entity& entity)
{
    auto* water = entity.getComponent<WaterSurfaceComponent>();
    if (!water) return;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
    if (ImGui::CollapsingHeader("Water Surface", ImGuiTreeNodeFlags_DefaultOpen))
    {
        auto& config = water->getConfig();

        // --- Presets ---
        const char* presets[] = {"Custom", "Still Bath", "Gentle Pool", "Flowing Stream", "Ocean Swell"};
        static int currentPreset = 0;
        if (ImGui::Combo("Preset", &currentPreset, presets, 5))
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
        if (ImGui::Combo("Reflection Mode", &reflMode, reflModes, 3))
        {
            config.reflectionMode = static_cast<WaterReflectionMode>(reflMode);
        }
        ImGui::SetItemTooltip("None: cheapest (no reflection)\n"
                              "Planar: accurate (re-renders scene)\n"
                              "Cubemap: cheaper (1 face/frame, lower quality)");

        if (config.reflectionMode != WaterReflectionMode::NONE)
        {
            float resScale = config.reflectionResolutionScale;
            if (ImGui::SliderFloat("Reflection Resolution", &resScale, 0.1f, 1.0f, "%.0f%%"))
            {
                config.reflectionResolutionScale = resScale;
            }
        }

        // --- Refraction ---
        ImGui::Checkbox("Refraction (Beer's Law)", &config.refractionEnabled);
        ImGui::SetItemTooltip("Depth-based underwater coloring.\n"
                              "Disabling saves a full scene render pass.");

        ImGui::Spacing();
        ImGui::Separator();

        // --- Colors ---
        ImGui::ColorEdit4("Shallow Color", &config.shallowColor.x);
        ImGui::ColorEdit4("Deep Color", &config.deepColor.x);

        ImGui::Spacing();
        ImGui::Separator();

        // --- Surface detail ---
        ImGui::SliderFloat("Normal Strength", &config.normalStrength, 0.0f, 3.0f);
        ImGui::SliderFloat("DuDv Strength", &config.dudvStrength, 0.0f, 0.1f, "%.4f");
        ImGui::SliderFloat("Flow Speed", &config.flowSpeed, 0.0f, 2.0f);
        ImGui::SliderFloat("Specular Power", &config.specularPower, 1.0f, 512.0f);

        ImGui::Spacing();
        ImGui::Separator();

        // --- Waves ---
        ImGui::SliderInt("Wave Count", &config.numWaves, 0, WaterSurfaceConfig::MAX_WAVES);
        for (int i = 0; i < config.numWaves; ++i)
        {
            ImGui::PushID(i);
            if (ImGui::TreeNode("Wave", "Wave %d", i + 1))
            {
                ImGui::SliderFloat("Amplitude", &config.waves[i].amplitude, 0.0f, 0.1f, "%.4f");
                ImGui::SliderFloat("Wavelength", &config.waves[i].wavelength, 0.1f, 10.0f);
                ImGui::SliderFloat("Speed", &config.waves[i].speed, 0.0f, 2.0f);
                ImGui::SliderFloat("Direction", &config.waves[i].direction, 0.0f, 360.0f, "%.0f deg");
                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        // --- Geometry ---
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::SliderFloat("Width", &config.width, 1.0f, 100.0f);
        ImGui::SliderFloat("Depth", &config.depth, 1.0f, 100.0f);
        ImGui::SliderInt("Grid Resolution", &config.gridResolution, 16, 256);
    }
    ImGui::PopStyleVar();
    ImGui::Spacing();
}

// ---------------------------------------------------------------------------
// Cloth Simulation
// ---------------------------------------------------------------------------

void InspectorPanel::drawClothComponent(Entity& entity)
{
    auto* cloth = entity.getComponent<ClothComponent>();
    if (!cloth || !cloth->isReady()) return;

    if (!drawComponentHeader("Cloth Simulation")) return;

    auto& sim = cloth->getSimulator();
    const auto& config = sim.getConfig();

    // --- Preset selector ---
    ClothPresetType currentPreset = cloth->getPresetType();
    int presetIdx = static_cast<int>(currentPreset);

    const char* presetNames[] = {
        "Custom", "Linen Curtain", "Tent Fabric", "Banner", "Heavy Drape", "Stiff Fence"
    };
    static_assert(static_cast<int>(ClothPresetType::COUNT) == 6,
                  "Update presetNames if ClothPresetType changes");

    if (ImGui::Combo("Preset", &presetIdx, presetNames,
                      static_cast<int>(ClothPresetType::COUNT)))
    {
        auto type = static_cast<ClothPresetType>(presetIdx);
        cloth->applyPreset(type);
    }

    // --- Reset button ---
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
        if (ImGui::DragFloat("Mass", &mass, 0.001f, 0.001f, 1.0f, "%.3f kg"))
        {
            sim.setParticleMass(mass);
            paramChanged = true;
        }

        int substeps = config.substeps;
        if (ImGui::SliderInt("Substeps", &substeps, 1, 20))
        {
            sim.setSubsteps(substeps);
            paramChanged = true;
        }

        float damping = config.damping;
        if (ImGui::DragFloat("Damping", &damping, 0.001f, 0.0f, 0.5f, "%.3f"))
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
        if (ImGui::DragFloat("Stretch", &stretch, 0.00001f, 0.0f, 0.01f, "%.5f"))
        {
            sim.setStretchCompliance(stretch);
            paramChanged = true;
        }

        float shear = config.shearCompliance;
        if (ImGui::DragFloat("Shear", &shear, 0.0001f, 0.0f, 0.1f, "%.4f"))
        {
            sim.setShearCompliance(shear);
            paramChanged = true;
        }

        float bend = config.bendCompliance;
        if (ImGui::DragFloat("Bend", &bend, 0.01f, 0.0f, 2.0f, "%.3f"))
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

        if (ImGui::DragFloat3("Direction", &windDir.x, 0.01f, -1.0f, 1.0f, "%.2f"))
        {
            float dirLen = glm::length(windDir);
            if (dirLen > 0.001f)
            {
                windDir /= dirLen;
            }
            sim.setWind(windDir, windStrength);
            paramChanged = true;
        }

        if (ImGui::DragFloat("Strength", &windStrength, 0.1f, 0.0f, 30.0f, "%.1f"))
        {
            sim.setWind(windDir, windStrength);
            paramChanged = true;
        }

        float drag = sim.getDragCoefficient();
        if (ImGui::DragFloat("Drag", &drag, 0.1f, 0.0f, 10.0f, "%.1f"))
        {
            sim.setDragCoefficient(drag);
            paramChanged = true;
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

    ImGui::Spacing();
}

} // namespace Vestige
