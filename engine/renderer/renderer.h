/// @file renderer.h
/// @brief Core OpenGL rendering system with Blinn-Phong lighting, shadows, and FBO pipeline.
#pragma once

#include "renderer/shader.h"
#include "renderer/mesh.h"
#include "renderer/camera.h"
#include "renderer/material.h"
#include "renderer/light.h"
#include "renderer/framebuffer.h"
#include "renderer/fullscreen_quad.h"
#include "renderer/shadow_map.h"
#include "core/event_bus.h"

#include <glm/glm.hpp>

#include <memory>
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

    /// @brief Initializes the framebuffer pipeline (MSAA + resolve + shadow FBOs).
    /// @param width Initial framebuffer width.
    /// @param height Initial framebuffer height.
    /// @param msaaSamples MSAA sample count (1 = off, 4 = 4x MSAA).
    void initFramebuffers(int width, int height, int msaaSamples = 4);

    /// @brief Binds the scene framebuffer and clears for a new frame.
    void beginFrame();

    /// @brief Resolves MSAA and draws the final image to the screen.
    void endFrame();

    /// @brief Renders a mesh with material and lighting.
    void drawMesh(const Mesh& mesh, const glm::mat4& modelMatrix,
                  const Material& material, const Camera& camera,
                  float aspectRatio);

    /// @brief Sets the clear color (background color).
    void setClearColor(const glm::vec3& color);

    /// @brief Sets the directional light.
    void setDirectionalLight(const DirectionalLight& light);

    /// @brief Clears all point lights.
    void clearPointLights();

    /// @brief Adds a point light to the scene.
    bool addPointLight(const PointLight& light);

    /// @brief Clears all spot lights.
    void clearSpotLights();

    /// @brief Adds a spot light to the scene.
    bool addSpotLight(const SpotLight& light);

    /// @brief Enables or disables wireframe rendering mode.
    void setWireframeMode(bool isEnabled);

    /// @brief Checks if wireframe mode is active.
    bool isWireframeMode() const;

    /// @brief Enables or disables the directional light.
    void setDirectionalLightEnabled(bool isEnabled);

    /// @brief Renders an entire scene from collected render data.
    void renderScene(const SceneRenderData& renderData, const Camera& camera, float aspectRatio);

private:
    void uploadLightUniforms(const Camera& camera);
    void renderShadowPass(const SceneRenderData& renderData);
    void onWindowResize(int width, int height);

    Shader m_blinnPhongShader;
    Shader m_screenShader;
    Shader m_shadowDepthShader;
    EventBus& m_eventBus;

    // Framebuffer pipeline
    std::unique_ptr<Framebuffer> m_msaaFbo;
    std::unique_ptr<Framebuffer> m_resolveFbo;
    std::unique_ptr<FullscreenQuad> m_screenQuad;
    int m_windowWidth = 0;
    int m_windowHeight = 0;
    int m_msaaSamples = 4;

    // Shadow mapping
    std::unique_ptr<ShadowMap> m_shadowMap;

    // Lighting
    DirectionalLight m_directionalLight;
    std::vector<PointLight> m_pointLights;
    std::vector<SpotLight> m_spotLights;
    bool m_hasDirectionalLight;
    bool m_isWireframe;
};

} // namespace Vestige
