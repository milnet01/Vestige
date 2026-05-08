// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file file_menu.cpp
/// @brief File menu implementation — save/load, unsaved changes dialog, auto-save, recent files.
#include "editor/file_menu.h"
#include "editor/command_history.h"
#include "editor/scene_serializer.h"
#include "editor/selection.h"
#include "core/logger.h"
#include "resource/resource_manager.h"
#include "scene/scene.h"
#include "utils/atomic_write.h"

#include <imgui.h>
#include <GLFW/glfw3.h>

#include <fstream>

namespace Vestige
{

namespace fs = std::filesystem;

FileMenu::FileMenu()
    : m_openBrowser(0)
    , m_saveBrowser(ImGuiFileBrowserFlags_EnterNewFilename
                    | ImGuiFileBrowserFlags_CreateNewDir)
    , m_lastAutoSaveTime(std::chrono::steady_clock::now())
{
    m_openBrowser.SetTitle("Open Scene");
    m_openBrowser.SetTypeFilters({".scene"});

    m_saveBrowser.SetTitle("Save Scene As");
    m_saveBrowser.SetTypeFilters({".scene"});

    // Load recent files from disk
    m_recentFiles.load();

    // Check if a crash recovery file exists
    if (fs::exists(getAutoSavePath()))
    {
        m_recoveryPending = true;
    }
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

void FileMenu::setFoliageManager(FoliageManager* manager)
{
    m_foliageManager = manager;
}

void FileMenu::setTerrain(Terrain* terrain)
{
    m_terrain = terrain;
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
    drawRecentFilesMenu(scene, selection);
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
    // --- Crash recovery dialog (shown once on startup if autosave exists) ---
    if (m_recoveryPending && scene && m_resources)
    {
        m_showRecoveryModal = true;
        m_recoveryPending = false;
    }
    drawRecoveryModal(scene, selection);

    // --- File browser: Open ---
    bool openBrowserWasOpen = m_openBrowser.IsOpened();
    m_openBrowser.Display();
    if (m_openBrowser.HasSelected())
    {
        handleOpenResult(scene, selection);
    }
    else if (openBrowserWasOpen && !m_openBrowser.IsOpened())
    {
        // User cancelled the Open dialog — clear pending action
        m_pendingAction = PendingAction::NONE;
    }

    // --- File browser: Save As ---
    bool saveBrowserWasOpen = m_saveBrowser.IsOpened();
    m_saveBrowser.Display();
    if (m_saveBrowser.HasSelected())
    {
        handleSaveResult(scene, selection);
    }
    else if (saveBrowserWasOpen && !m_saveBrowser.IsOpened())
    {
        // User cancelled the Save As dialog — clear pending action so the
        // app can respond to subsequent close/quit requests.
        m_pendingAction = PendingAction::NONE;
    }

    // --- Unsaved changes modal ---
    if (m_showUnsavedModal)
    {
        ImGui::OpenPopup("Unsaved Changes");
        m_showUnsavedModal = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    // Ensure the modal is wide enough for three buttons with spacing
    ImGui::SetNextWindowSizeConstraints(ImVec2(420, 0), ImVec2(FLT_MAX, FLT_MAX));

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

        // Buttons: Don't Save | Cancel | [Save] | Save As
        // "Don't Save" left-aligned, rest right-aligned
        float buttonW = 120.0f;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        bool hasExistingPath = !m_currentScenePath.empty();

        if (ImGui::Button("Don't Save", ImVec2(buttonW, 0)))
        {
            ImGui::CloseCurrentPopup();
            markClean();
            proceedWithPendingAction(scene, selection);
        }

        ImGui::SameLine();

        // Right-align the remaining buttons
        float rightEdge = ImGui::GetWindowContentRegionMax().x;
        int rightButtonCount = hasExistingPath ? 3 : 2;  // Cancel + [Save] + Save As
        ImGui::SetCursorPosX(rightEdge - static_cast<float>(rightButtonCount) * buttonW
                             - static_cast<float>(rightButtonCount - 1) * spacing);

        if (ImGui::Button("Cancel", ImVec2(buttonW, 0)))
        {
            ImGui::CloseCurrentPopup();
            m_pendingAction = PendingAction::NONE;
        }

        ImGui::SameLine();

        // "Save" only shown when a file path exists (overwrites without prompting)
        if (hasExistingPath)
        {
            if (ImGui::Button("Save", ImVec2(buttonW, 0)))
            {
                ImGui::CloseCurrentPopup();
                saveScene(scene);
                if (!isDirty())
                {
                    proceedWithPendingAction(scene, selection);
                }
            }
            ImGui::SameLine();
        }

        if (ImGui::Button("Save As...", ImVec2(buttonW, 0)))
        {
            ImGui::CloseCurrentPopup();
            saveSceneAs(scene);
            // Pending action is preserved — will be processed in
            // handleSaveResult after the file browser completes.
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

    if (isDirty())
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
    // Phase 10.9 Slice 12 Ed7: forward to CommandHistory's sticky-dirty
    // flag so isDirty() naturally clears on save; pre-Ed7 the
    // FileMenu::m_isDirty flag stuck forever regardless of undo.
    if (m_commandHistory)
    {
        m_commandHistory->markUnsavedChange();
    }
}

void FileMenu::markClean()
{
    if (m_commandHistory)
    {
        m_commandHistory->markSaved();
    }
    // Reset auto-save timer so the next autosave is a full interval after save
    m_lastAutoSaveTime = std::chrono::steady_clock::now();
}

bool FileMenu::isDirty() const
{
    // Phase 10.9 Slice 12 Ed7: single source of truth. CommandHistory
    // tracks both "version differs from save" (real mutations) and
    // "saved version lost" (sticky flag for non-undoable scene loads,
    // set via markUnsavedChange()).
    return m_commandHistory && m_commandHistory->isDirty();
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
    if (isDirty())
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
    if (isDirty())
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
        case PendingAction::OPEN_RECENT:
            openRecentScene(scene, selection);
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
    deleteAutoSave();
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
        *scene, m_currentScenePath, *m_resources, m_foliageManager, m_terrain);

    if (result.success)
    {
        markClean();
        deleteAutoSave();
        m_recentFiles.addPath(m_currentScenePath);
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
        *scene, selectedPath, *m_resources, m_foliageManager, m_terrain);

    if (result.success)
    {
        m_currentScenePath = selectedPath;
        selection.clearSelection();
        if (m_commandHistory)
        {
            m_commandHistory->clear();
        }
        markClean();
        deleteAutoSave();
        m_recentFiles.addPath(selectedPath);
        updateWindowTitle(scene->getName());
        Logger::info("Scene loaded: " + selectedPath.string()
                     + " (" + std::to_string(result.entityCount) + " entities)");
    }
    else
    {
        Logger::error("Failed to load scene: " + result.errorMessage);
    }
}

void FileMenu::handleSaveResult(Scene* scene, Selection& selection)
{
    fs::path selectedPath = m_saveBrowser.GetSelected();
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
        *scene, m_currentScenePath, *m_resources, m_foliageManager, m_terrain);

    if (result.success)
    {
        markClean();
        deleteAutoSave();
        m_recentFiles.addPath(selectedPath);
        updateWindowTitle(scene->getName());

        // If there was a pending action (e.g. QUIT) that triggered the save,
        // proceed with it now that the save completed.
        if (m_pendingAction != PendingAction::NONE)
        {
            proceedWithPendingAction(scene, selection);
        }
    }
    else
    {
        Logger::error("Failed to save scene: " + result.errorMessage);
    }
}

// ---------------------------------------------------------------------------
// Recent files submenu
// ---------------------------------------------------------------------------

void FileMenu::drawRecentFilesMenu(Scene* scene, Selection& selection)
{
    const auto& paths = m_recentFiles.getPaths();

    if (ImGui::BeginMenu("Recent Scenes", !paths.empty()))
    {
        for (const auto& path : paths)
        {
            std::string displayName = path.filename().string();
            if (ImGui::MenuItem(displayName.c_str()))
            {
                m_pendingRecentPath = path;
                handleUnsavedChanges(PendingAction::OPEN_RECENT, scene, selection);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", path.string().c_str());
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Clear Recent Files"))
        {
            m_recentFiles.clear();
        }
        ImGui::EndMenu();
    }
}

void FileMenu::openRecentScene(Scene* scene, Selection& selection)
{
    if (!scene || !m_resources || m_pendingRecentPath.empty())
    {
        return;
    }

    if (!fs::exists(m_pendingRecentPath))
    {
        Logger::warning("Recent file no longer exists: " + m_pendingRecentPath.string());
        m_recentFiles.removePath(m_pendingRecentPath);
        m_pendingRecentPath.clear();
        return;
    }

    SceneSerializerResult result = SceneSerializer::loadScene(
        *scene, m_pendingRecentPath, *m_resources, m_foliageManager, m_terrain);

    if (result.success)
    {
        m_currentScenePath = m_pendingRecentPath;
        selection.clearSelection();
        if (m_commandHistory)
        {
            m_commandHistory->clear();
        }
        markClean();
        deleteAutoSave();
        m_recentFiles.addPath(m_pendingRecentPath);
        updateWindowTitle(scene->getName());
        Logger::info("Loaded recent scene: " + m_pendingRecentPath.string()
                     + " (" + std::to_string(result.entityCount) + " entities)");
    }
    else
    {
        Logger::error("Failed to load recent scene: " + result.errorMessage);
    }

    m_pendingRecentPath.clear();
}

// ---------------------------------------------------------------------------
// Auto-save
// ---------------------------------------------------------------------------

void FileMenu::tickAutoSave(const Scene* scene)
{
    auto now = std::chrono::steady_clock::now();

    if (!scene || !m_resources || !isDirty())
    {
        return;
    }

    float elapsed = std::chrono::duration<float>(now - m_lastAutoSaveTime).count();

    if (elapsed >= AUTO_SAVE_INTERVAL)
    {
        performAutoSave(*scene);
        m_lastAutoSaveTime = now;
    }
}

void FileMenu::performAutoSave(const Scene& scene)
{
    std::string content = SceneSerializer::serializeToString(scene, *m_resources);
    if (content.empty())
    {
        Logger::warning("Auto-save: serialization failed");
        return;
    }

    fs::path autoSavePath = getAutoSavePath();

    // Durable write via the canonical helper (creates parent dirs,
    // fsync + rename + fsync-dir). F7 replaces the earlier ad-hoc
    // tmp+rename dance that omitted fsync.
    AtomicWrite::Status s = AtomicWrite::writeFile(autoSavePath, content);
    if (s != AtomicWrite::Status::Ok)
    {
        Logger::warning(std::string("Auto-save: ") + AtomicWrite::describe(s)
                        + " for " + autoSavePath.string());
        return;
    }

    Logger::info("Auto-saved to " + autoSavePath.string());

    // Save the original scene path alongside the autosave so recovery
    // can restore it.
    fs::path pathFile = autoSavePath;
    pathFile.replace_extension(".path");
    const std::string pathPayload = m_currentScenePath.string();
    AtomicWrite::Status ps = AtomicWrite::writeFile(pathFile, pathPayload);
    if (ps != AtomicWrite::Status::Ok)
    {
        Logger::warning(std::string("Auto-save: .path sidecar ")
                        + AtomicWrite::describe(ps) + " for " + pathFile.string());
    }
}

void FileMenu::deleteAutoSave()
{
    std::error_code ec;
    fs::path autoSavePath = getAutoSavePath();
    if (fs::exists(autoSavePath))
    {
        fs::remove(autoSavePath, ec);
        if (!ec)
        {
            Logger::info("Deleted autosave file");
        }
    }

    // Also remove the sidecar path file
    fs::path pathFile = autoSavePath;
    pathFile.replace_extension(".path");
    if (fs::exists(pathFile))
    {
        fs::remove(pathFile, ec);
    }
}

fs::path FileMenu::getAutoSavePath()
{
    return RecentFiles::getConfigDir() / "autosave.scene";
}

// ---------------------------------------------------------------------------
// Crash recovery dialog
// ---------------------------------------------------------------------------

void FileMenu::drawRecoveryModal(Scene* scene, Selection& selection)
{
    if (m_showRecoveryModal)
    {
        ImGui::OpenPopup("Recover Auto-Save");
        m_showRecoveryModal = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Recover Auto-Save", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize
                                | ImGuiWindowFlags_NoMove))
    {
        fs::path autoSavePath = getAutoSavePath();

        // Read metadata for the timestamp
        SceneMetadata meta = SceneSerializer::readMetadata(autoSavePath);
        std::string timeStr = meta.modified.empty() ? "unknown time" : meta.modified;

        ImGui::Text("An auto-save recovery file was found.");
        ImGui::Text("Last modified: %s", timeStr.c_str());
        ImGui::Spacing();
        ImGui::TextWrapped("This may indicate that the application crashed or was "
                           "terminated unexpectedly. Would you like to recover?");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Recover", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup();

            if (scene && m_resources)
            {
                SceneSerializerResult result = SceneSerializer::loadScene(
                    *scene, autoSavePath, *m_resources, m_foliageManager, m_terrain);

                if (result.success)
                {
                    // Restore the original scene path from sidecar file
                    m_currentScenePath.clear();
                    fs::path pathFile = autoSavePath;
                    pathFile.replace_extension(".path");
                    if (fs::exists(pathFile))
                    {
                        std::ifstream pathIn(pathFile);
                        std::string originalPath;
                        if (std::getline(pathIn, originalPath) && !originalPath.empty())
                        {
                            m_currentScenePath = originalPath;
                        }
                    }

                    // Fallback: match scene name against recent files
                    if (m_currentScenePath.empty())
                    {
                        std::string sceneName = scene->getName();
                        for (const auto& recentPath : m_recentFiles.getPaths())
                        {
                            if (recentPath.stem().string() == sceneName && fs::exists(recentPath))
                            {
                                m_currentScenePath = recentPath;
                                break;
                            }
                        }
                    }

                    selection.clearSelection();
                    if (m_commandHistory)
                    {
                        m_commandHistory->clear();
                        // Phase 10.9 Slice 12 Ed7: scene was wholesale-
                        // replaced by an autosave recovery; sticky-
                        // dirty so user is prompted to save before
                        // quit. Same path as wizard apply.
                        m_commandHistory->markUnsavedChange();
                    }
                    deleteAutoSave();
                    updateWindowTitle(scene->getName());
                    Logger::info("Recovered auto-save ("
                                 + std::to_string(result.entityCount) + " entities)");
                }
                else
                {
                    Logger::error("Failed to recover auto-save: " + result.errorMessage);
                    deleteAutoSave();
                }
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Discard", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup();
            deleteAutoSave();
            Logger::info("Discarded auto-save recovery file");
        }

        ImGui::EndPopup();
    }
}

} // namespace Vestige
