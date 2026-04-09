/// @file template_dialog.cpp
/// @brief Game type template dialog implementation.
#include "editor/panels/template_dialog.h"
#include "core/logger.h"
#include "editor/entity_factory.h"
#include "editor/file_menu.h"
#include "editor/selection.h"
#include "renderer/renderer.h"
#include "resource/resource_manager.h"
#include "scene/scene.h"
#include "scene/entity.h"
#include "scene/camera_component.h"

#include <imgui.h>

namespace Vestige
{

// ============================================================================
// Template definitions
// ============================================================================

std::vector<GameTemplateConfig> TemplateDialog::getTemplates()
{
    std::vector<GameTemplateConfig> templates;

    // 3D First Person
    {
        GameTemplateConfig t;
        t.type = GameTemplateType::FIRST_PERSON_3D;
        t.displayName = "3D First Person";
        t.description = "Perspective camera at eye height. Full 3D physics with FPS-style "
                        "movement. Ideal for exploration, horror, or FPS games.";
        t.projectionType = ProjectionType::PERSPECTIVE;
        t.fov = 90.0f;
        t.cameraPosition = glm::vec3(0.0f, 1.7f, 0.0f);
        t.cameraRotation = glm::vec3(0.0f);
        t.enableGravity = true;
        t.createGround = true;
        t.createPlayerEntity = false;
        t.createDirectionalLight = true;
        t.createSkybox = true;
        t.inputProfile = "fps";
        templates.push_back(t);
    }

    // 3D Third Person
    {
        GameTemplateConfig t;
        t.type = GameTemplateType::THIRD_PERSON_3D;
        t.displayName = "3D Third Person";
        t.description = "Perspective camera following behind a character. Orbit/follow camera "
                        "with character controller. Ideal for action-adventure or RPGs.";
        t.projectionType = ProjectionType::PERSPECTIVE;
        t.fov = 60.0f;
        t.cameraPosition = glm::vec3(0.0f, 3.0f, -5.0f);
        t.cameraRotation = glm::vec3(-15.0f, 0.0f, 0.0f);
        t.enableGravity = true;
        t.createGround = true;
        t.createPlayerEntity = true;
        t.createDirectionalLight = true;
        t.createSkybox = true;
        t.inputProfile = "tps";
        templates.push_back(t);
    }

    // 2.5D
    {
        GameTemplateConfig t;
        t.type = GameTemplateType::TWO_POINT_FIVE_D;
        t.displayName = "2.5D Side-Scroller";
        t.description = "3D rendering with gameplay constrained to a vertical plane. "
                        "Side-scrolling camera. Ideal for platformers or beat-em-ups.";
        t.projectionType = ProjectionType::PERSPECTIVE;
        t.fov = 45.0f;
        t.cameraPosition = glm::vec3(0.0f, 5.0f, -10.0f);
        t.cameraRotation = glm::vec3(-20.0f, 0.0f, 0.0f);
        t.enableGravity = true;
        t.createGround = true;
        t.createPlayerEntity = false;
        t.createDirectionalLight = true;
        t.createSkybox = false;
        t.inputProfile = "sidescroll";
        templates.push_back(t);
    }

    // Isometric
    {
        GameTemplateConfig t;
        t.type = GameTemplateType::ISOMETRIC;
        t.displayName = "Isometric";
        t.description = "Fixed-angle orthographic camera at classic isometric angle (45/30). "
                        "Grid-based or free movement. Ideal for strategy, city-builders, or ARPGs.";
        t.projectionType = ProjectionType::ORTHOGRAPHIC;
        t.orthoSize = 10.0f;
        t.cameraPosition = glm::vec3(10.0f, 10.0f, 10.0f);
        t.cameraRotation = glm::vec3(-30.0f, -45.0f, 0.0f);
        t.enableGravity = true;
        t.createGround = true;
        t.createPlayerEntity = false;
        t.createDirectionalLight = true;
        t.createSkybox = false;
        t.inputProfile = "isometric";
        templates.push_back(t);
    }

    // Top-Down
    {
        GameTemplateConfig t;
        t.type = GameTemplateType::TOP_DOWN;
        t.displayName = "Top-Down";
        t.description = "Orthographic overhead camera looking straight down. Free movement "
                        "on the ground plane. Ideal for twin-stick shooters or roguelikes.";
        t.projectionType = ProjectionType::ORTHOGRAPHIC;
        t.orthoSize = 15.0f;
        t.cameraPosition = glm::vec3(0.0f, 20.0f, 0.0f);
        t.cameraRotation = glm::vec3(-90.0f, 0.0f, 0.0f);
        t.enableGravity = true;
        t.createGround = true;
        t.createPlayerEntity = false;
        t.createDirectionalLight = true;
        t.createSkybox = false;
        t.inputProfile = "topdown";
        templates.push_back(t);
    }

    // Point-and-Click
    {
        GameTemplateConfig t;
        t.type = GameTemplateType::POINT_AND_CLICK;
        t.displayName = "Point-and-Click";
        t.description = "Perspective camera at a fixed or panning angle. Click-to-move "
                        "navigation. Ideal for adventure games or puzzle games.";
        t.projectionType = ProjectionType::PERSPECTIVE;
        t.fov = 50.0f;
        t.cameraPosition = glm::vec3(0.0f, 8.0f, -6.0f);
        t.cameraRotation = glm::vec3(-40.0f, 0.0f, 0.0f);
        t.enableGravity = false;
        t.createGround = true;
        t.createPlayerEntity = false;
        t.createDirectionalLight = true;
        t.createSkybox = true;
        t.inputProfile = "pointclick";
        templates.push_back(t);
    }

    return templates;
}

// ============================================================================
// Dialog lifecycle
// ============================================================================

void TemplateDialog::open()
{
    m_open = true;
    m_selectedTemplate = 0;
    m_showUnsavedWarning = false;
    ImGui::OpenPopup("New from Template");
}

void TemplateDialog::draw(Scene* scene, ResourceManager* resources,
                           Renderer* renderer, Selection& selection,
                           FileMenu& fileMenu)
{
    if (!m_open)
    {
        return;
    }

    // Ensure the popup is opened
    if (!ImGui::IsPopupOpen("New from Template"))
    {
        ImGui::OpenPopup("New from Template");
    }

    ImGui::SetNextWindowSize(ImVec2(600.0f, 450.0f), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("New from Template", &m_open,
                                ImGuiWindowFlags_NoResize))
    {
        auto templates = getTemplates();

        ImGui::Text("Select a game type template:");
        ImGui::Separator();

        // Template list (left side)
        ImGui::BeginChild("##template_list", ImVec2(200.0f, -40.0f), true);
        for (int i = 0; i < static_cast<int>(templates.size()); ++i)
        {
            bool selected = (i == m_selectedTemplate);
            if (ImGui::Selectable(templates[static_cast<size_t>(i)].displayName.c_str(),
                                   selected))
            {
                m_selectedTemplate = i;
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Template details (right side)
        ImGui::BeginChild("##template_details", ImVec2(0.0f, -40.0f), true);
        if (m_selectedTemplate >= 0 &&
            m_selectedTemplate < static_cast<int>(templates.size()))
        {
            const auto& tmpl = templates[static_cast<size_t>(m_selectedTemplate)];
            ImGui::TextWrapped("%s", tmpl.displayName.c_str());
            ImGui::Separator();
            ImGui::TextWrapped("%s", tmpl.description.c_str());
            ImGui::Spacing();

            // Configuration summary
            ImGui::TextDisabled("Configuration:");
            const char* projName = (tmpl.projectionType == ProjectionType::PERSPECTIVE)
                                    ? "Perspective" : "Orthographic";
            ImGui::BulletText("Camera: %s", projName);
            if (tmpl.projectionType == ProjectionType::PERSPECTIVE)
            {
                ImGui::BulletText("FOV: %.0f degrees", static_cast<double>(tmpl.fov));
            }
            else
            {
                ImGui::BulletText("Ortho Size: %.0f", static_cast<double>(tmpl.orthoSize));
            }
            ImGui::BulletText("Camera Position: (%.0f, %.0f, %.0f)",
                               static_cast<double>(tmpl.cameraPosition.x),
                               static_cast<double>(tmpl.cameraPosition.y),
                               static_cast<double>(tmpl.cameraPosition.z));
            ImGui::BulletText("Gravity: %s", tmpl.enableGravity ? "Enabled" : "Disabled");
            ImGui::BulletText("Skybox: %s", tmpl.createSkybox ? "Yes" : "No");
            ImGui::BulletText("Input Profile: %s", tmpl.inputProfile.c_str());
        }
        ImGui::EndChild();

        // Buttons
        ImGui::Separator();
        if (ImGui::Button("Create", ImVec2(120.0f, 0.0f)))
        {
            if (scene && resources)
            {
                // Check for unsaved changes
                if (fileMenu.isDirty() && !m_showUnsavedWarning)
                {
                    m_showUnsavedWarning = true;
                }
                else
                {
                    const auto& tmpl = templates[static_cast<size_t>(m_selectedTemplate)];
                    applyTemplate(tmpl, scene, resources, renderer);
                    selection.clearSelection();
                    fileMenu.markDirty();
                    m_open = false;
                    m_showUnsavedWarning = false;
                    ImGui::CloseCurrentPopup();
                    Logger::info("[TemplateDialog] Created scene from template: " +
                                 tmpl.displayName);
                }
            }
        }

        if (m_showUnsavedWarning)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                                "Unsaved changes will be lost! Click Create again to confirm.");
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
        {
            m_open = false;
            m_showUnsavedWarning = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// ============================================================================
// Apply template
// ============================================================================

void TemplateDialog::applyTemplate(const GameTemplateConfig& config,
                                    Scene* scene, ResourceManager* resources,
                                    Renderer* renderer)
{
    if (!scene || !resources)
    {
        return;
    }

    // Clear existing scene
    scene->clearEntities();

    // Create ground plane
    if (config.createGround)
    {
        auto* ground = EntityFactory::createPlane(*scene, *resources, glm::vec3(0.0f));
        if (ground)
        {
            ground->setName("Ground");
            // Scale ground up for larger play area
            ground->transform.scale = glm::vec3(10.0f, 1.0f, 10.0f);
        }
    }

    // Create directional light
    if (config.createDirectionalLight)
    {
        auto* light = EntityFactory::createDirectionalLight(
            *scene, glm::vec3(5.0f, 10.0f, 5.0f));
        if (light)
        {
            light->setName("Sun");
        }
    }

    // Create player entity (for third-person template)
    if (config.createPlayerEntity)
    {
        auto* player = EntityFactory::createCube(*scene, *resources,
                                                  glm::vec3(0.0f, 0.5f, 0.0f));
        if (player)
        {
            player->setName("Player");
        }
    }

    // Create camera entity with CameraComponent
    auto* cameraEntity = EntityFactory::createEmptyEntity(*scene, config.cameraPosition);
    if (cameraEntity)
    {
        cameraEntity->setName("Camera");
        cameraEntity->transform.rotation = config.cameraRotation;

        auto* cameraComp = cameraEntity->addComponent<CameraComponent>();
        cameraComp->projectionType = config.projectionType;
        cameraComp->fov = config.fov;
        cameraComp->orthoSize = config.orthoSize;
    }

    // Load skybox
    if (config.createSkybox && renderer)
    {
        // Use default procedural sky (no HDRI path needed)
        renderer->setSkyboxEnabled(true);
    }
}

} // namespace Vestige
