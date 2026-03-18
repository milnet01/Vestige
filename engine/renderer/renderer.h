/// @file renderer.h
/// @brief Core OpenGL rendering system with Blinn-Phong/PBR lighting, shadows, and FBO pipeline.
#pragma once

#include "renderer/shader.h"
#include "renderer/mesh.h"
#include "renderer/camera.h"
#include "renderer/material.h"
#include "renderer/light.h"
#include "renderer/framebuffer.h"
#include "renderer/fullscreen_quad.h"
#include "renderer/shadow_map.h"
#include "renderer/cascaded_shadow_map.h"
#include "renderer/skybox.h"
#include "renderer/point_shadow_map.h"
#include "renderer/taa.h"
#include "renderer/text_renderer.h"
#include "renderer/color_grading_lut.h"
#include "renderer/instance_buffer.h"
#include "renderer/environment_map.h"
#include "core/event_bus.h"
#include "scene/scene.h"

#include <glm/glm.hpp>

#include <memory>
#include <unordered_map>
#include <vector>

#include <glad/gl.h>

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

    /// @brief Sets the exposure multiplier for HDR tone mapping.
    void setExposure(float exposure);

    /// @brief Gets the current exposure multiplier.
    float getExposure() const;

    /// @brief Sets the tonemapper: 0=Reinhard, 1=ACES Filmic, 2=None.
    void setTonemapMode(int mode);

    /// @brief Gets the current tonemapper mode.
    int getTonemapMode() const;

    /// @brief Sets the HDR debug mode: 0=off, 1=false-color luminance.
    void setDebugMode(int mode);

    /// @brief Gets the current HDR debug mode.
    int getDebugMode() const;

    /// @brief Enables or disables parallax occlusion mapping globally.
    void setPomEnabled(bool isEnabled);

    /// @brief Checks if parallax occlusion mapping is globally enabled.
    bool isPomEnabled() const;

    /// @brief Sets the global POM height multiplier (runtime depth adjustment).
    void setPomHeightMultiplier(float multiplier);

    /// @brief Gets the global POM height multiplier.
    float getPomHeightMultiplier() const;

    /// @brief Enables or disables bloom post-processing.
    void setBloomEnabled(bool isEnabled);

    /// @brief Checks if bloom is enabled.
    bool isBloomEnabled() const;

    /// @brief Sets the bloom brightness threshold.
    void setBloomThreshold(float threshold);

    /// @brief Gets the bloom brightness threshold.
    float getBloomThreshold() const;

    /// @brief Sets the bloom intensity multiplier.
    void setBloomIntensity(float intensity);

    /// @brief Gets the bloom intensity multiplier.
    float getBloomIntensity() const;

    /// @brief Enables or disables SSAO.
    void setSsaoEnabled(bool isEnabled);

    /// @brief Checks if SSAO is enabled.
    bool isSsaoEnabled() const;

    /// @brief Sets the anti-aliasing mode (MSAA, TAA, or None).
    void setAntiAliasMode(AntiAliasMode mode);

    /// @brief Gets the current anti-aliasing mode.
    AntiAliasMode getAntiAliasMode() const;

    /// @brief Enables or disables color grading.
    void setColorGradingEnabled(bool enabled);

    /// @brief Checks if color grading is enabled.
    bool isColorGradingEnabled() const;

    /// @brief Cycles to the next color grading LUT preset.
    void nextColorGradingPreset();

    /// @brief Gets the name of the current color grading preset.
    std::string getColorGradingPresetName() const;

    /// @brief Sets the color grading LUT intensity (0.0 to 1.0).
    void setColorGradingIntensity(float intensity);

    /// @brief Gets the color grading LUT intensity.
    float getColorGradingIntensity() const;

    /// @brief Loads an external .cube LUT file as a preset.
    bool loadColorGradingLut(const std::string& filePath, const std::string& name);

    /// @brief Enables or disables the directional light.
    void setDirectionalLightEnabled(bool isEnabled);

    /// @brief Enables or disables cascade debug visualization.
    void setCascadeDebug(bool enabled);

    /// @brief Checks if cascade debug visualization is active.
    bool isCascadeDebug() const;

    /// @brief Renders an entire scene from collected render data.
    void renderScene(const SceneRenderData& renderData, const Camera& camera, float aspectRatio);

    /// @brief Gets the text renderer (nullptr if not initialized).
    TextRenderer* getTextRenderer();

    /// @brief Initializes the text rendering subsystem.
    /// @param fontPath Path to a .ttf font file.
    /// @param assetPath Base path to assets directory.
    /// @return True if initialization succeeded.
    bool initTextRenderer(const std::string& fontPath, const std::string& assetPath);

    /// @brief A batch of instances sharing the same mesh and material.
    struct InstanceBatch
    {
        const Mesh* mesh;
        const Material* material;
        std::vector<glm::mat4> modelMatrices;
    };

    /// @brief Groups render items by (mesh, material) pair for instanced drawing.
    static std::vector<InstanceBatch> buildInstanceBatches(
        const std::vector<SceneRenderData::RenderItem>& items);

