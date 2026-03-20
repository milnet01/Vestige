/// @file file_menu.cpp
/// @brief File menu implementation — save/load, unsaved changes dialog.
#include "editor/file_menu.h"
#include "editor/command_history.h"
#include "editor/scene_serializer.h"
#include "editor/selection.h"
#include "core/logger.h"
#include "resource/resource_manager.h"
#include "scene/scene.h"

#include <imgui.h>
#include <GLFW/glfw3.h>

namespace Vestige
{

FileMenu::FileMenu()
    : m_openBrowser(0)
    , m_saveBrowser(ImGuiFileBrowserFlags_EnterNewFilename
                    | ImGuiFileBrowserFlags_CreateNewDir)
{
    m_openBrowser.SetTitle("Open Scene");
    m_openBrowser.SetTypeFilters({".scene"});

    m_saveBrowser.SetTitle("Save Scene As");
    m_saveBrowser.SetTypeFilters({".scene"});
}

void FileMenu::setWindow(GLFWwindow* window)
{
    m_window = window;
}

void FileMenu::setResourceManager(ResourceManager* resources)
{
    m_resources = resources;
}

void FileMenu::setCommandHistory(CommandHistory* history)
{
    m_commandHistory = history;
}

// ---------------------------------------------------------------------------
// Menu items
// ---------------------------------------------------------------------------

void FileMenu::drawMenuItems(Scene* scene, Selection& selection)
{
    if (ImGui::MenuItem("New Scene", "Ctrl+N"))
    {
        handleUnsavedChanges(PendingAction::NEW_SCENE, scene, selection);
    }
    if (ImGui::MenuItem("Open Scene...", "Ctrl+O"))
    {
        handleUnsavedChanges(PendingAction::OPEN_SCENE, scene, selection);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
    {
        saveScene(scene);
    }
    if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S"))
    {
        saveSceneAs(scene);
    }
}

// ---------------------------------------------------------------------------
// Global keyboard shortcuts
// ---------------------------------------------------------------------------

void FileMenu::processShortcuts(Scene* scene, Selection& selection)
{
    ImGuiIO& io = ImGui::GetIO();

    // File shortcuts work globally in editor mode (even during text input).
    // Guard against double-trigger from menu items by checking that no popup
    // is open (the unsaved changes modal blocks further shortcut processing).
    if (ImGui::IsPopupOpen("Unsaved Changes"))
    {
        return;
    }

    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_N))
    {
        handleUnsavedChanges(PendingAction::NEW_SCENE, scene, selection);
    }
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O))
    {
        handleUnsavedChanges(PendingAction::OPEN_SCENE, scene, selection);
    }
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S))
    {
        saveScene(scene);
    }
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S))
    {
        saveSceneAs(scene);
    }
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Q))
    {
        handleUnsavedChanges(PendingAction::QUIT, scene, selection);
    }
}

// ---------------------------------------------------------------------------
// Dialogs (file browsers + unsaved changes modal)
// ---------------------------------------------------------------------------

