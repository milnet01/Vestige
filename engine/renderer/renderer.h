// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

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
#include "renderer/color_vision_filter.h"
#include "accessibility/photosensitive_safety.h"
#include "renderer/fog.h"
#include "renderer/instance_buffer.h"
#include "renderer/light_probe_manager.h"
#include "renderer/sh_probe_grid.h"
#include "renderer/environment_map.h"
#include "renderer/depth_reducer.h"
#include "renderer/smaa.h"
#include "renderer/mesh_pool.h"
#include "renderer/indirect_buffer.h"
#include "core/event_bus.h"
#include "scene/scene.h"

#include <glm/glm.hpp>

#include <memory>
#include <memory_resource>
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
    /// @param boneMatrices Optional bone matrices for skeletal animation (nullptr for static).
    /// @param morphWeights Optional morph target weights (nullptr if no morphs).
    /// @param morphWeightCount Number of morph weights.
    void drawMesh(const Mesh& mesh, const glm::mat4& modelMatrix,
                  const Material& material, const Camera& camera,
                  float aspectRatio,
                  const std::vector<glm::mat4>* boneMatrices = nullptr,
                  const float* morphWeights = nullptr, int morphWeightCount = 0);

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

    /// @brief Enables or disables the skybox (disable for indoor scenes).
    void setSkyboxEnabled(bool enabled) { m_skyboxEnabled = enabled; }

    /// @brief Loads an equirectangular HDRI as the skybox and regenerates the IBL environment map.
    /// @param path File path to equirectangular image (.hdr, .jpg, .png, etc.).
    /// @return True if loaded and IBL regenerated successfully.
    bool loadSkyboxHDRI(const std::string& path);

    /// @brief Gets the light probe manager for adding/configuring probes.
    LightProbeManager* getLightProbeManager() const { return m_lightProbeManager.get(); }

    /// @brief Gets the SH probe grid for configuration and capture.
    SHProbeGrid* getSHProbeGrid() const { return m_shProbeGrid.get(); }

    /// @brief Sets the normal bias for SH grid sampling (anti-leak, in meters).
    void setSHNormalBias(float bias) { m_shNormalBias = bias; }

    /// @brief Captures the SH probe grid by rendering cubemaps at each probe position.
    /// Call after scene geometry and lights are placed.
    /// @param faceSize Cubemap face resolution (default 64; use 16 for fast radiosity bounces).
    void captureSHGrid(const SceneRenderData& renderData,
                       const Camera& camera, float aspectRatio,
                       int faceSize = 64);

    /// @brief Captures a light probe by rendering the scene from the probe's position.
    /// @param probeIndex Index of the probe in the LightProbeManager.
    /// @param renderData Scene geometry and lights to render.
    /// @param camera The main camera (for shadow computation reference).
    /// @param aspectRatio Camera aspect ratio.
    void captureLightProbe(int probeIndex, const SceneRenderData& renderData,
                           const Camera& camera, float aspectRatio);

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

    /// @brief Sets the color-vision-deficiency simulation mode
    ///        (accessibility). Applied post-grade, pre-gamma in
    ///        screen_quad.frag.
    void setColorVisionMode(ColorVisionMode mode);

    /// @brief Gets the current color-vision-deficiency simulation mode.
    ColorVisionMode getColorVisionMode() const;

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

    /// @brief Diagnostic toggle for the per-object motion vector overlay
    ///        added by AUDIT.md §H15 / commit a122e85. When false the TAA
    ///        path uses only the camera-only full-screen motion pass —
    ///        ghosting on dynamic geometry returns, but rules the overlay
    ///        out as a regression source. No production reason to disable.
    void setObjectMotionOverlayEnabled(bool isEnabled);
    bool isObjectMotionOverlayEnabled() const;

    /// @brief Diagnostic global override for u_iblMultiplier sent to the
    ///        scene shader. -1 (default) means use each material's own
    ///        getIblMultiplier(); >= 0 forces every draw to use this value.
    ///        Set to 0 to rule IBL specular/diffuse out as the regression
    ///        source (BRDF LUT change in commit c1b641a). Negative values
    ///        restore per-material behaviour.
    void setIblMultiplierOverride(float multiplier);
    float getIblMultiplierOverride() const;

    /// @brief Diagnostic IBL split scales — independently zero out the
    ///        diffuse-irradiance or specular-prefilter contribution to
    ///        further bisect which IBL sub-component blew up. Sets the
    ///        u_iblDiffuseScale / u_iblSpecularScale shader uniforms
    ///        once on the scene program (uniforms persist).
    void setIblSubScales(float diffuseScale, float specularScale);

    /// @brief Diagnostic: when true, force u_hasSHGrid=false in the scene
    ///        shader regardless of whether a baked SH grid is ready.
    ///        Falls the IBL-diffuse path back to cubemap probe / sky
    ///        irradiance, which was the only path active before the
    ///        Tabernacle radiosity bake landed.
    void setShGridForceDisabled(bool isDisabled);
    bool isShGridForceDisabled() const;

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

    /// @brief Sets the distance-fog curve. Pass `FogMode::None` to disable.
    ///        Applied in linear HDR between contact shadows and bloom.
    void setFogMode(FogMode mode);

    /// @brief Gets the current distance-fog curve.
    FogMode getFogMode() const;

    /// @brief Sets distance-fog parameters (colour, start/end, density).
    void setFogParams(const FogParams& params);

    /// @brief Gets the current distance-fog parameters.
    const FogParams& getFogParams() const;

    /// @brief Enables or disables the exponential height-fog layer
    ///        (Quílez analytic integral). Composes multiplicatively
    ///        with distance fog.
    void setHeightFogEnabled(bool enabled);
    bool isHeightFogEnabled() const;

    /// @brief Sets height-fog parameters (ground density, falloff, etc.).
    void setHeightFogParams(const HeightFogParams& params);
    const HeightFogParams& getHeightFogParams() const;

    /// @brief Enables or disables the sun-direction inscatter lobe.
    void setSunInscatterEnabled(bool enabled);
    bool isSunInscatterEnabled() const;

    /// @brief Sets sun-inscatter parameters (colour, exponent, start distance).
    void setSunInscatterParams(const SunInscatterParams& params);
    const SunInscatterParams& getSunInscatterParams() const;

    /// @brief Sets the post-process accessibility settings that gate
    ///        fog visibility (master toggle, intensity scale, and
    ///        reduce-motion for the sun-inscatter lobe).
    ///
    /// The authored fog parameters above stay untouched; the transform
    /// in `fog.cpp` (`applyFogAccessibilitySettings`) runs each frame
    /// between the stored state and the GPU uniform upload so users
    /// can toggle accessibility without losing their scene-authored
    /// look. See `docs/phases/phase_10_fog_design.md` §6.
    void setPostProcessAccessibility(const PostProcessAccessibilitySettings& settings);
    const PostProcessAccessibilitySettings& getPostProcessAccessibility() const;

    /// @brief Phase 10.7 slice C1 — pushes the engine's photosensitive
    ///        safe-mode state into the renderer so the bloom composite
    ///        can clamp its uploaded intensity at draw time.
    ///
    /// Called once per frame from `Engine::run()` so a mid-session
    /// Settings toggle takes effect on the next drawn frame. Passing
    /// `enabled = false` (default) means "no clamp" — the renderer
    /// uploads `m_bloomIntensity` unchanged.
    void setPhotosensitive(bool enabled,
                           const PhotosensitiveLimits& limits);

    /// @brief Enables or disables SDSM (Sample Distribution Shadow Maps).
    void setSdsmEnabled(bool enabled);

    /// @brief Checks if SDSM is enabled.
    bool isSdsmEnabled() const;

    /// @brief Renders an entire scene from collected render data.
    /// @param clipPlane Optional clip plane for water reflection/refraction (0,0,0,0 = disabled).
    /// @param geometryOnly When true, skips shadow passes and FBO rebinding (caller manages FBO).
    ///        Used for water reflection/refraction passes that reuse existing shadow maps.
    /// @param viewOverride Optional view matrix override (for cubemap face rendering).
    /// @param projOverride Optional projection matrix override (for cubemap face rendering).
    void renderScene(const SceneRenderData& renderData, const Camera& camera, float aspectRatio,
                     const glm::vec4& clipPlane = glm::vec4(0.0f),
                     bool geometryOnly = false,
                     const glm::mat4& viewOverride = glm::mat4(0.0f),
                     const glm::mat4& projOverride = glm::mat4(0.0f));

    /// @brief Re-binds the active scene FBO and restores viewport.
    /// Call after rendering to an external FBO (e.g., water reflection/refraction).
    void rebindSceneFbo();

    /// @brief Saves view/projection state that geometryOnly renderScene calls overwrite.
    void saveViewState();

    /// @brief Restores view/projection state saved by saveViewState().
    void restoreViewState();

    /// @brief Sets caustics parameters for underwater geometry.
    /// Call each frame when water surfaces exist. Caustics are disabled when not called.
    /// @param enabled Whether caustics are active this frame.
    /// @param waterY World-space Y of the water surface.
    /// @param time Elapsed time for animation.
    /// @param center XZ center of the water surface.
    /// @param halfExtent Half-width and half-depth of the water surface.
    void setCausticsParams(bool enabled, float waterY, float time,
                           const glm::vec2& center = glm::vec2(0.0f),
                           const glm::vec2& halfExtent = glm::vec2(0.0f),
                           float intensity = 0.15f, float scale = 0.1f);

    /// @brief Set caustics quality tier (0=Full, 1=Approximate, 2=Simple).
    void setCausticsQuality(int quality) { m_causticsQuality = quality; }

    /// @brief Gets the procedural caustics texture ID (for external renderers like terrain).
    GLuint getCausticsTexture() const { return m_causticsTexture; }

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

    /// @brief Resizes the render target FBOs to match a new viewport size.
    /// Call this when the editor viewport panel changes dimensions.
    /// Does nothing if the size matches the current render target.
    /// @param width New render width in pixels.
    /// @param height New render height in pixels.
    void resizeRenderTarget(int width, int height);

    /// @brief Gets the current render target width.
    int getRenderWidth() const;

    /// @brief Gets the current render target height.
    int getRenderHeight() const;

    /// @brief Gets the OpenGL texture ID of the final composited (post-tonemapped) frame.
    /// Used by the editor viewport panel to display the scene in ImGui.
    GLuint getOutputTextureId() const;

    /// @brief Blits the final output to the default framebuffer (screen).
    /// Call this in play mode when ImGui is not displaying the viewport.
    /// @brief Blits the output FBO to the default framebuffer (screen).
    /// @param screenWidth Actual window width (may differ from render resolution).
    /// @param screenHeight Actual window height.
    void blitToScreen(int screenWidth = 0, int screenHeight = 0);

    /// @brief Renders the scene to the ID buffer for mouse picking.
    /// Call on-demand (when the user clicks in the viewport).
    void renderIdBuffer(const SceneRenderData& renderData,
                        const Camera& camera, float aspectRatio);

    /// @brief Reads the ID buffer pixel at (x, y) and decodes the entity ID.
    /// @param x Pixel X in FBO coordinates (0 = left).
    /// @param y Pixel Y in FBO coordinates (0 = bottom).
    /// @return Entity ID, or 0 if background/empty.
    uint32_t pickEntityAt(int x, int y);

    /// @brief Reads a rectangular region of the ID buffer and returns unique entity IDs.
    /// @param x0 Left pixel X.
    /// @param y0 Bottom pixel Y.
    /// @param x1 Right pixel X.
    /// @param y1 Top pixel Y.
    /// @return Set of unique non-zero entity IDs in the region.
    std::vector<uint32_t> pickEntitiesInRect(int x0, int y0, int x1, int y1);

    /// @brief Binds the output FBO and sets viewport for overlay rendering (debug draw, etc.).
    void bindOutputFbo();

    /// @brief Renders selection outlines into the output FBO for the given entities.
    void renderSelectionOutline(const SceneRenderData& renderData,
                                const std::vector<uint32_t>& selectedIds,
                                const Camera& camera, float aspectRatio);

    /// @brief Gets the skybox cubemap texture ID (0 if no skybox loaded).
    GLuint getSkyboxTextureId() const;

    /// @brief Gets the cascaded shadow map (for external renderers that need shadow data).
    /// @return Pointer to the CSM, or nullptr if not initialized.
    CascadedShadowMap* getCascadedShadowMap() const;

    /// @brief Sets the foliage renderer for shadow casting during the shadow pass.
    /// @param foliageRenderer Pointer to the foliage renderer (must outlive Renderer).
    /// @param foliageManager Pointer to the foliage manager for chunk access.
    /// @brief Sets the foliage renderer for shadow casting during the shadow pass.
    void setFoliageShadowCaster(class FoliageRenderer* foliageRenderer,
                                class FoliageManager* foliageManager);

    /// @brief Updates the elapsed time for foliage wind sync in the shadow pass.
    void setFoliageShadowTime(float time) { m_foliageShadowTime = time; }

    /// @brief Gets the resolved depth texture ID for soft particles and effects.
    /// @return OpenGL texture ID, or 0 if not available.
    GLuint getResolvedDepthTexture() const;

    /// @brief Resolves the MSAA scene color + depth to sampleable textures mid-frame.
    /// Call after opaques/terrain/foliage, before water rendering.
    /// The resolve FBOs are overwritten again in endFrame() — safe to use mid-frame.
    void resolveSceneForWater();

    /// @brief Gets the resolved scene color texture ID (from last resolveSceneForWater call).
    /// @return OpenGL texture ID, or 0 if not available.
    GLuint getResolvedColorTexture() const;

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
        std::vector<const std::vector<glm::mat4>*> boneMatrixPtrs;  ///< Parallel to modelMatrices (nullptr for static)
        std::vector<const std::vector<float>*> morphWeightPtrs;     ///< Parallel to modelMatrices (nullptr if no morphs)
    };

    /// @brief Groups render items by (mesh, material) pair for instanced drawing.
    /// Static version for unit testing (allocates fresh containers each call).
    static std::vector<InstanceBatch> buildInstanceBatchesStatic(
        const std::vector<SceneRenderData::RenderItem>& items);

