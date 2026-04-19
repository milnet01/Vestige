// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file import_dialog.cpp
/// @brief ImportDialog implementation — file browser + import settings modal.
#include "editor/panels/import_dialog.h"
#include "editor/editor_camera.h"
#include "editor/selection.h"
#include "core/logger.h"
#include "resource/resource_manager.h"
#include "resource/model.h"
#include "scene/entity.h"
#include "scene/mesh_renderer.h"
#include "scene/scene.h"

#include <imgui.h>

#include <cctype>

namespace Vestige
{

ImportDialog::ImportDialog()
    : m_fileBrowser(ImGuiFileBrowserFlags_CloseOnEsc)
{
    m_fileBrowser.SetTitle("Import Model");
    m_fileBrowser.SetTypeFilters({".gltf", ".glb", ".obj"});
}

void ImportDialog::open()
{
    m_fileBrowser.Open();
}

void ImportDialog::draw(Scene* scene, ResourceManager* resources, Selection& selection,
                        EditorCamera* editorCamera)
{
    // Always display the file browser (no-op when closed)
    m_fileBrowser.Display();

    if (m_fileBrowser.HasSelected())
    {
        m_selectedPath = m_fileBrowser.GetSelected();
        m_fileBrowser.ClearSelected();
        m_detectedFormat = detectFormat(m_selectedPath);
        m_importScale = 1.0f;
        m_showImportSettings = true;
        analyzeFile();
    }

    // Open the import settings popup (must call OpenPopup before BeginPopupModal)
    if (m_showImportSettings)
    {
        ImGui::OpenPopup("Import Settings");
        m_showImportSettings = false;
    }

    if (ImGui::BeginPopupModal("Import Settings", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        // File name
        ImGui::Text("File:");
        ImGui::SameLine();
        std::string filename = m_selectedPath.filename().string();
        ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "%s", filename.c_str());

        // Full path (dimmed)
        ImGui::TextDisabled("%s", m_selectedPath.string().c_str());

        ImGui::Separator();

        // Format indicator
        ImGui::Text("Format:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "%s",
                           m_detectedFormat.c_str());

        // File analysis info
        if (m_hasAnalysis)
        {
            if (m_meshCount > 0)
            {
                ImGui::Text("Meshes: %d", m_meshCount);
            }
            if (m_triangleCount > 0)
            {
                ImGui::Text("Triangles: %d", m_triangleCount);
            }
            if (m_materialCount > 0)
            {
                ImGui::Text("Materials: %d", m_materialCount);
            }
            if (m_boundsSize.x > 0.0f || m_boundsSize.y > 0.0f || m_boundsSize.z > 0.0f)
            {
                ImGui::Text("Bounds: %.2f x %.2f x %.2f m",
                            static_cast<double>(m_boundsSize.x),
                            static_cast<double>(m_boundsSize.y),
                            static_cast<double>(m_boundsSize.z));
            }
            ImGui::Separator();
        }

        // Scale slider (logarithmic for wide range)
        ImGui::SliderFloat("Import Scale", &m_importScale, 0.001f, 100.0f, "%.3f",
                           ImGuiSliderFlags_Logarithmic);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Scale factor for models not in metric units");
        }

        // Scale validation warning
        m_scaleWarning.clear();
        if (m_hasAnalysis && m_boundsSize.x > 0.0f)
        {
            float maxDim = std::max({m_boundsSize.x, m_boundsSize.y, m_boundsSize.z})
                           * m_importScale;
            if (maxDim < 0.01f)
            {
                m_scaleWarning = "Warning: model will be very small (< 1cm)";
            }
            else if (maxDim > 1000.0f)
            {
                m_scaleWarning = "Warning: model will be very large (> 1km)";
            }
        }
        if (!m_scaleWarning.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%s", m_scaleWarning.c_str());
        }

        ImGui::Separator();

        // Centered Import / Cancel buttons
        float buttonWidth = 120.0f;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float totalWidth = buttonWidth * 2.0f + spacing;
        float cursorX = ImGui::GetCursorPosX();
        float availWidth = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(cursorX + (availWidth - totalWidth) * 0.5f);

        bool canImport = scene && resources;
        if (!canImport)
        {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Import", ImVec2(buttonWidth, 0)))
        {
            glm::vec3 spawnPos = editorCamera
                ? editorCamera->getFocusPoint() : glm::vec3(0.0f);
            performImport(*scene, *resources, selection, spawnPos);
            ImGui::CloseCurrentPopup();
        }
        if (!canImport)
        {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0)))
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

std::string ImportDialog::detectFormat(const std::filesystem::path& filePath)
{
    std::string ext = filePath.extension().string();
    for (char& c : ext)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (ext == ".gltf")
    {
        return "glTF 2.0 (text)";
    }
    if (ext == ".glb")
    {
        return "glTF 2.0 (binary)";
    }
    if (ext == ".obj")
    {
        return "Wavefront OBJ";
    }
    return "Unknown";
}

void ImportDialog::performImport(Scene& scene, ResourceManager& resources,
                                 Selection& selection, const glm::vec3& spawnPos)
{
    std::string filePath = m_selectedPath.string();
    std::string ext = m_selectedPath.extension().string();
    for (char& c : ext)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    std::string modelName = m_selectedPath.stem().string();

    if (ext == ".gltf" || ext == ".glb")
    {
        // Load via glTF loader → Model → instantiate into scene
        auto model = resources.loadModel(filePath);
        if (!model)
        {
            Logger::error("Import failed: could not load " + filePath);
            return;
        }

        Entity* root = model->instantiate(scene, nullptr, modelName);
        if (root)
        {
            root->transform.position = spawnPos;
            root->transform.scale = glm::vec3(m_importScale);
            selection.select(root->getId());
            Logger::info("Imported glTF: " + modelName
                + " (" + std::to_string(model->getMeshCount()) + " meshes, "
                + std::to_string(model->getMaterialCount()) + " materials)");
        }
    }
    else if (ext == ".obj")
    {
        // Load OBJ mesh → create entity with MeshRenderer + default material
        auto mesh = resources.loadMesh(filePath);
        if (!mesh)
        {
            Logger::error("Import failed: could not load " + filePath);
            return;
        }

        Entity* entity = scene.createEntity(modelName);
        entity->transform.position = spawnPos;
        entity->transform.scale = glm::vec3(m_importScale);

        // Default grey PBR material for OBJ imports
        auto material = resources.createMaterial("__import_obj_" + modelName);
        material->setAlbedo(glm::vec3(0.8f, 0.8f, 0.8f));
        material->setRoughness(0.5f);
        material->setMetallic(0.0f);

        entity->addComponent<MeshRenderer>(mesh, material);
        selection.select(entity->getId());
        Logger::info("Imported OBJ: " + modelName);
    }
    else
    {
        Logger::error("Import failed: unsupported format '" + ext + "'");
    }
}

void ImportDialog::analyzeFile()
{
    m_hasAnalysis = false;
    m_triangleCount = 0;
    m_materialCount = 0;
    m_meshCount = 0;
    m_boundsSize = glm::vec3(0.0f);
    m_scaleWarning.clear();

    std::string ext = m_selectedPath.extension().string();
    for (char& c : ext)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    // For glTF files, we can get metadata without fully loading the model.
    // For now, report basic file info from a lightweight parse.
    // The actual mesh/material counts will be reported after import.
    if (ext == ".gltf" || ext == ".glb" || ext == ".obj")
    {
        // Check file size as a rough metric
        std::error_code ec;
        auto fileSize = std::filesystem::file_size(m_selectedPath, ec);
        if (!ec)
        {
            // Rough estimate: ~50 bytes per triangle for glTF binary
            if (ext == ".glb")
            {
                m_triangleCount = static_cast<int>(fileSize / 50);
            }
            else if (ext == ".obj")
            {
                // OBJ: ~80 bytes per triangle (text format)
                m_triangleCount = static_cast<int>(fileSize / 80);
            }
            else
            {
                // glTF text: ~100 bytes per triangle
                m_triangleCount = static_cast<int>(fileSize / 100);
            }

            m_hasAnalysis = true;
        }
    }
}

} // namespace Vestige