void FileMenu::drawDialogs(Scene* scene, Selection& selection)
{
    // --- File browser: Open ---
    m_openBrowser.Display();
    if (m_openBrowser.HasSelected())
    {
        handleOpenResult(scene, selection);
    }

    // --- File browser: Save As ---
    m_saveBrowser.Display();
    if (m_saveBrowser.HasSelected())
    {
        handleSaveResult(scene);
    }

    // --- Unsaved changes modal ---
    if (m_showUnsavedModal)
    {
        ImGui::OpenPopup("Unsaved Changes");
        m_showUnsavedModal = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize
                                | ImGuiWindowFlags_NoMove))
    {
        std::string fileName = m_currentScenePath.empty()
            ? "Untitled Scene"
            : m_currentScenePath.filename().string();

        ImGui::Text("\"%s\" has been modified.", fileName.c_str());
        ImGui::Text("Save changes before closing?");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Three buttons: Don't Save | Cancel | Save
        if (ImGui::Button("Don't Save", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup();
            // Discard and proceed with the pending action
            m_isDirty = false;
            proceedWithPendingAction(scene, selection);
        }

        ImGui::SameLine();

        // Push Cancel and Save to the right
        float rightEdge = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
        ImGui::SetCursorPosX(rightEdge - 252);

        if (ImGui::Button("Cancel", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup();
            m_pendingAction = PendingAction::NONE;
        }

        ImGui::SameLine();

        if (ImGui::Button("Save", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup();
            saveScene(scene);
            // Only proceed if save succeeded (not redirected to Save As)
            if (!m_isDirty)
            {
                proceedWithPendingAction(scene, selection);
            }
        }

        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------
// Quit
// ---------------------------------------------------------------------------

void FileMenu::requestQuit()
{
    // Avoid re-triggering if already handling a pending action
    if (m_pendingAction != PendingAction::NONE)
    {
        return;
    }

    if (m_isDirty)
    {
        m_pendingAction = PendingAction::QUIT;
        m_showUnsavedModal = true;

        // Cancel the GLFW close flag so the loop keeps running
        if (m_window)
        {
            glfwSetWindowShouldClose(m_window, GLFW_FALSE);
        }
    }
    else
    {
        m_shouldQuit = true;
    }
}

bool FileMenu::shouldQuit() const
{
    return m_shouldQuit;
}

// ---------------------------------------------------------------------------
// Dirty state
// ---------------------------------------------------------------------------

void FileMenu::markDirty()
{
    m_isDirty = true;
}

void FileMenu::markClean()
{
    m_isDirty = false;
    if (m_commandHistory)
    {
        m_commandHistory->markSaved();
    }
}

bool FileMenu::isDirty() const
{
    // Prefer CommandHistory for dirty tracking when available
    if (m_commandHistory)
    {
        return m_commandHistory->isDirty() || m_isDirty;
    }
    return m_isDirty;
}

// ---------------------------------------------------------------------------
// Scene path
// ---------------------------------------------------------------------------

const std::filesystem::path& FileMenu::getCurrentScenePath() const
{
    return m_currentScenePath;
}

void FileMenu::updateWindowTitle(const std::string& sceneName)
{
    if (!m_window)
    {
        return;
    }

    std::string title;
    if (m_isDirty)
    {
        title = "*";
    }

    if (m_currentScenePath.empty())
    {
        title += sceneName.empty() ? "Untitled Scene" : sceneName;
    }
    else
    {
        title += m_currentScenePath.filename().string();
    }

    title += " - Vestige";

    glfwSetWindowTitle(m_window, title.c_str());
}

// ---------------------------------------------------------------------------
// Private: action routing
// ---------------------------------------------------------------------------

void FileMenu::handleUnsavedChanges(PendingAction action,
                                     Scene* scene, Selection& selection)
{
    if (m_isDirty)
    {
        m_pendingAction = action;
        m_showUnsavedModal = true;
    }
    else
    {
        m_pendingAction = action;
        proceedWithPendingAction(scene, selection);
    }
}

void FileMenu::proceedWithPendingAction(Scene* scene, Selection& selection)
{
    PendingAction action = m_pendingAction;
    m_pendingAction = PendingAction::NONE;

    switch (action)
    {
        case PendingAction::NEW_SCENE:
            newScene(scene, selection);
            break;
        case PendingAction::OPEN_SCENE:
            openScene();
            break;
        case PendingAction::QUIT:
            m_shouldQuit = true;
            break;
        case PendingAction::NONE:
            break;
    }
}

// ---------------------------------------------------------------------------
// Private: scene operations
// ---------------------------------------------------------------------------

void FileMenu::newScene(Scene* scene, Selection& selection)
{
    if (!scene)
    {
        return;
    }

    scene->clearEntities();
    scene->setName("Untitled Scene");
    selection.clearSelection();
    m_currentScenePath.clear();
    if (m_commandHistory)
    {
        m_commandHistory->clear();
    }
    markClean();
    updateWindowTitle("Untitled Scene");
    Logger::info("New scene created");
}

void FileMenu::openScene()
{
    m_openBrowser.Open();
}

void FileMenu::saveScene(Scene* scene)
{
    if (!scene || !m_resources)
    {
        return;
    }

    if (m_currentScenePath.empty())
    {
        // No path yet — redirect to Save As
        saveSceneAs(scene);
        return;
    }

    SceneSerializerResult result = SceneSerializer::saveScene(
        *scene, m_currentScenePath, *m_resources);

    if (result.success)
    {
        markClean();
        updateWindowTitle(scene->getName());
    }
    else
    {
        Logger::error("Failed to save scene: " + result.errorMessage);
    }
}

void FileMenu::saveSceneAs(Scene* scene)
{
    if (!scene)
    {
        return;
    }

    // Set initial directory from current path
    if (!m_currentScenePath.empty())
    {
        m_saveBrowser.SetPwd(m_currentScenePath.parent_path());
    }

    m_saveBrowser.Open();
}

void FileMenu::handleOpenResult(Scene* scene, Selection& selection)
{
    std::filesystem::path selectedPath = m_openBrowser.GetSelected();
    m_openBrowser.ClearSelected();

    if (!scene || !m_resources || selectedPath.empty())
    {
        return;
    }

    SceneSerializerResult result = SceneSerializer::loadScene(
        *scene, selectedPath, *m_resources);

    if (result.success)
    {
        m_currentScenePath = selectedPath;
        selection.clearSelection();
        if (m_commandHistory)
        {
            m_commandHistory->clear();
        }
        markClean();
        updateWindowTitle(scene->getName());
        Logger::info("Scene loaded: " + selectedPath.string()
                     + " (" + std::to_string(result.entityCount) + " entities)");
    }
    else
    {
        Logger::error("Failed to load scene: " + result.errorMessage);
    }
}

void FileMenu::handleSaveResult(Scene* scene)
{
    std::filesystem::path selectedPath = m_saveBrowser.GetSelected();
    m_saveBrowser.ClearSelected();

    if (!scene || !m_resources || selectedPath.empty())
    {
        return;
    }

    // Ensure .scene extension
    if (selectedPath.extension() != ".scene")
    {
        selectedPath += ".scene";
    }

    m_currentScenePath = selectedPath;
    scene->setName(selectedPath.stem().string());

    SceneSerializerResult result = SceneSerializer::saveScene(
        *scene, m_currentScenePath, *m_resources);

    if (result.success)
    {
        markClean();
        updateWindowTitle(scene->getName());
    }
    else
    {
        Logger::error("Failed to save scene: " + result.errorMessage);
    }
}

} // namespace Vestige
