// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file template_dialog.h
/// @brief Game type template dialog — creates pre-configured scenes for different game types.
#pragma once

#include "scene/camera_component.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Vestige
{

class FileMenu;
class Renderer;
class ResourceManager;
class Scene;
class Selection;

/// @brief Game template type identifiers.
enum class GameTemplateType
{
    FIRST_PERSON_3D,
    THIRD_PERSON_3D,
    TWO_POINT_FIVE_D,
    ISOMETRIC,
    TOP_DOWN,
    POINT_AND_CLICK
};

/// @brief Configuration for a game type template.
struct GameTemplateConfig
{
    GameTemplateType type;
    std::string displayName;
    std::string description;

    // Camera
    ProjectionType projectionType = ProjectionType::PERSPECTIVE;
    float fov = 90.0f;
    float orthoSize = 10.0f;
    glm::vec3 cameraPosition = glm::vec3(0.0f);
    glm::vec3 cameraRotation = glm::vec3(0.0f);  ///< Euler degrees (pitch, yaw, roll)

    // Physics
    bool enableGravity = true;

    // Starter entities
    bool createGround = true;
    bool createPlayerEntity = false;
    bool createDirectionalLight = true;
    bool createSkybox = false;

    // Input profile (metadata for future input system)
    std::string inputProfile;
};

/// @brief Modal dialog for creating a new scene from a game type template.
class TemplateDialog
{
public:
    /// @brief Opens the template selection dialog.
    void open();

    /// @brief Draws the dialog. Call inside the ImGui frame.
    /// @param scene Scene to populate with template entities.
    /// @param resources ResourceManager for entity creation.
    /// @param renderer Renderer for skybox loading (may be nullptr).
    /// @param selection Selection to update after creation.
    /// @param fileMenu FileMenu for dirty state checking.
    void draw(Scene* scene, ResourceManager* resources,
              Renderer* renderer, Selection& selection,
              FileMenu& fileMenu);

    bool isOpen() const { return m_open; }

    /// @brief Gets the list of available templates (for testing).
    static std::vector<GameTemplateConfig> getTemplates();

private:
    void applyTemplate(const GameTemplateConfig& config,
                        Scene* scene, ResourceManager* resources,
                        Renderer* renderer);

    bool m_open = false;
    int m_selectedTemplate = 0;
    bool m_showUnsavedWarning = false;
};

} // namespace Vestige
