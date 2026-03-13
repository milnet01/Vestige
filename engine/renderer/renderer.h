/// @file renderer.h
/// @brief Core OpenGL rendering system with Blinn-Phong lighting.
#pragma once

#include "renderer/shader.h"
#include "renderer/mesh.h"
#include "renderer/camera.h"
#include "renderer/material.h"
#include "renderer/light.h"
#include "core/event_bus.h"

#include <glm/glm.hpp>

#include <vector>

namespace Vestige
{

struct SceneRenderData;

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

    /// @brief Loads all shader programs.
    /// @param assetPath Base path to the assets directory.
    /// @return True if all shaders loaded successfully.
    bool loadShaders(const std::string& assetPath);

    /// @brief Clears the screen to prepare for a new frame.
    void beginFrame();

    /// @brief Renders a mesh with material and lighting.
    /// @param mesh The mesh to render.
    /// @param modelMatrix The model's world transform.
    /// @param material The surface material.
    /// @param camera The camera providing view/projection matrices.
    /// @param aspectRatio Window width / height.
    void drawMesh(const Mesh& mesh, const glm::mat4& modelMatrix,
                  const Material& material, const Camera& camera,
                  float aspectRatio);

    /// @brief Sets the clear color (background color).
    /// @param color RGB color values (0.0 to 1.0).
    void setClearColor(const glm::vec3& color);

    /// @brief Sets the directional light.
    void setDirectionalLight(const DirectionalLight& light);

    /// @brief Clears all point lights.
    void clearPointLights();

    /// @brief Adds a point light to the scene.
    /// @return True if added (false if at MAX_POINT_LIGHTS).
    bool addPointLight(const PointLight& light);

    /// @brief Clears all spot lights.
    void clearSpotLights();

    /// @brief Adds a spot light to the scene.
    /// @return True if added (false if at MAX_SPOT_LIGHTS).
    bool addSpotLight(const SpotLight& light);

    /// @brief Enables or disables wireframe rendering mode.
    void setWireframeMode(bool isEnabled);

    /// @brief Checks if wireframe mode is active.
    bool isWireframeMode() const;

    /// @brief Enables or disables the directional light.
    void setDirectionalLightEnabled(bool isEnabled);

    /// @brief Renders an entire scene from collected render data.
    /// @param renderData The scene's collected render data.
    /// @param camera The camera providing view/projection matrices.
    /// @param aspectRatio Window width / height.
    void renderScene(const SceneRenderData& renderData, const Camera& camera, float aspectRatio);

private:
    void uploadLightUniforms(const Camera& camera);

    Shader m_blinnPhongShader;
    EventBus& m_eventBus;

    DirectionalLight m_directionalLight;
    std::vector<PointLight> m_pointLights;
    std::vector<SpotLight> m_spotLights;
    bool m_hasDirectionalLight;
    bool m_isWireframe;
};

} // namespace Vestige
