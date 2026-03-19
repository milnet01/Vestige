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
    /// @param deltaTime Time elapsed since last frame (for auto-exposure smoothing).
    void endFrame(float deltaTime = 1.0f / 60.0f);

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
    /// When auto-exposure is on, this is overridden each frame.
    void setExposure(float exposure);

    /// @brief Gets the current exposure multiplier.
    float getExposure() const;

    /// @brief Enables or disables automatic exposure adaptation.
    void setAutoExposure(bool enabled);

    /// @brief Checks if auto-exposure is enabled.
    bool isAutoExposure() const;

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

    /// @brief Per-frame rendering statistics.
    struct CullingStats
    {
        int totalItems = 0;           // Total opaque items before culling
        int culledItems = 0;          // Items remaining after frustum cull
        int transparentTotal = 0;     // Total transparent items before culling
        int transparentCulled = 0;    // Transparent items remaining after cull
        int shadowCastersTotal = 0;   // Total shadow casters before culling
        int shadowCastersCulled = 0;  // Shadow casters per cascade (avg)
        int drawCalls = 0;            // Total draw calls this frame
        int instanceBatches = 0;      // Draw calls that used instancing
    };

    /// @brief Gets the most recent frame's culling statistics.
    const CullingStats& getCullingStats() const;

    /// @brief Gets the number of active point lights.
    int getPointLightCount() const;

    /// @brief Gets the number of active spot lights.
    int getSpotLightCount() const;

    /// @brief Gets the OpenGL texture ID of the final composited (post-tonemapped) frame.
    /// Used by the editor viewport panel to display the scene in ImGui.
    GLuint getOutputTextureId() const;

    /// @brief Blits the final output to the default framebuffer (screen).
    /// Call this in play mode when ImGui is not displaying the viewport.
    void blitToScreen();

    /// @brief Renders the scene to the ID buffer for mouse picking.
    /// Call on-demand (when the user clicks in the viewport).
    void renderIdBuffer(const SceneRenderData& renderData,
                        const Camera& camera, float aspectRatio);

    /// @brief Reads the ID buffer pixel at (x, y) and decodes the entity ID.
    /// @param x Pixel X in FBO coordinates (0 = left).
    /// @param y Pixel Y in FBO coordinates (0 = bottom).
    /// @return Entity ID, or 0 if background/empty.
    uint32_t pickEntityAt(int x, int y);

    /// @brief Renders selection outlines into the output FBO for the given entities.
    void renderSelectionOutline(const SceneRenderData& renderData,
                                const std::vector<uint32_t>& selectedIds,
                                const Camera& camera, float aspectRatio);

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
    void renderShadowPass(const std::vector<SceneRenderData::RenderItem>& shadowCasterItems,
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
    Shader m_bloomDownsampleShader;
    Shader m_bloomUpsampleShader;
    Shader m_ssaoShader;
    Shader m_ssaoBlurShader;
    Shader m_taaResolveShader;
    Shader m_motionVectorShader;
    Shader m_idBufferShader;
    Shader m_outlineShader;
    EventBus& m_eventBus;
    std::string m_assetPath;

    // Framebuffer pipeline
    std::unique_ptr<Framebuffer> m_msaaFbo;
    std::unique_ptr<Framebuffer> m_resolveFbo;
    std::unique_ptr<Framebuffer> m_outputFbo;  // Post-tonemapped LDR output for editor viewport
    GLuint m_outlineStencilRbo = 0;            // Depth-stencil RBO attached to output FBO for outline rendering

    // ID buffer (for mouse picking — rendered on demand)
    std::unique_ptr<Framebuffer> m_idBufferFbo;
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
    float m_targetExposure = 1.0f;
    bool m_autoExposure = true;
    float m_autoExposureSpeed = 2.0f;       // Adaptation speed (higher = faster)
    float m_autoExposureMin = 0.2f;         // Minimum auto-exposure value
    float m_autoExposureMax = 8.0f;         // Maximum auto-exposure value
    float m_autoExposureTarget = 0.25f;     // Target average luminance (mid-grey)
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

    // Bloom post-processing (mip-chain approach)
    static constexpr int BLOOM_MIP_COUNT = 6;
    GLuint m_bloomTexture = 0;         // Single texture with mip levels
    GLuint m_bloomFbo = 0;             // FBO reused for each mip level
    // Auto-exposure luminance (separate from bloom — reads unthresholded scene)
    GLuint m_luminanceTexture = 0;     // Small texture with mipmaps for averaging
    GLuint m_luminancePbo[2] = {0, 0}; // Double-buffered PBO for async GPU→CPU readback
    int m_pboWriteIndex = 0;           // Which PBO to write into this frame
    bool m_pboReady = false;           // True after first frame's PBO has been filled
    int m_bloomMipWidths[BLOOM_MIP_COUNT] = {};
    int m_bloomMipHeights[BLOOM_MIP_COUNT] = {};
    bool m_bloomEnabled = true;
    float m_bloomThreshold = 1.0f;
    float m_bloomIntensity = 0.04f;
    float m_bloomFilterRadius = 1.0f;

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
    glm::mat4 m_lastView = glm::mat4(1.0f);

    // Screen-space reflections (disabled until G-buffer in Phase 5)
    Shader m_ssrShader;
    std::unique_ptr<Framebuffer> m_ssrFbo;
    bool m_ssrEnabled = false;  // Disabled: needs G-buffer for per-pixel roughness
    float m_ssrMaxDistance = 5.0f;
    float m_ssrThickness = 0.3f;
    int m_ssrMaxSteps = 32;

    // Screen-space contact shadows
    Shader m_contactShadowShader;
    std::unique_ptr<Framebuffer> m_contactShadowFbo;
    bool m_contactShadowsEnabled = false;  // Disabled: needs G-buffer normals for correct results
    float m_contactShadowLength = 0.5f;   // Max ray length in view space
    int m_contactShadowSteps = 16;

    void generateSsaoKernel();
    void generateSsaoNoiseTexture();

    // Frustum culling statistics (updated each frame in renderScene)
    CullingStats m_cullingStats;
};

} // namespace Vestige
