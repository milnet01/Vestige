/// @file renderer.h
/// @brief Core OpenGL rendering system.
#pragma once

#include "renderer/shader.h"
#include "renderer/mesh.h"
#include "renderer/camera.h"
#include "core/event_bus.h"

#include <glm/glm.hpp>

namespace Vestige
{

/// @brief Manages OpenGL rendering state and draw operations.
class Renderer
{
public:
    /// @brief Creates the renderer and initializes OpenGL state.
    /// @param eventBus Event bus for subscribing to window events.
    explicit Renderer(EventBus& eventBus);
    ~Renderer();

    // Non-copyable
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    /// @brief Clears the screen to prepare for a new frame.
    void beginFrame();

    /// @brief Renders a mesh with the given transform and camera.
    /// @param mesh The mesh to render.
    /// @param modelMatrix The model's world transform.
    /// @param camera The camera providing view/projection matrices.
    /// @param aspectRatio Window width / height.
    void drawMesh(const Mesh& mesh, const glm::mat4& modelMatrix,
                  const Camera& camera, float aspectRatio);

    /// @brief Sets the clear color (background color).
    /// @param color RGB color values (0.0 to 1.0).
    void setClearColor(const glm::vec3& color);

    /// @brief Gets a reference to the basic shader.
    Shader& getBasicShader();

    /// @brief Loads the default shaders.
    /// @param assetPath Base path to the assets directory.
    /// @return True if shaders loaded successfully.
    bool loadShaders(const std::string& assetPath);

private:
    Shader m_basicShader;
    EventBus& m_eventBus;
};

} // namespace Vestige
