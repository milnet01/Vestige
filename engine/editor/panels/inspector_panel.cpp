/// @file inspector_panel.cpp
/// @brief Inspector panel implementation — property editing for selected entities.
#include "editor/panels/inspector_panel.h"
#include "editor/selection.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "scene/mesh_renderer.h"
#include "scene/light_component.h"
#include "renderer/material.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
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

/// @brief Draws a vec3 with 3 coloured drag fields (red/green/blue labels).
static bool drawVec3Control(const char* label, glm::vec3& values,
                            float resetValue = 0.0f, float speed = 0.1f)
{
    bool changed = false;

    ImGui::PushID(label);

    // Label on left
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, 100.0f);
    ImGui::Text("%s", label);
    ImGui::NextColumn();

    ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));

    float lineHeight = ImGui::GetFrameHeight();
    ImVec2 buttonSize = { lineHeight, lineHeight };

    // --- X (red) ---
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.85f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
    if (ImGui::Button("X", buttonSize))
    {
        values.x = resetValue;
        changed = true;
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine();
    if (ImGui::DragFloat("##X", &values.x, speed))
    {
        changed = true;
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();

    // --- Y (green) ---
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.15f, 0.6f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.2f, 0.75f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.15f, 0.6f, 0.15f, 1.0f));
    if (ImGui::Button("Y", buttonSize))
    {
        values.y = resetValue;
        changed = true;
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine();
    if (ImGui::DragFloat("##Y", &values.y, speed))
    {
        changed = true;
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();

    // --- Z (blue) ---
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.15f, 0.15f, 0.7f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.2f, 0.2f, 0.85f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.15f, 0.15f, 0.7f, 1.0f));
    if (ImGui::Button("Z", buttonSize))
    {
        values.z = resetValue;
        changed = true;
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine();
    if (ImGui::DragFloat("##Z", &values.z, speed))
    {
        changed = true;
    }
    ImGui::PopItemWidth();

    ImGui::PopStyleVar();
    ImGui::Columns(1);
    ImGui::PopID();

    return changed;
}

// ---------------------------------------------------------------------------
// Main draw
// ---------------------------------------------------------------------------

