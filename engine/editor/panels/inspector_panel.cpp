/// @file inspector_panel.cpp
/// @brief Inspector panel implementation — property editing for selected entities.
#include "editor/panels/inspector_panel.h"
#include "editor/selection.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "scene/mesh_renderer.h"
#include "scene/light_component.h"
#include "renderer/material.h"
#include "renderer/texture.h"
#include "renderer/light_utils.h"

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

/// @brief Draws a vec3 with 3 coloured drag fields (red/green/blue labels).
static bool drawVec3Control(const char* label, glm::vec3& values,
                            float resetValue = 0.0f, float speed = 0.1f)
{
    bool changed = false;

    ImGui::PushID(label);

    // Label on left — use narrow column so XYZ fields have room
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, 75.0f);
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
        changed = true;
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(perDragWidth);
    if (ImGui::DragFloat("##X", &values.x, speed))
    {
        changed = true;
    }
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
    ImGui::SetNextItemWidth(perDragWidth);
    if (ImGui::DragFloat("##Y", &values.y, speed))
    {
        changed = true;
    }
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
    ImGui::SetNextItemWidth(std::max(20.0f, ImGui::GetContentRegionAvail().x));
    if (ImGui::DragFloat("##Z", &values.z, speed))
    {
        changed = true;
    }

    ImGui::PopStyleVar();
    ImGui::Columns(1);
    ImGui::PopID();

    return changed;
}

// ---------------------------------------------------------------------------
// Main draw
// ---------------------------------------------------------------------------

void InspectorPanel::initialize(const std::string& assetPath)
{
    m_materialPreview.initialize(assetPath);
}

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
    if (ImGui::CollapsingHeader("Preview", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // Mark dirty whenever the material is viewed (simple approach — re-renders once
        // per selection change and when properties change via inspector interaction)
        m_materialPreview.markDirty();
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

} // namespace Vestige
