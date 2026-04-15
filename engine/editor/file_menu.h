// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file file_menu.h
/// @brief File menu for scene save/load, unsaved changes dialog, and dirty tracking.
#pragma once

#include "editor/recent_files.h"

#include <imgui.h>
#include <imfilebrowser.h>

#include <chrono>
#include <filesystem>
#include <string>

struct GLFWwindow;

namespace Vestige
{

class CommandHistory;
class FoliageManager;
class ResourceManager;
class Scene;
class Selection;
class Terrain;

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

    /// @brief Sets the FoliageManager for environment save/load.
    void setFoliageManager(FoliageManager* manager);

    /// @brief Sets the Terrain for terrain save/load.
    void setTerrain(Terrain* terrain);

    /// @brief Ticks the auto-save timer. Call once per frame.
    /// Writes an autosave file when the scene is dirty and the interval has elapsed.
    /// @param scene Active scene to serialize.
    void tickAutoSave(const Scene* scene);

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
        OPEN_RECENT,
        QUIT
    };

    void newScene(Scene* scene, Selection& selection);
    void openScene();
    void openRecentScene(Scene* scene, Selection& selection);
    void saveScene(Scene* scene);
    void saveSceneAs(Scene* scene);
    void handleUnsavedChanges(PendingAction action, Scene* scene, Selection& selection);
    void proceedWithPendingAction(Scene* scene, Selection& selection);
    void handleOpenResult(Scene* scene, Selection& selection);
    void handleSaveResult(Scene* scene, Selection& selection);
    void drawRecentFilesMenu(Scene* scene, Selection& selection);
    void performAutoSave(const Scene& scene);
    void deleteAutoSave();
    void drawRecoveryModal(Scene* scene, Selection& selection);

    /// @brief Returns the autosave file path (~/.config/vestige/autosave.scene).
    static std::filesystem::path getAutoSavePath();

    GLFWwindow* m_window = nullptr;
    ResourceManager* m_resources = nullptr;
    CommandHistory* m_commandHistory = nullptr;
    FoliageManager* m_foliageManager = nullptr;
    Terrain* m_terrain = nullptr;

    std::filesystem::path m_currentScenePath;
    std::filesystem::path m_pendingRecentPath;
    bool m_isDirty = false;
    bool m_shouldQuit = false;

    // File browser dialogs
    ImGui::FileBrowser m_openBrowser;
    ImGui::FileBrowser m_saveBrowser;

    // Unsaved changes modal
    bool m_showUnsavedModal = false;
    PendingAction m_pendingAction = PendingAction::NONE;

    // Recent files
    RecentFiles m_recentFiles;

    // Auto-save
    static constexpr float AUTO_SAVE_INTERVAL = 120.0f;
    std::chrono::steady_clock::time_point m_lastAutoSaveTime;

    // Crash recovery
    bool m_recoveryPending = false;
    bool m_showRecoveryModal = false;
};

} // namespace Vestige