private:
    void uploadLightUniforms(const Camera& camera);
    void uploadMaterialUniforms(const Material& material);
    void renderShadowPass(const std::vector<SceneRenderData::RenderItem>& shadowCasterItems,
                          const Camera& camera, float aspectRatio);
    void renderPointShadowPass(const std::vector<int>& shadowCasters);
    void selectShadowCastingPointLights();
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

    // AUDIT.md §H15 / FIXPLAN G1: per-object motion vector pass.
    // Runs after the full-screen camera-motion pass and overwrites the
    // motion buffer wherever scene geometry lives, so TAA reprojection
    // of dynamic / animated objects reproduces their real motion
    // instead of showing only the camera's.
    Shader m_motionVectorObjectShader;
    std::unordered_map<uint32_t, glm::mat4> m_prevWorldMatrices;
    /// @brief Most recent SceneRenderData pointer set by renderScene(),
    /// consumed by endFrame()'s motion vector overlay pass. Nulled after
    /// use so stale pointers cannot be dereferenced.
    const SceneRenderData* m_currentRenderData = nullptr;
    Shader m_idBufferShader;
    Shader m_outlineShader;
    EventBus& m_eventBus;
    // Subscription token for the WindowResizeEvent handler — torn down in
    // ~Renderer so the lambda's captured ``this`` can't be called after we
    // begin destruction. (AUDIT M9.)
    SubscriptionId m_windowResizeSubscription = 0;
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

    // SDSM (Sample Distribution Shadow Maps)
    std::unique_ptr<DepthReducer> m_depthReducer;
    bool m_sdsmEnabled = true;             // Enabled by default
    float m_sdsmNear = 0.1f;              // Smoothed near bound (lerped between frames)
    float m_sdsmFar = 150.0f;             // Smoothed far bound (lerped between frames)

    // External shadow casters (foliage)
    class FoliageRenderer* m_foliageShadowCaster = nullptr;
    class FoliageManager* m_foliageShadowManager = nullptr;
    float m_foliageShadowTime = 0.0f;  ///< Elapsed time for wind sync in shadow pass.
    // Scratch vector for the per-cascade foliage-chunk list — its capacity is
    // preserved across frames so the shadow pass doesn't heap-alloc. (AUDIT H9.)
    mutable std::vector<const class FoliageChunk*> m_scratchFoliageChunks;

    // Point light shadows
    std::vector<std::unique_ptr<PointShadowMap>> m_pointShadowMaps;

    // Skybox
    std::unique_ptr<Skybox> m_skybox;

    // Light probes
    std::unique_ptr<LightProbeManager> m_lightProbeManager;

    // SH probe grid
    std::unique_ptr<SHProbeGrid> m_shProbeGrid;
    float m_shNormalBias = 0.0f;  ///< Normal bias for SH grid sampling (anti-leak)
    GLuint m_fallbackTex3D = 0;  ///< 1×1×1 3D texture for Mesa sampler binding safety

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
    ColorVisionMode m_colorVisionMode = ColorVisionMode::Normal;

    // Phase 10 fog — CPU-side source of truth for the composite pass.
    // Uniforms are pushed in endFrame() between contact-shadow and bloom
    // setup, matching the composition order in
    // docs/phases/phase_10_fog_design.md §4.
    FogMode             m_fogMode = FogMode::None;
    FogParams           m_fogParams;
    bool                m_heightFogEnabled = false;
    HeightFogParams     m_heightFogParams;
    bool                m_sunInscatterEnabled = false;
    SunInscatterParams  m_sunInscatterParams;
    glm::vec3           m_cameraWorldPosition = glm::vec3(0.0f);

    // Accessibility settings applied to fog before GPU upload. The
    // transform lives in fog.cpp (pure function); the renderer just
    // stores the settings and calls `applyFogAccessibilitySettings`
    // each frame.
    PostProcessAccessibilitySettings m_postProcessAccessibility;

    // Phase 10.7 slice C1 — photosensitive safe-mode state, pushed
    // from Engine::run() once per frame. The bloom composite reads
    // these via limitBloomIntensity() at the uniform-upload site so
    // enabling safe mode mid-session takes effect on the next frame.
    bool                 m_photosensitiveEnabled = false;
    PhotosensitiveLimits m_photosensitiveLimits{};

    // Anti-aliasing mode
    AntiAliasMode m_antiAliasMode = AntiAliasMode::MSAA_4X;

    // TAA
    std::unique_ptr<Taa> m_taa;
    std::unique_ptr<Framebuffer> m_taaSceneFbo;  // Non-MSAA scene FBO for TAA mode
    glm::mat4 m_prevViewProjection = glm::mat4(1.0f);
    glm::mat4 m_lastViewProjection = glm::mat4(1.0f);

    // Saved view state (for restoring after water FBO passes)
    glm::mat4 m_savedLastProjection = glm::mat4(1.0f);
    glm::mat4 m_savedLastView = glm::mat4(1.0f);
    glm::mat4 m_savedLastViewProjection = glm::mat4(1.0f);

    // SMAA
    std::unique_ptr<Smaa> m_smaa;
    Shader m_smaaEdgeShader;
    Shader m_smaaBlendShader;
    Shader m_smaaNeighborhoodShader;

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

    // Diagnostic toggles (CLI: --isolate-feature). See header docs above.
    bool m_objectMotionOverlayEnabled = true;
    float m_iblMultiplierOverride = -1.0f;  // <0 means "use material's value"
    bool m_shGridForceDisabled = false;

    // Instanced rendering
    std::unique_ptr<InstanceBuffer> m_instanceBuffer;
    static constexpr int MIN_INSTANCE_BATCH_SIZE = 2;

    // Multi-Draw Indirect (MDI). CPU-side frustum culling happens in scene
    // gather; per-instance GPU culling is future work for a later
    // per-instance-compaction phase (per-instance AABB SSBO, atomic-counter
    // compaction, MDI command-build on GPU) that would reuse
    // assets/shaders/frustum_cull.comp.glsl. Note: ROADMAP E3 was closed by
    // finding 2026-05-02 — wiring the compute shader at per-chunk granularity
    // is redundant with FoliageManager::getVisibleChunks's CPU isAabbInFrustum
    // pass, so the future caller is the per-instance compaction work, not E3.
    std::unique_ptr<MeshPool> m_meshPool;
    std::unique_ptr<IndirectBuffer> m_indirectBuffer;
    bool m_mdiEnabled = false;  // Disabled until mesh pool is populated

    // Color grading LUT
    std::unique_ptr<ColorGradingLut> m_colorGradingLut;

    // IBL (Image-Based Lighting)
    std::unique_ptr<EnvironmentMap> m_environmentMap;

    // Stored clear color (re-applied each frame — editor may clobber glClearColor)
    glm::vec3 m_clearColor = glm::vec3(0.1f, 0.1f, 0.12f);

    // Frame counter for staggered cascade updates
    uint32_t m_shadowFrameCount = 0;

    // Reusable per-frame vectors (avoid allocation every frame)
    std::vector<SceneRenderData::RenderItem> m_culledItems;
    std::vector<SceneRenderData::RenderItem> m_sortedTransparentItems;
    std::vector<int> m_shadowCasters;
    std::vector<SceneRenderData::RenderItem> m_shadowCasterItems;
    std::vector<SceneRenderData::RenderItem> m_cascadeCulledCasters;

    // Pooled instance batching (avoids per-frame map + vector allocation)
    struct PairHash
    {
        size_t operator()(const std::pair<const Mesh*, const Material*>& p) const
        {
            size_t h1 = std::hash<const void*>{}(p.first);
            size_t h2 = std::hash<const void*>{}(p.second);
            return h1 ^ (h2 * 2654435761u);
        }
    };
    std::unordered_map<std::pair<const Mesh*, const Material*>, size_t, PairHash> m_batchIndexMap;
    std::vector<InstanceBatch> m_instanceBatches;
    size_t m_instanceBatchCount = 0;
    void buildInstanceBatches(const std::vector<SceneRenderData::RenderItem>& items);

    // Material grouping for MDI path
    std::unordered_map<const Material*, std::vector<const InstanceBatch*>> m_materialGroups;

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

    // Screen-space contact shadows
    Shader m_contactShadowShader;
    std::unique_ptr<Framebuffer> m_contactShadowFbo;
    bool m_contactShadowsEnabled = false;  // Disabled: needs G-buffer normals for correct results
    float m_contactShadowLength = 0.5f;   // Max ray length in view space
    int m_contactShadowSteps = 16;

    void generateSsaoKernel();
    void generateSsaoNoiseTexture();

    bool m_skyboxEnabled = true;

    // Dummy SSBO for model matrices (binding point 0) — Mesa requires all declared
    // SSBOs to have valid buffers bound, even when the MDI code path is not taken.
    GLuint m_dummyModelSSBO = 0;

    // Fallback textures — Mesa requires ALL declared samplers to have valid textures
    // bound at draw time, even when the shader code path doesn't sample them.
    GLuint m_fallbackTexture = 0;      ///< 1x1 white 2D texture
    GLuint m_fallbackCubemap = 0;      ///< 1x1 black cubemap
    GLuint m_fallbackTexArray = 0;     ///< 1x1x1 2D array texture

    // Skeletal animation bone matrix SSBO (binding point 2)
    GLuint m_boneMatrixSSBO = 0;
    static constexpr int MAX_BONES = 128;

    // Morph target dummy SSBO (binding point 3) — Mesa safety
    GLuint m_dummyMorphSSBO = 0;

    // Water caustics
    GLuint m_causticsTexture = 0;
    bool m_causticsEnabled = false;
    float m_causticsWaterY = 0.0f;
    float m_causticsTime = 0.0f;
    glm::vec2 m_causticsCenter = glm::vec2(0.0f);
    glm::vec2 m_causticsHalfExtent = glm::vec2(0.0f);
    float m_causticsIntensity = 0.15f;
    float m_causticsScale = 0.1f;
    int m_causticsQuality = 0;  // 0=Full, 1=Approximate, 2=Simple
    void generateCausticsTexture();

    // Frustum culling statistics (updated each frame in renderScene)
    CullingStats m_cullingStats;


    // Per-frame PMR arena for scratch allocations (reset each frame).
    // Value-initialized so cppcheck's uninitMemberVar rule stops firing;
    // the pmr arena overwrites this storage on first allocation anyway.
    static constexpr size_t FRAME_ARENA_SIZE = 2 * 1024 * 1024;  // 2 MB
    alignas(64) char m_frameArena[FRAME_ARENA_SIZE]{};
    std::pmr::monotonic_buffer_resource m_frameResource{
        m_frameArena, FRAME_ARENA_SIZE, std::pmr::null_memory_resource()};
    void resetFrameAllocator();
};

} // namespace Vestige
