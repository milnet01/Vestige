/// @file inspector_panel.h
/// @brief Inspector panel — displays and edits properties of the selected entity.
#pragma once

#include "editor/material_preview.h"

#include <cstdint>
#include <string>

namespace Vestige
{

class Entity;
class Scene;
class Selection;

/// @brief Draws and handles the inspector panel for the selected entity.
class InspectorPanel
{
public:
    /// @brief Initializes subsystems (material preview, etc.).
    /// @param assetPath Base path to assets directory.
    void initialize(const std::string& assetPath);

    /// @brief Draws the inspector contents inside the current ImGui window.
    /// @param scene Active scene (may be nullptr).
    /// @param selection Current editor selection.
    void draw(Scene* scene, Selection& selection);

private:
    void drawTransform(Entity& entity);
    void drawMeshRenderer(Entity& entity);
    void drawMaterial(class Material& material);
    void drawMaterialBlinnPhong(Material& material);
    void drawMaterialPbr(Material& material);
    void drawMaterialTextures(Material& material);
    void drawMaterialTransparency(Material& material);
    void drawDirectionalLight(Entity& entity);
    void drawPointLight(Entity& entity);
    void drawSpotLight(Entity& entity);
    void drawEmissiveLight(Entity& entity);

    MaterialPreview m_materialPreview;
    uint32_t m_lastPreviewMaterialId = 0;  ///< Track which material is being previewed
};

} // namespace Vestige