private:
    void uploadLightUniforms(const Camera& camera);
    void uploadMaterialUniforms(const Material& material);
    void renderShadowPass(const std::vector<InstanceBatch>& batches,
                          const Camera& camera, float aspectRatio);
    void renderPointShadowPass(const std::vector<InstanceBatch>& batches,
                               const std::vector<int>& shadowCasters);
    std::vector<int> selectShadowCastingPointLights() const;
    void onWindowResize(int width, int height);

    Shader m_sceneShader;
    Shader m_screenShader;
    Shader m_shadowDepthShader;
    Shader m_skyboxShader;
    Shader m_pointShadowDepthShader;
    Shader m_bloomBrightShader;
    Shader m_bloomBlurShader;
    Shader m_ssaoShader;
    Shader m_ssaoBlurShader;
    Shader m_taaResolveShader;
    Shader m_motionVectorShader;
    EventBus& m_eventBus;
    std::string m_assetPath;

    // Framebuffer pipeline
    std::unique_ptr<Framebuffer> m_msaaFbo;
    std::unique_ptr<Framebuffer> m_resolveFbo;
    std::unique_ptr<FullscreenQuad> m_screenQuad;
    int m_windowWidth = 0;
    int m_windowHeight = 0;
    int m_msaaSamples = 4;

    // Shadow mapping (cascaded)
    std::unique_ptr<CascadedShadowMap> m_cascadedShadowMap;
    bool m_cascadeDebug = false;

    // Point light shadows
    std::vector<std::unique_ptr<PointShadowMap>> m_pointShadowMaps;

    // Skybox
    std::unique_ptr<Skybox> m_skybox;

    // Text rendering
    std::unique_ptr<TextRenderer> m_textRenderer;

    // Lighting
    DirectionalLight m_directionalLight;
    std::vector<PointLight> m_pointLights;
    std::vector<SpotLight> m_spotLights;
    bool m_hasDirectionalLight;
    bool m_isWireframe;

    // HDR / Tone mapping
    float m_exposure = 1.0f;
    int m_tonemapMode = 1;    // Default to ACES Filmic
    int m_debugMode = 0;      // 0 = off

    // Anti-aliasing mode
    AntiAliasMode m_antiAliasMode = AntiAliasMode::MSAA_4X;

    // TAA
    std::unique_ptr<Taa> m_taa;
    std::unique_ptr<Framebuffer> m_taaSceneFbo;  // Non-MSAA scene FBO for TAA mode
    glm::mat4 m_prevViewProjection = glm::mat4(1.0f);
    glm::mat4 m_lastViewProjection = glm::mat4(1.0f);

    // Parallax occlusion mapping
    bool m_pomEnabled = true;
    float m_pomHeightMultiplier = 1.0f;

    // Bloom post-processing
    std::unique_ptr<Framebuffer> m_bloomBrightFbo;
    std::unique_ptr<Framebuffer> m_bloomPingFbo;
    std::unique_ptr<Framebuffer> m_bloomPongFbo;
    bool m_bloomEnabled = true;
    float m_bloomThreshold = 1.0f;
    float m_bloomIntensity = 1.0f;
    int m_bloomIterations = 5;

    // Instanced rendering
    std::unique_ptr<InstanceBuffer> m_instanceBuffer;
    static constexpr int MIN_INSTANCE_BATCH_SIZE = 2;

    // Color grading LUT
    std::unique_ptr<ColorGradingLut> m_colorGradingLut;

    // IBL (Image-Based Lighting)
    std::unique_ptr<EnvironmentMap> m_environmentMap;

    // Reusable per-frame vectors (avoid allocation every frame)
    std::vector<SceneRenderData::RenderItem> m_culledItems;
    std::vector<SceneRenderData::RenderItem> m_sortedTransparentItems;

    // SSAO
    std::unique_ptr<Framebuffer> m_resolveDepthFbo;
    std::unique_ptr<Framebuffer> m_ssaoFbo;
    std::unique_ptr<Framebuffer> m_ssaoBlurFbo;
    GLuint m_ssaoNoiseTexture = 0;
    std::vector<glm::vec3> m_ssaoKernel;
    bool m_ssaoEnabled = true;
    float m_ssaoRadius = 0.5f;
    float m_ssaoBias = 0.025f;
    glm::mat4 m_lastProjection = glm::mat4(1.0f);

    void generateSsaoKernel();
    void generateSsaoNoiseTexture();
};

} // namespace Vestige
