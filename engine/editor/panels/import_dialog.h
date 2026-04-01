/// @file import_dialog.h
/// @brief Model import dialog — file browser with scale control and format detection.
#pragma once

#include <imgui.h>
#include <imfilebrowser.h>

#include <glm/glm.hpp>

#include <filesystem>
#include <string>

namespace Vestige
{

class EditorCamera;
class ResourceManager;
class Scene;
class Selection;

/// @brief Modal dialog for importing 3D models (glTF/GLB/OBJ) into the scene.
class ImportDialog
{
public:
    ImportDialog();

    /// @brief Opens the file browser dialog.
    void open();

    /// @brief Draws the dialog each frame. Call inside the ImGui frame.
    /// @param scene Scene to import into.
    /// @param resources ResourceManager for loading models/meshes.
    /// @param selection Selection to auto-select the imported entity.
    /// @param editorCamera Camera for determining spawn position.
    void draw(Scene* scene, ResourceManager* resources, Selection& selection,
              EditorCamera* editorCamera);

private:
    /// @brief Detects the format string from a file extension.
    static std::string detectFormat(const std::filesystem::path& filePath);

    /// @brief Performs the actual model import.
    void performImport(Scene& scene, ResourceManager& resources, Selection& selection,
                       const glm::vec3& spawnPos);

    /// @brief Analyzes the selected file and populates metadata fields.
    void analyzeFile();

    ImGui::FileBrowser m_fileBrowser;
    bool m_showImportSettings = false;
    std::filesystem::path m_selectedPath;
    std::string m_detectedFormat;
    float m_importScale = 1.0f;

    // File analysis metadata
    int m_triangleCount = 0;
    int m_materialCount = 0;
    int m_meshCount = 0;
    glm::vec3 m_boundsSize = glm::vec3(0.0f);
    bool m_hasAnalysis = false;
    std::string m_scaleWarning;
};

} // namespace Vestige
