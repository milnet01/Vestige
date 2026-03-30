/// @file inspector_panel.h
/// @brief Inspector panel — displays and edits properties of the selected entity.
#pragma once

#include "editor/material_preview.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace Vestige
{

class CommandHistory;
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

    /// @brief Sets the command history for undo support.
    void setCommandHistory(CommandHistory* history) { m_commandHistory = history; }

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
    void drawParticleEmitter(Entity& entity);
    void drawWaterSurface(Entity& entity);

    MaterialPreview m_materialPreview;
    uint32_t m_lastPreviewMaterialId = 0;  ///< Track which material is being previewed

    // Undo support
    CommandHistory* m_commandHistory = nullptr;
    Scene* m_currentScene = nullptr;

    // Transform edit bracketing (DragFloat start/end tracking)
    bool m_transformEditActive = false;
    uint32_t m_editEntityId = 0;
    glm::vec3 m_editOldPos = glm::vec3(0.0f);
    glm::vec3 m_editOldRot = glm::vec3(0.0f);
    glm::vec3 m_editOldScale = glm::vec3(1.0f);
};

} // namespace Vestige
