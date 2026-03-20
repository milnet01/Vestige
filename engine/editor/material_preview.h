/// @file material_preview.h
/// @brief Renders a small material preview sphere to an offscreen FBO.
#pragma once

#include "renderer/framebuffer.h"
#include "renderer/mesh.h"
#include "renderer/shader.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <memory>
#include <string>

namespace Vestige
{

class Material;
class Renderer;
class Texture;

/// @brief Renders a preview sphere with a given material into a small FBO.
///
/// The preview uses a fixed camera and single directional light.
/// It only re-renders when markDirty() is called (e.g. when material properties change).
class MaterialPreview
{
public:
    /// @brief Initializes the preview FBO, sphere mesh, and camera matrices.
    /// @param assetPath Base path for loading shaders.
    /// @param resolution Preview texture size in pixels (square).
    /// @return True if initialization succeeded.
    bool initialize(const std::string& assetPath, int resolution = 128);

    /// @brief Renders the material preview if dirty.
    /// @param material The material to preview.
    void render(const Material& material);

    /// @brief Gets the preview texture ID for ImGui::Image().
    GLuint getTextureId() const;

    /// @brief Gets the preview resolution.
    int getResolution() const { return m_resolution; }

    /// @brief Marks the preview as needing a re-render.
    void markDirty() { m_dirty = true; }

    /// @brief Returns true if the preview needs re-rendering.
    bool isDirty() const { return m_dirty; }

    /// @brief Releases GPU resources.
    void cleanup();

private:
    std::unique_ptr<Framebuffer> m_fbo;
    Mesh m_sphere;
    Shader m_previewShader;
    std::shared_ptr<Texture> m_defaultTexture;
    int m_resolution = 128;
    bool m_dirty = true;
    bool m_initialized = false;

    // Fixed camera matrices for the preview
    glm::mat4 m_viewMatrix = glm::mat4(1.0f);
    glm::mat4 m_projMatrix = glm::mat4(1.0f);
};

} // namespace Vestige