void InspectorPanel::draw(Scene* scene, Selection& selection)
{
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
        bool isActive = entity->isActive();
        if (ImGui::Checkbox("##Active", &isActive))
        {
            entity->setActive(isActive);
        }
        ImGui::SameLine();

        char nameBuf[256];
        std::strncpy(nameBuf, entity->getName().c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##EntityName", nameBuf, sizeof(nameBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue))
        {
            if (std::strlen(nameBuf) > 0)
            {
                entity->setName(nameBuf);
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

    drawVec3Control("Position", entity.transform.position, 0.0f, 0.05f);
    drawVec3Control("Rotation", entity.transform.rotation, 0.0f, 1.0f);
    drawVec3Control("Scale",    entity.transform.scale,    1.0f, 0.02f);

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

    ImGui::PopID();
}

void InspectorPanel::drawMaterialBlinnPhong(Material& material)
{
    glm::vec3 diffuse = material.getDiffuseColor();
    if (ImGui::ColorEdit3("Diffuse", &diffuse.x))
    {
        material.setDiffuseColor(diffuse);
    }

    glm::vec3 specular = material.getSpecularColor();
    if (ImGui::ColorEdit3("Specular", &specular.x))
    {
        material.setSpecularColor(specular);
    }

    float shininess = material.getShininess();
    if (ImGui::DragFloat("Shininess", &shininess, 1.0f, 1.0f, 512.0f))
    {
        material.setShininess(shininess);
    }

    ImGui::Spacing();
}

void InspectorPanel::drawMaterialPbr(Material& material)
{
    glm::vec3 albedo = material.getAlbedo();
    if (ImGui::ColorEdit3("Albedo", &albedo.x))
    {
        material.setAlbedo(albedo);
    }

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

    float ao = material.getAo();
    if (ImGui::SliderFloat("AO", &ao, 0.0f, 1.0f))
    {
        material.setAo(ao);
    }

    // Clearcoat
    float clearcoat = material.getClearcoat();
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

    // Emissive
    glm::vec3 emissive = material.getEmissive();
    if (ImGui::ColorEdit3("Emissive", &emissive.x))
    {
        material.setEmissive(emissive);
    }

    float emissiveStrength = material.getEmissiveStrength();
    if (ImGui::DragFloat("Emissive Strength", &emissiveStrength, 0.1f, 0.0f, 100.0f))
    {
        material.setEmissiveStrength(emissiveStrength);
    }

    float uvScale = material.getUvScale();
    if (ImGui::DragFloat("UV Scale", &uvScale, 0.05f, 0.01f, 50.0f))
    {
        material.setUvScale(uvScale);
    }

    ImGui::Spacing();
}

void InspectorPanel::drawMaterialTextures(Material& material)
{
    if (ImGui::TreeNode("Textures"))
    {
        auto showTexSlot = [](const char* label, bool hasTexture)
        {
            if (hasTexture)
            {
                ImGui::BulletText("%s: loaded", label);
            }
            else
            {
                ImGui::BulletText("%s: (none)", label);
            }
        };

        showTexSlot("Diffuse/Albedo", material.hasDiffuseTexture());
        showTexSlot("Normal Map",     material.hasNormalMap());
        showTexSlot("Height Map",     material.hasHeightMap());

        if (material.getType() == MaterialType::PBR)
        {
            showTexSlot("Metallic-Roughness", material.hasMetallicRoughnessTexture());
            showTexSlot("Emissive",           material.hasEmissiveTexture());
            showTexSlot("AO",                 material.hasAoTexture());
        }

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

        // Stochastic tiling
        bool stochastic = material.isStochasticTiling();
        if (ImGui::Checkbox("Stochastic Tiling", &stochastic))
        {
            material.setStochasticTiling(stochastic);
        }

        ImGui::TreePop();
    }
}

void InspectorPanel::drawMaterialTransparency(Material& material)
{
    if (ImGui::TreeNode("Transparency"))
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

        ImGui::TreePop();
    }
}

// ---------------------------------------------------------------------------
// Light components
// ---------------------------------------------------------------------------

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

    ImGui::ColorEdit3("Ambient",  &light.ambient.x);
    ImGui::ColorEdit3("Diffuse",  &light.diffuse.x);
    ImGui::ColorEdit3("Specular", &light.specular.x);

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

    ImGui::ColorEdit3("Ambient",  &light.ambient.x);
    ImGui::ColorEdit3("Diffuse",  &light.diffuse.x);
    ImGui::ColorEdit3("Specular", &light.specular.x);

    ImGui::Spacing();
    ImGui::Text("Attenuation");
    ImGui::DragFloat("Constant",  &light.constant,  0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Linear",    &light.linear,    0.001f, 0.0f, 2.0f);
    ImGui::DragFloat("Quadratic", &light.quadratic, 0.001f, 0.0f, 2.0f);

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

    ImGui::ColorEdit3("Ambient",  &light.ambient.x);
    ImGui::ColorEdit3("Diffuse",  &light.diffuse.x);
    ImGui::ColorEdit3("Specular", &light.specular.x);

    // Convert cos cutoffs to degrees for editing, then back
    float innerDeg = glm::degrees(std::acos(std::clamp(light.innerCutoff, -1.0f, 1.0f)));
    float outerDeg = glm::degrees(std::acos(std::clamp(light.outerCutoff, -1.0f, 1.0f)));
    if (ImGui::DragFloat("Inner Angle", &innerDeg, 0.5f, 0.0f, 90.0f, "%.1f deg"))
    {
        light.innerCutoff = std::cos(glm::radians(innerDeg));
    }
    if (ImGui::DragFloat("Outer Angle", &outerDeg, 0.5f, 0.0f, 90.0f, "%.1f deg"))
    {
        light.outerCutoff = std::cos(glm::radians(outerDeg));
    }

    ImGui::Spacing();
    ImGui::Text("Attenuation");
    ImGui::DragFloat("Constant",  &light.constant,  0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Linear",    &light.linear,    0.001f, 0.0f, 2.0f);
    ImGui::DragFloat("Quadratic", &light.quadratic, 0.001f, 0.0f, 2.0f);

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
    ImGui::DragFloat("Intensity", &comp->lightIntensity, 0.05f, 0.0f, 50.0f);

    ImGui::ColorEdit3("Override Color", &comp->overrideColor.x);
    ImGui::SameLine();
    ImGui::TextDisabled("(0,0,0 = derive from material)");

    ImGui::Spacing();
}

} // namespace Vestige
