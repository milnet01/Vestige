/// @file file_menu.h
/// @brief File menu for scene save/load, unsaved changes dialog, and dirty tracking.
#pragma once

#include <imgui.h>
#include <imfilebrowser.h>

#include <filesystem>
#include <string>

struct GLFWwindow;

namespace Vestige
{

class CommandHistory;
class ResourceManager;
class Scene;
class Selection;

/// @brief Manages the File menu, save/load dialogs, and unsaved changes tracking.
///
/// Owns the scene file path, dirty flag, and file browser dialogs. The Editor
/// calls drawMenuItems() inside BeginMenu("File"), processShortcuts() once per
/// frame for global Ctrl+N/O/S/Q, and drawDialogs() for file browsers and the
/// unsaved changes modal.
class FileMenu
{
public:
    FileMenu();

    /// @brief Sets the GLFW window handle for title bar updates.
    void setWindow(GLFWwindow* window);

    /// @brief Sets the ResourceManager pointer for scene loading.
    void setResourceManager(ResourceManager* resources);

    /// @brief Sets the CommandHistory for dirty tracking and clear-on-load.
    void setCommandHistory(CommandHistory* history);

    /// @brief Draws File menu items. Call inside ImGui::BeginMenu("File").
    /// @param scene Active scene (for save/load operations).
    /// @param selection Selection to clear on new/open.
    void drawMenuItems(Scene* scene, Selection& selection);

    /// @brief Processes global keyboard shortcuts (Ctrl+N/O/S/Shift+S/Q).
    /// Call once per frame in EDIT mode, before panel-specific shortcuts.
    /// @param scene Active scene.
    /// @param selection Selection to clear on new/open.
    void processShortcuts(Scene* scene, Selection& selection);

    /// @brief Draws file browser dialogs and the unsaved changes modal.
    /// Call once per frame, after all panels.
    /// @param scene Active scene.
    /// @param selection Selection to clear on new/open.
    void drawDialogs(Scene* scene, Selection& selection);

    /// @brief Requests a quit (from window close button or Ctrl+Q).
    /// Shows the unsaved changes modal if dirty; otherwise sets shouldQuit.
    void requestQuit();

    /// @brief Returns true if the application should quit.
    bool shouldQuit() const;

    /// @brief Marks the scene as modified (title bar shows *).
    void markDirty();

    /// @brief Marks the scene as clean (after save).
    void markClean();

    /// @brief Returns true if the scene has unsaved changes.
    bool isDirty() const;

    /// @brief Gets the current scene file path (empty if untitled).
    const std::filesystem::path& getCurrentScenePath() const;

    /// @brief Updates the GLFW window title to reflect scene name and dirty state.
    /// @param sceneName Scene name to display.
    void updateWindowTitle(const std::string& sceneName);

private:
    /// @brief Action that was pending when the unsaved changes dialog appeared.
    enum class PendingAction
    {
        NONE,
        NEW_SCENE,
        OPEN_SCENE,
        QUIT
    };

    void newScene(Scene* scene, Selection& selection);
    void openScene();
    void saveScene(Scene* scene);
    void saveSceneAs(Scene* scene);
    void handleUnsavedChanges(PendingAction action, Scene* scene, Selection& selection);
    void proceedWithPendingAction(Scene* scene, Selection& selection);
    void handleOpenResult(Scene* scene, Selection& selection);
    void handleSaveResult(Scene* scene);

    GLFWwindow* m_window = nullptr;
    ResourceManager* m_resources = nullptr;
    CommandHistory* m_commandHistory = nullptr;

    std::filesystem::path m_currentScenePath;
    bool m_isDirty = false;
    bool m_shouldQuit = false;

    // File browser dialogs
    ImGui::FileBrowser m_openBrowser;
    ImGui::FileBrowser m_saveBrowser;

    // Unsaved changes modal
    bool m_showUnsavedModal = false;
    PendingAction m_pendingAction = PendingAction::NONE;
};

} // namespace Vestige
