# Subsystem Specification — `engine/renderer`

## Header

| Field | Value |
|-------|-------|
| Subsystem | `engine/renderer` |
| Status | `shipped` |
| Spec version | `1.0` |
| Last reviewed | `2026-04-28` (initial draft — pending cold-eyes review) |
| Owners | `milnet01` |
| Engine version range | `v0.1.0+` (foundation since Phase 4; lit + shadowed since Phase 4, IBL + post-process since Phase 7, GI since Phase 9) |

---

## 1. Purpose

`engine/renderer` is the GPU-facing subsystem that turns a `SceneRenderData` snapshot — meshes, materials, lights, particles, water surfaces, cloth dynamic meshes — into the pixels the user sees. It owns every OpenGL handle (textures, buffers, Frame Buffer Objects (FBOs), Vertex Array Objects (VAOs), shader programs, sync objects), every GLSL program the engine ships, the Cascaded Shadow Map (CSM) directional shadow path, the omnidirectional point-light shadow path, the Image-Based Lighting (IBL) split-sum environment, the Spherical-Harmonic (SH) probe grid + radiosity bake for diffuse Global Illumination (GI), the post-process chain (Screen-Space Ambient Occlusion (SSAO), bloom, tone mapping, color grading, color-vision filter, fog, Temporal Anti-Aliasing (TAA) / Subpixel Morphological Anti-Aliasing (SMAA) / 4× Multi-Sample Anti-Aliasing (MSAA)), the per-render-pass scratch arena, and the satellite renderers (foliage, trees, terrain, water, sprites, tilemaps, GPU compute particles, debug-draw, text). It exists as its own subsystem because every OpenGL call in the engine has to live behind one driver-affinity boundary (single-threaded GL context) and because a clean separation between "what to draw" (`engine/scene/`) and "how to draw it" (`engine/renderer/`) is what lets the same scene render at 60 FPS in the editor viewport, the runtime walkthrough, and a Phase 12 Vulkan backend without touching gameplay code. For the engine's primary use case — first-person architectural walkthroughs of biblical structures (Tabernacle, Solomon's Temple) — this subsystem is what makes a goldleaf-and-cedar interior at noon look like a goldleaf-and-cedar interior at noon, with the directional sun, the lampstand point lights, and the IBL probes that capture the room's ambient bounce all composing physically.

## 2. Scope

| In scope | Out of scope |
|----------|--------------|
| `Renderer` — pipeline orchestration, frame begin/end, MSAA → resolve → post-process → output | Domain `ISystem`s that submit drawables (`engine/systems/lighting`, `…/water`, `…/particle`, `…/vegetation`, `…/terrain`) |
| Forward+ pipeline (single geometry pass, mip-chain shadows, no G-buffer) | Deferred / G-buffer rendering — flagged as a Phase 12+ research item, not on roadmap |
| `Camera` — view + reverse-Z infinite-far projection + culling-finite projection + frustum extraction | First-person controller — `engine/core/first_person_controller.h` (drives camera but isn't owned here) |
| `Shader`, `Mesh`, `DynamicMesh`, `Texture`, `Material` — GPU-resource RAII wrappers | Asset loading / caching — `engine/resource/` (Renderer consumes already-loaded assets) |
| `CascadedShadowMap` (4 cascades, Sample-Distribution Shadow Maps (SDSM) bounds, staggered cascade updates), `PointShadowMap` (depth cubemap × 2 lights), per-cascade frustum culling, foliage shadow casting | Spot-light shadow maps — wired in `Renderer::addSpotLight` plumbing but not in CSM/point pass at this revision |
| `EnvironmentMap` (split-sum IBL: irradiance + Trowbridge-Reitz (GGX) prefilter mip chain + Bidirectional Reflectance Distribution Function (BRDF) Look-Up Table (LUT)), `LightProbeManager` (local cubemap probes), `SHProbeGrid` (L2 SH × 7 RGBA16F 3D textures), `RadiosityBaker` (multi-bounce iterative gathering), `IblPrefilter` (shared mip-chain loop), `IblCaptureSequence` (forward-Z bracket) | Global-illumination data structures the bake reads (lights, geometry) — those are owned by scene |
| Post-process: `Taa` (Halton jitter + per-object motion vectors + neighborhood clamp), `Smaa` (1× HIGH preset), `ColorGradingLut` (`.cube` file load + neutral fallback), `ColorVisionFilter` (Viénot/Brettel/Mollon dichromacy matrices), `Fog` (linear / exp / exp²) + height fog (Quílez analytic integral) + sun-inscatter lobe, bloom (Karis 13-tap downsample / tent upsample mip chain), SSAO (16-sample kernel + blur), contact shadows (disabled), tone mapping (Reinhard / ACES Filmic / off), High Dynamic Range (HDR) auto-exposure (Pixel Buffer Object (PBO) async readback) | Photosensitive-safe-mode and post-process-accessibility *policy* (`engine/accessibility/`); the renderer applies the limits but does not author them |
| `Skybox` — equirectangular High Dynamic Range Image (HDRI) load + procedural fallback + IBL regeneration on swap | HDRI sourcing / asset-management — `engine/resource/` |
| Satellite renderers: `FoliageRenderer`, `TreeRenderer`, `TerrainRenderer` (Continuous Distance LOD (CDLOD)), `WaterRenderer` + `WaterFbo` (planar reflect/refract), `ParticleRenderer` (CPU billboards), `GPUParticleSystem` (compute pipeline), `SpriteRenderer` + `SpriteAtlas` + `TilemapRenderer`, `TextRenderer` + `Font`, `DebugDraw` (line gizmos), `FrameDiagnostics` (PNG + diagnostic-text capture) | Component data they consume (`WaterSurfaceComponent`, `ParticleEmitterComponent`, `FoliageChunk`, etc.) — those live in `engine/scene/` and `engine/environment/` |
| `Framebuffer`, `FullscreenQuad`, `MeshPool` (mega Vertex Buffer Object (VBO) / Index Buffer Object (IBO)), `IndirectBuffer` (`glMultiDrawElementsIndirect`), `InstanceBuffer` (per-instance mat4 VBO), `DepthReducer` (compute min/max for SDSM) | Job system / async asset upload — `engine/resource/` |
| RAII GL-state guards: `ScopedForwardZ` (clipControl + depthFunc + clearDepth), `ScopedBlendState`, `ScopedCullFace`, `ScopedShadowDepthState` | Engine-wide RAII for non-GL handles — `engine/utils/` |
| Helpers: `SamplerFallback` (1×1 white / black-cube / 1-layer / 1-voxel — Mesa AMD draw-time binding rule), `bloom_downsample_karis.h` (CPU mirror of the GLSL combine for parity tests), `motion_overlay_prev_world.h` (TAA prev-world-matrix cache update), `normal_matrix.h` (uniform-scale fast path), `light_utils.h` (range / attenuation / Kelvin → RGB), `ibl_capture_sequence.h` (forward-Z scope bracket) | Formula tuning — every named constant that came from a fit lives in `tools/formula_workbench/` (CLAUDE.md Rule 6) |
| Subscribes to `WindowResizeEvent`; resizes scene / resolve / output / SSAO / TAA / SMAA / bloom mip / shadow FBOs in lock-step | Window creation / GLFW lifecycle — `engine/core/window.h` |

## 3. Architecture

```
┌──────────────────────────────────────────────────────────────────────────────────┐
│                                  Renderer                                        │
│              (engine/renderer/renderer.h:49)                                     │
│                                                                                  │
│  ┌─────────────────┐  ┌──────────────┐  ┌────────────┐  ┌─────────────────────┐  │
│  │ MSAA scene FBO  │  │ resolve FBO  │  │ output FBO │  │ idBuffer FBO (R32UI)│  │
│  │ (4× RGBA16F+D)  │  │ (RGBA16F+D)  │  │ (RGBA8 LDR)│  │ (mouse picking)     │  │
│  └────────┬────────┘  └──────┬───────┘  └─────┬──────┘  └─────────────────────┘  │
│           │                  │                │                                  │
│           ▼                  ▼                ▼                                  │
│  shadow / IBL / post-process satellites:                                         │
│  ┌────────────┐ ┌────────────┐ ┌────────────┐ ┌────────────┐ ┌──────────────┐    │
│  │CSM (4 casc)│ │PointShadow │ │EnvMap +    │ │SHProbeGrid │ │TAA / SMAA /  │    │
│  │+ DepthRedu │ │×MAX_PT_SH=2│ │BRDF LUT    │ │+ Radiosity │ │MSAA resolve  │    │
│  │(SDSM)      │ │            │ │+ LightPrb  │ │Baker       │ │              │    │
│  └────────────┘ └────────────┘ └────────────┘ └────────────┘ └──────────────┘    │
│  ┌────────────┐ ┌────────────┐ ┌────────────┐ ┌────────────┐ ┌──────────────┐    │
│  │SSAO + blur │ │Bloom mip   │ │ColorGrad   │ │FogParams + │ │ColorVision   │    │
│  │(16 kernel) │ │(Karis 6mip)│ │LUT 3D      │ │HeightFog + │ │matrix (post- │    │
│  │            │ │            │ │            │ │SunInscatter│ │tonemap)      │    │
│  └────────────┘ └────────────┘ └────────────┘ └────────────┘ └──────────────┘    │
│                                                                                  │
│  satellite renderers (each owns its FBO/shaders, called from systems):           │
│  ┌──────────────┐ ┌────────────┐ ┌──────────────┐ ┌───────────┐ ┌──────────┐     │
│  │TerrainRender │ │FoliageRen  │ │WaterRenderer │ │GPUParticle│ │SpriteRen │     │
│  │(CDLOD)       │ │+TreeRen    │ │+ WaterFbo    │ │System(comp│ │+TilemapR │     │
│  │              │ │            │ │              │ │ pipeline) │ │+TextRen  │     │
│  └──────────────┘ └────────────┘ └──────────────┘ └───────────┘ └──────────┘     │
└──────────────────────────────────────────────────────────────────────────────────┘
                                       │
                                       │ subscribes
                                       ▼
                              ┌────────────────┐
                              │ EventBus       │
                              │ (WindowResize) │
                              └────────────────┘
```

Key abstractions:

| Abstraction | Type | Purpose |
|-------------|------|---------|
| `Renderer` | class | Pipeline orchestrator; owns every OpenGL handle below, runs `beginFrame → renderScene → endFrame`. `engine/renderer/renderer.h:49` |
| `Camera` | class | First-person camera; reverse-Z infinite-far projection (`getProjectionMatrix`) plus a finite-far variant for frustum culling (`getCullingProjectionMatrix`). `engine/renderer/camera.h:21` |
| `Shader` | class (RAII) | Vertex+fragment or compute program; load, link, bind, uniform setters. `engine/renderer/shader.h:19` |
| `Mesh` / `DynamicMesh` | class (RAII) | Static / streamed VAO+VBO+IBO with `Vertex` (pos/normal/color/uv/tangent/bitangent/4 bones+weights). `engine/renderer/mesh.h:39`, `engine/renderer/dynamic_mesh.h:22` |
| `Texture` | class (RAII) | Loadable 2D texture with mipmap + filter mode (NEAREST → ANISOTROPIC_16X). `engine/renderer/texture.h:28` |
| `Material` | class | Blinn-Phong or Physically Based Rendering (PBR) surface; albedo + normal + metallic-roughness + emissive textures + alpha mode (OPAQUE/MASK/BLEND) + IBL multiplier. `engine/renderer/material.h:34` |
| `Framebuffer` | class (RAII) | Configurable FBO (color + depth, optional MSAA, optional float HDR, optional sampleable depth). `engine/renderer/framebuffer.h:26` |
| `FullscreenQuad` | class | Reusable 2-triangle screen quad VAO for post-process passes. `engine/renderer/fullscreen_quad.h` |
| `CascadedShadowMap` | class | 4-cascade directional shadow with frustum-fitted ortho projections, SDSM near/far refinement, staggered per-cascade updates, foliage shadow-caster hook. `engine/renderer/cascaded_shadow_map.h:30` |
| `ShadowMap` | class | Single-cascade directional shadow primitive (kept as a smaller building block + spot-light fallback). `engine/renderer/shadow_map.h:28` |
| `PointShadowMap` | class (RAII) | Depth-cubemap shadow for one point light × 6 faces; max `MAX_POINT_SHADOW_LIGHTS = 2` simultaneous. `engine/renderer/point_shadow_map.h:26` |
| `EnvironmentMap` | class | Split-sum IBL: 32×32 irradiance cubemap (diffuse) + 128² roughness-mip prefilter cubemap (5 mips, GGX importance-sampled per Karis 2013) + 512² BRDF integration LUT. `engine/renderer/environment_map.h:25` |
| `LightProbe` / `LightProbeManager` | class | Local IBL cubemap probes with Axis-Aligned Bounding Box (AABB) influence + fade-distance blend. `engine/renderer/light_probe.h:24`, `engine/renderer/light_probe_manager.h:31` |
| `SHProbeGrid` | class | 3D L2 SH probe grid (9 coefficients × 3 channels = 27 floats per probe), stored in 7 RGBA16F 3D textures with hardware trilinear interpolation. `engine/renderer/sh_probe_grid.h:38` |
| `RadiosityBaker` | class | Iterative multi-bounce gathering — re-captures the SH grid with the previous bounce visible until energy delta < threshold. `engine/renderer/radiosity_baker.h:34` |
| `Taa` | class | Halton-sequence sub-pixel jitter, history FBO, neighborhood AABB clamp, per-object motion vectors. `engine/renderer/taa.h:28` |
| `Smaa` | class | SMAA 1× HIGH: luma edge detect → blend-weight (area + search LUT) → neighborhood blend. `engine/renderer/smaa.h:25` |
| `ColorGradingLut` | class | 3D LUT preset registry; loads `.cube` files, blends to neutral by intensity. `engine/renderer/color_grading_lut.h:27` |
| `colorVisionMatrix` | free function | Returns the 3×3 RGB simulation matrix for protanopia / deuteranopia / tritanopia (Viénot 1999). `engine/renderer/color_vision_filter.h:43` |
| `Fog` types | structs + free functions | `FogMode` (Linear / Exp / Exp²), `FogParams`, `HeightFogParams`, `SunInscatterParams`, `applyFogAccessibilitySettings()`. `engine/renderer/fog.h` |
| `MeshPool` / `IndirectBuffer` / `InstanceBuffer` | classes | Mega-VBO/IBO + `glMultiDrawElementsIndirect` command buffer + per-instance mat4 VBO. `engine/renderer/mesh_pool.h:31`, `engine/renderer/indirect_buffer.h:33`, `engine/renderer/instance_buffer.h:17` |
| `DepthReducer` | class | Double-buffered Shader Storage Buffer Object (SSBO) compute reduction; produces the SDSM near/far bounds with one frame of latency. `engine/renderer/depth_reducer.h:19` |
| `SamplerFallback` | template + free function | Lazy 1×1 white / black-cube / 1-layer / 1-voxel textures bound to every declared-but-unused sampler unit (Mesa AMD draw-time binding rule, CODING_STANDARDS §21). `engine/renderer/sampler_fallback.h:63` |
| `ScopedForwardZ` / `ScopedBlendState` / `ScopedCullFace` / `ScopedShadowDepthState` | RAII classes (template-IO) | Snapshot + restore one slice of GL state per scope; injectable IO so the contract is unit-testable headlessly. `engine/renderer/scoped_*.h` |
| `runIblCaptureSequence` | template free function | Wraps a list of IBL capture passes in `ScopedForwardZ`; tests inject a recording mock guard. `engine/renderer/ibl_capture_sequence.h:53` |
| `combineBloomKarisGroups` | free function (constexpr-friendly) | CPU mirror of the Jimenez 13-tap downsample + Karis luminance weight (parity test against the GLSL). `engine/renderer/bloom_downsample_karis.h:55` |
| `computeNormalMatrix` | free function | Uniform-scale fast path for the per-draw normal matrix (skips `glm::inverse` for translate+rotate+uniform-scale). `engine/renderer/normal_matrix.h:35` |
| `updateMotionOverlayPrevWorld` | template free function | Refreshes the prev-world-matrix cache for the per-object motion-vector overlay; clears unconditionally even in non-TAA modes (R10 fix). `engine/renderer/motion_overlay_prev_world.h:51` |
| `FrameDiagnostics::capture` | static function | Saves a PNG screenshot + a text diagnostic report (camera state, culling stats, lighting, brightness analysis) — built for the partially-sighted user constraint (see project memory). `engine/renderer/frame_diagnostics.h:22` |
| `TerrainRenderer` / `FoliageRenderer` / `TreeRenderer` | class | CDLOD-grid terrain, instanced 3-quad-star foliage, mesh-LOD + billboard-crossfade trees. `engine/renderer/terrain_renderer.h`, `engine/renderer/foliage_renderer.h`, `engine/renderer/tree_renderer.h` |
| `WaterRenderer` + `WaterFbo` | class | Planar reflection + refraction half-resolution FBOs + Gerstner / Fast Fourier Transform (FFT) wave shader. `engine/renderer/water_renderer.h`, `engine/renderer/water_fbo.h` |
| `GPUParticleSystem` / `ParticleRenderer` | class | Compute-pipeline GPU particles (Emit → Simulate → Compact → Sort → IndirectDraw) and CPU instanced-billboard renderer. `engine/renderer/gpu_particle_system.h`, `engine/renderer/particle_renderer.h` |
| `SpriteRenderer` + `SpriteAtlas` + `TilemapRenderer` | class + free function | Instanced 2D sprite pass, TexturePacker JSON-Array atlas loader, tilemap → sprite-instance converter. `engine/renderer/sprite_renderer.h`, `engine/renderer/sprite_atlas.h`, `engine/renderer/tilemap_renderer.h:50` |
| `TextRenderer` + `Font` | class | FreeType glyph-atlas, 2D + 3D text, oblique (italic) shear free function. `engine/renderer/text_renderer.h`, `engine/renderer/font.h` |
| `DebugDraw` | class (static) | GL_LINES queue for editor gizmos: line, circle, wireSphere, cone, arrow. `engine/renderer/debug_draw.h:24` |

## 4. Public API

The renderer's facade is large — 53 public headers — so this section follows the **facade-by-header pattern** (CODING_STANDARDS §18, SPEC_TEMPLATE.md §4 pattern 2). Each block below shows the headline types + functions for one public header; the header is the single legitimate `#include` target downstream code uses, with full Doxygen on every symbol.

```cpp
// renderer/renderer.h — pipeline orchestrator. ~280 public methods.
class Renderer
{
public:
    explicit Renderer(EventBus&);
    bool   loadShaders(const std::string& assetPath);
    void   initFramebuffers(int w, int h, int msaaSamples = 4);
    void   beginFrame();
    void   endFrame(float deltaTime = 1.0f / 60.0f);
    void   renderScene(const SceneRenderData&, const Camera&, float aspect,
                       const glm::vec4& clipPlane = {}, bool geometryOnly = false,
                       const glm::mat4& viewOverride = {}, const glm::mat4& projOverride = {});
    void   resizeRenderTarget(int w, int h);
    GLuint getOutputTextureId() const;
    void   blitToScreen(int screenW = 0, int screenH = 0);

    // lighting
    void   setDirectionalLight(const DirectionalLight&); void setDirectionalLightEnabled(bool);
    bool   addPointLight(const PointLight&);     void clearPointLights();
    bool   addSpotLight (const SpotLight&);      void clearSpotLights();

    // skybox / IBL
    bool   loadSkyboxHDRI(const std::string&);   void setSkyboxEnabled(bool);
    void   captureSHGrid (const SceneRenderData&, const Camera&, float, int faceSize = 64);
    void   captureLightProbe(int probeIdx, const SceneRenderData&, const Camera&, float);

    // post-process
    void   setExposure(float); void setAutoExposure(bool); void setTonemapMode(int);
    void   setBloomEnabled(bool); void setBloomThreshold(float); void setBloomIntensity(float);
    void   setSsaoEnabled(bool); void setAntiAliasMode(AntiAliasMode);
    void   setColorGradingEnabled(bool); void nextColorGradingPreset();
    void   setColorVisionMode(ColorVisionMode);
    void   setFogMode(FogMode); void setHeightFogEnabled(bool); void setSunInscatterEnabled(bool);

    // accessibility sinks (called by Settings apply chain)
    void   setPostProcessAccessibility(const PostProcessAccessibilitySettings&);
    void   setPhotosensitive(bool, const PhotosensitiveLimits&);

    // editor / mouse picking
    void   renderIdBuffer(const SceneRenderData&, const Camera&, float);
    uint32_t pickEntityAt(int x, int y);
    std::vector<uint32_t> pickEntitiesInRect(int, int, int, int);
    void   renderSelectionOutline(const SceneRenderData&, const std::vector<uint32_t>&,
                                  const Camera&, float);

    // diagnostics (Phase 8 isolation toggles — kept; no production reason to disable)
    void   setObjectMotionOverlayEnabled(bool); void setIblMultiplierOverride(float);
    void   setIblSubScales(float diffuse, float specular); void setShGridForceDisabled(bool);
};
// see renderer/renderer.h:49 for full surface (~280 public methods).
```

```cpp
// renderer/camera.h — first-person camera + projection matrices.
constexpr float DEFAULT_FOV = 45.0f;
class Camera
{
public:
    explicit Camera(const glm::vec3& pos = {0,0,3}, float yaw = -90.0f, float pitch = 0.0f);
    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix      (float aspect) const;   ///< reverse-Z, infinite far
    glm::mat4 getCullingProjectionMatrix(float aspect) const;  ///< standard, finite far
    void      processKeyboard(...); void processMouse(...); void processScroll(...);
    glm::vec3 getPosition() const; glm::vec3 getFront() const; glm::vec3 getUp() const;
    void      setFov(float); float getFov() const;
};
// see renderer/camera.h:21 for full surface.
```

```cpp
// renderer/shader.h — GLSL program RAII wrapper.
class Shader
{
public:
    Shader(); ~Shader();
    Shader(Shader&&) noexcept; Shader& operator=(Shader&&) noexcept;     // move-only
    bool   loadFromFiles(const std::string& vert, const std::string& frag);
    bool   loadComputeShader(const std::string& comp);
    void   use() const;
    GLuint getId() const;
    void   setBool/Int/Float/Vec2/Vec3/Vec4/Mat3/Mat4(std::string_view name, T val);
};
// see renderer/shader.h:19 for full surface.
```

```cpp
// renderer/mesh.h, renderer/dynamic_mesh.h — vertex data + GPU buffers.
struct Vertex { glm::vec3 position, normal, color; glm::vec2 texCoord;
                glm::vec3 tangent, bitangent; glm::ivec4 boneIds; glm::vec4 boneWeights; };
void calculateTangents(std::vector<Vertex>&, const std::vector<uint32_t>& indices);
class Mesh         { /* static  GL_STATIC_DRAW VBO/IBO + VAO */ };
class DynamicMesh  { /* GL_DYNAMIC_STORAGE_BIT VBO; updateVertices() per frame */ };
// see renderer/mesh.h:39, renderer/dynamic_mesh.h:22 for full surface.
```

```cpp
// renderer/texture.h, renderer/material.h, renderer/light.h — surface + light data.
enum class TextureFilterMode { NEAREST, LINEAR, TRILINEAR, ANISOTROPIC_4X/_8X/_16X };
class  Texture { bool loadFromFile(const std::string&, bool linear = false);
                 void setFilterMode(TextureFilterMode); GLuint getId() const; };
enum class MaterialType { BLINN_PHONG, PBR };
enum class AlphaMode    { OPAQUE, MASK, BLEND };
class  Material { /* albedo + normal + metallic-roughness + emissive textures + IBL multiplier */ };
struct DirectionalLight { glm::vec3 direction, ambient, diffuse, specular; };
struct PointLight       { glm::vec3 position; float constant, linear, quadratic;
                          float range; bool castsShadow; /* + ambient/diffuse/specular */ };
struct SpotLight        { /* direction + inner/outer cone */ };
// see renderer/{texture,material,light}.h for full surface.
```

```cpp
// renderer/framebuffer.h, renderer/fullscreen_quad.h — render-target + post-process primitives.
struct FramebufferConfig { int width, height, samples; bool hasColor, hasDepth, isFloat, isDepthTex; };
class  Framebuffer  { void bind(); static void unbind(); void resolve(Framebuffer& dst);
                      void resize(int w, int h); GLuint getColorTexture() const; ... };
class  FullscreenQuad { void draw() const; };
// see renderer/framebuffer.h:26 for full surface.
```

```cpp
// renderer/cascaded_shadow_map.h, renderer/shadow_map.h, renderer/point_shadow_map.h — shadow primitives.
struct CascadedShadowConfig { int resolution = 2048; int cascadeCount = 4; float shadowDistance = 150;
                              float splitLambda = 0.5f; };
class  CascadedShadowMap { void update(const DirectionalLight&, const Camera&, float aspect);
                           void setSdsmBounds(float near, float far);
                           void beginCascade(int idx); void endCascade();
                           const std::array<glm::mat4, 4>& getLightSpaceMatrices() const;
                           GLuint getDepthArrayTexture() const; };
class  ShadowMap          { void update(const DirectionalLight&, const glm::vec3& sceneCenter); ... };
constexpr int MAX_POINT_SHADOW_LIGHTS = 2;
class  PointShadowMap     { void update(const glm::vec3& lightPos);
                            void beginFace(int face); void endFace();
                            GLuint getCubemapTexture() const; };
// see renderer/cascaded_shadow_map.h:30 for full surface.
```

```cpp
// renderer/environment_map.h, renderer/light_probe.h, renderer/light_probe_manager.h,
// renderer/sh_probe_grid.h, renderer/radiosity_baker.h, renderer/ibl_prefilter.h,
// renderer/ibl_capture_sequence.h — IBL + GI.
class EnvironmentMap     { bool initialize(const std::string& assetPath);
                           void generate(GLuint skyboxCubemap, bool hasCubemap,
                                         const FullscreenQuad&, const Shader& skyboxShader);
                           void bindIrradiance(int unit) const; void bindPrefilter(int unit) const;
                           void bindBrdfLut(int unit) const; };
struct ProbeAssignment   { const LightProbe* probe; float weight; };
class LightProbeManager  { int  addProbe(const glm::vec3&, const AABB&, float fadeDist = 2.0f);
                           void generateProbe(int probeIdx, GLuint capturedCubemap);
                           ProbeAssignment findProbe(const glm::vec3&) const; };
struct SHGridConfig      { glm::vec3 worldMin, worldMax; glm::ivec3 resolution; };
class SHProbeGrid        { bool initialize(const SHGridConfig&);
                           void capture(Renderer&, const SceneRenderData&, const Camera&,
                                        float aspect, int faceSize = 64);
                           void bind(int firstUnit) const; };  // 7 RGBA16F 3D textures
struct RadiosityConfig   { int maxBounces = 4; float convergenceThreshold = 0.02f; float normalBias; };
class RadiosityBaker     { void bake(Renderer&, const SceneRenderData&, const Camera&,
                                     float aspect, const RadiosityConfig& = {}); };
inline void runIblPrefilterLoop(const IblPrefilterParams&);   // shared mip-chain prefilter
void        runIblCaptureSequence(std::initializer_list<std::function<void()>> steps);
// see renderer/{environment_map,light_probe,light_probe_manager,sh_probe_grid,radiosity_baker,
//               ibl_prefilter,ibl_capture_sequence}.h for full surface.
```

```cpp
// renderer/taa.h, renderer/smaa.h, renderer/skybox.h, renderer/fog.h, renderer/color_grading_lut.h,
// renderer/color_vision_filter.h — post-process + sky + accessibility filter.
enum class AntiAliasMode { NONE, MSAA_4X, TAA, SMAA };
class Taa  { glm::vec2 getJitterOffset() const; void nextFrame();
             glm::mat4 jitterProjection(const glm::mat4&, int w, int h) const;
             void resolve(GLuint scene, GLuint motion, GLuint history, GLuint output); };
class Smaa { /* edge → blend-weight (area + search LUT) → neighborhood blend; HIGH preset */ };
class Skybox { bool loadCubemap(const std::vector<std::string>&); bool loadEquirectangular(const std::string&);
               void draw() const; GLuint getTextureId() const; };
enum class FogMode { None, Linear, Exp, Exp2 };
struct FogParams         { glm::vec3 colour; float start, end, density; };
struct HeightFogParams   { float groundDensity, falloff, fogHeight; };
struct SunInscatterParams{ glm::vec3 colour; float exponent, startDistance; };
void  applyFogAccessibilitySettings(FogParams&, HeightFogParams&, SunInscatterParams&,
                                    const PostProcessAccessibilitySettings&);
class ColorGradingLut    { void initialize(); bool loadCubeFile(const std::string&, const std::string&);
                           void bind(unsigned unit) const; void nextPreset(); ... };
enum class ColorVisionMode { Normal, Protanopia, Deuteranopia, Tritanopia };
glm::mat3   colorVisionMatrix(ColorVisionMode);
const char* colorVisionModeLabel(ColorVisionMode);
// see renderer/{taa,smaa,skybox,fog,color_grading_lut,color_vision_filter}.h for full surface.
```

```cpp
// renderer/depth_reducer.h, renderer/instance_buffer.h, renderer/indirect_buffer.h,
// renderer/mesh_pool.h — GPU draw machinery.
class DepthReducer    { bool init(const std::string& computeShaderPath);
                        void dispatch(GLuint depthTex, int w, int h);
                        bool readBounds(float cameraNear, float& outNear, float& outFar); };
class InstanceBuffer  { void upload(const std::vector<glm::mat4>&); GLuint getHandle() const; };
struct DrawElementsIndirectCommand { GLuint count, instanceCount, firstIndex; GLint baseVertex; GLuint baseInstance; };
class IndirectBuffer  { void addCommand(const MeshPoolEntry&, const std::vector<glm::mat4>&);
                        void clear(); void upload(); void dispatch(); };
struct MeshPoolEntry  { int32_t baseVertex; uint32_t firstIndex, indexCount; };
class MeshPool        { MeshPoolEntry registerMesh(const Mesh*, const std::vector<Vertex>&,
                                                   const std::vector<uint32_t>&);
                        bool hasMesh(const Mesh*) const; void bind(); void unbind(); };
// see renderer/{depth_reducer,instance_buffer,indirect_buffer,mesh_pool}.h for full surface.
```

```cpp
// renderer/scoped_forward_z.h, scoped_blend_state.h, scoped_cull_face.h, scoped_shadow_depth_state.h,
// renderer/sampler_fallback.h, renderer/normal_matrix.h, renderer/light_utils.h,
// renderer/bloom_downsample_karis.h, renderer/motion_overlay_prev_world.h — RAII guards + helpers.
class  ScopedForwardZ          { /* clipControl + depthFunc + clearDepth save/restore */ };
template<typename Io = BlendStateGlIo>    class ScopedBlendStateImpl { ... };
template<typename Io = CullFaceGlIo>      class ScopedCullFaceImpl   { ... };
template<typename Io = ShadowDepthGlIo>   class ScopedShadowDepthStateImpl { ... };

template<typename Creator = GlTextureCreator> struct SamplerFallbackImpl
{ GLuint sampler2D(); GLuint samplerCube(); GLuint sampler2DArray(); GLuint sampler3D();
  void shutdown(); };

inline glm::mat3 computeNormalMatrix(const glm::mat4& model);          // uniform-scale fast path
inline float     calculateLightRange(float c, float l, float q);
inline void      setAttenuationFromRange(float r, float& c, float& l, float& q);
inline glm::vec3 kelvinToRgb(float kelvin);
inline glm::vec3 combineBloomKarisGroups(const glm::vec3& centre,
                                          const glm::vec3& tl, const glm::vec3& tr,
                                          const glm::vec3& bl, const glm::vec3& br);
template<typename ItemRange>
void updateMotionOverlayPrevWorld(std::unordered_map<uint32_t, glm::mat4>& cache,
                                  bool isTaa, const ItemRange& opaque, const ItemRange& transparent);
// see renderer/{scoped_*,sampler_fallback,normal_matrix,light_utils,bloom_downsample_karis,
//               motion_overlay_prev_world}.h for full surface.
```

```cpp
// renderer/terrain_renderer.h, foliage_renderer.h, tree_renderer.h, water_renderer.h,
// water_fbo.h, particle_renderer.h, gpu_particle_system.h, sprite_renderer.h,
// sprite_atlas.h, tilemap_renderer.h, text_renderer.h, font.h, debug_draw.h,
// frame_diagnostics.h — satellite renderers.
class TerrainRenderer    { bool init(const std::string& assetPath);
                           void render(const Scene&, const Camera&, float aspect, ...,
                                       CascadedShadowMap*); };
class FoliageRenderer    { void render(const std::vector<const FoliageChunk*>&,
                                       const Camera&, const glm::mat4& viewProj,
                                       float time, float maxDist,
                                       CascadedShadowMap*, const DirectionalLight*); };
class TreeRenderer       { void render(const std::vector<const FoliageChunk*>&,
                                       const Camera&, const glm::mat4& viewProj, float time); };
class WaterRenderer      { bool init(const std::string&); void shutdown();
                           void render(const std::vector<WaterRenderItem>&,
                                       const Camera&, float aspect, float time); };
class WaterFbo           { bool init(int rW, int rH, int rfW, int rfH);
                           void bindReflection(); void bindRefraction(); };
class ParticleRenderer   { /* CPU instanced billboards */ };
class GPUParticleSystem  { /* compute pipeline: emit/sim/compact/sort/indirect-draw */ };
class SpriteRenderer     { /* instance-rate batched sprite pass */ };
class SpriteAtlas        { static std::shared_ptr<SpriteAtlas> loadFromJson(const std::string&); };
const SpriteAtlas* buildTilemapInstances(const TilemapComponent&, const glm::mat4& world,
                                         float depth, std::vector<SpriteInstance>& out);
class TextRenderer       { /* 2D + 3D text; oblique-shear free function in text_oblique:: */ };
class Font               { bool loadFromFile(const std::string&, int pixelSize = 48); };
class DebugDraw          { static void line/circle/wireSphere/cone/arrow(...); static void flush(...); };
class FrameDiagnostics   { static std::string capture(const Renderer&, const Camera&, ...); };
// see the per-header sources for full surface.
```

**Non-obvious contract details:**

- `Renderer::initFramebuffers(...)` must be called **after** `loadShaders(...)` and before the first `beginFrame` — the bloom mip chain, SSAO FBOs, TAA scene FBO, and SMAA tables are built here, and `beginFrame` does not re-create them on demand.
- `renderScene(...)` with `geometryOnly = true` skips the shadow passes and FBO rebind; the caller (water reflection / refraction) **must** have a compatible FBO bound and **must** restore the scene FBO via `rebindSceneFbo()` before `endFrame`. Water uses `saveViewState()` / `restoreViewState()` to bracket the geometry-only call so TAA reprojection sees the correct view-projection on the *real* frame.
- `Renderer::endFrame(deltaTime)` consumes `m_currentRenderData` (set by the last `renderScene` call) for the per-object motion-vector overlay, then **nulls the pointer** — calling `endFrame` twice without a fresh `renderScene` between draws an empty motion buffer, which is the correct degenerate behaviour but worth knowing.
- Add-light call (`addPointLight` / `addSpotLight`) returns `false` if the per-frame light cap is exceeded (16 point, 8 spot per the GLSL UBO size); the renderer logs a warning at most once per frame to avoid spamming the console.
- `loadSkyboxHDRI(...)` triggers an IBL regeneration synchronously — the call blocks for ~10–50 ms (5 prefilter mips × 6 faces + irradiance convolution); it is **not** safe to call from the editor's hot-reload tick without a one-frame "regenerating IBL…" UI overlay.
- `setColorVisionMode`, `setPostProcessAccessibility`, and `setPhotosensitive` are the **renderer-side endpoints of the accessibility-sink contract** in `engine/core/settings_apply.h`. They are pure setters — uniform upload happens inside `endFrame`'s composite pass.
- The four Phase-8 isolation toggles (`setObjectMotionOverlayEnabled`, `setIblMultiplierOverride`, `setIblSubScales`, `setShGridForceDisabled`) exist for AUDIT bisection and are **kept on the public surface deliberately** — they are part of the `--isolate-feature` CLI contract.
- Mouse picking via `renderIdBuffer` + `pickEntityAt(x, y)` is **on-demand**, not per-frame; `pickEntityAt` triggers a synchronous `glReadPixels` (single texel), so it pipeline-stalls the GPU. That's acceptable for click handling; never call it inside a hover loop.

**Stability:** the facade above is semver-frozen for `v0.x`. Two known evolution points: (a) `setAntiAliasMode(AntiAliasMode::SMAA)` is wired but the SMAA path's depth-aware reprojection is still parking-lot — flagged as Open Q3 below; (b) the `m_mdiEnabled` Multi-Draw Indirect (MDI) path is staged behind a default-off flag pending mesh-pool population code (`engine/scene/scene_manager.cpp`) — flagged as Open Q4. Both are additive when they land.

## 5. Data Flow

**Steady-state per-frame (`Engine::run` calls into `Renderer` once per frame; see `engine/renderer/renderer.cpp:2656` for `renderScene`, `:693` for `beginFrame`, `:756` for `endFrame`):**

1. **Scene gather (caller side).** `Engine::run` → `SceneManager::collectRenderData(out, photosensitiveEnabled, limits)` → fills the reusable `SceneRenderData` (opaque + transparent + cloth + particles + water + lights). The renderer never walks the scene graph directly.
2. **Light upload.** `Engine::run` calls `clearPointLights / clearSpotLights / addPointLight / addSpotLight` once per frame from `m_renderData`; `setDirectionalLight` from the active `DirectionalLightComponent`.
3. **`beginFrame()`.** Decide TAA / SMAA / MSAA mode → bind the appropriate scene FBO (TAA + SMAA use the non-MSAA `m_taaSceneFbo`; MSAA uses `m_msaaFbo`). Apply `glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE)` (reverse-Z) + `glDepthFunc(GL_GEQUAL)` + `glClearDepth(0.0f)`. Clear color + depth.
4. **`renderScene(renderData, camera, aspect, …)`.** Steady path:
   1. **Frustum cull** opaque + transparent + shadow-caster lists into the pre-allocated `m_culledItems / m_sortedTransparentItems / m_shadowCasterItems` vectors (capacity preserved across frames; AUDIT H9). Stats land in `m_cullingStats`.
   2. **`renderShadowPass(...)`**: switch via `ScopedForwardZ` to forward-Z + `GL_LESS` + clearDepth 1.0; for each cascade (staggered — 1 cascade per frame on a 4-frame cycle for cascades 2/3, full update on cascades 0/1) render shadow-caster meshes plus foliage if `m_foliageShadowCaster` is set. SDSM near/far bounds (read from the previous frame via `DepthReducer::readBounds`) drive the cascade split. Restore reverse-Z on scope exit.
   3. **`renderPointShadowPass(...)`**: pick up to `MAX_POINT_SHADOW_LIGHTS = 2` shadow-casting point lights (sorted by camera distance), render each one to its 6-face cubemap.
   4. **Skybox** (if `m_skyboxEnabled`): `Skybox::draw` with `GL_GEQUAL` (reverse-Z infinite far).
   5. **Opaque scene** (forward+ single pass): bind scene shader, upload light + IBL + shadow uniforms, build instance batches via `buildInstanceBatches`, draw via either instanced (`MIN_INSTANCE_BATCH_SIZE = 2`) or non-instanced fallback. If `m_mdiEnabled` and `m_meshPool` is populated, take the MDI path: bind the pool VAO, group by material, dispatch one `glMultiDrawElementsIndirect` per material group.
   6. **Cloth meshes** (`renderData.clothItems`): same shader, `DynamicMesh` VAO, no instancing.
   7. **Transparent items**: depth-sorted back-to-front, `GL_BLEND` enabled (RAII'd via `ScopedBlendState`), depth-write off.
   8. **Object motion-vector overlay** (TAA only): write per-pixel motion via the per-object motion shader using `m_prevWorldMatrices` cache; cache is cleared+populated end-of-frame by `updateMotionOverlayPrevWorld`.
   9. **ID buffer** (only on demand, via `renderIdBuffer`): single integer-color FBO, one shader per draw, `glReadPixels` from `pickEntityAt`.
5. **`endFrame(dt)`.** Resolve MSAA → run depth reduction (compute SDSM bounds for next frame) → SSAO + blur (if enabled) → contact shadows (currently disabled) → bloom mip chain (Karis 13-tap downsample, tent upsample, mip 0…BLOOM_MIP_COUNT=6) → auto-exposure luminance pass + double-buffered PBO async readback → composite pass (tonemap + bloom add + fog + sun-inscatter + color-grade LUT + color-vision matrix + dither) into `m_outputFbo` → if TAA, run resolve into history with neighborhood AABB clamp; if SMAA, run edge → blend-weight → neighborhood passes. Update `m_prevWorldMatrices` via `updateMotionOverlayPrevWorld` (clears unconditionally even in non-TAA modes — R10 fix). Null `m_currentRenderData`. Restore default GL state.
6. **Caller composites.** `Engine::run` chooses: `Renderer::blitToScreen(...)` (play mode) or `getOutputTextureId()` (editor draws into an ImGui viewport).

**Cold start (`Renderer::Renderer(EventBus&)` + `loadShaders` + `initFramebuffers` — `engine/renderer/renderer.cpp:87`, `:253`, `:460`):**

1. **Constructor** — subscribe to `WindowResizeEvent`. Set initial state defaults (exposure 1.0, ACES tonemap, MSAA 4×, SSAO on, bloom on with intensity 0.04).
2. **`loadShaders(assetPath)`** — load every shader program (scene / screen / shadow-depth / skybox / point-shadow-depth / bloom-down + up / SSAO + blur / TAA resolve / motion-vector full-screen + per-object / SMAA edge + blend + neighborhood / id-buffer / outline / contact-shadow). Failure on any one: log + return false.
3. **`initFramebuffers(w, h, msaaSamples)`** — allocate the FBO chain: MSAA scene → resolve → output → ID buffer → SSAO + blur → contact-shadow → TAA scene FBO + history. Build the bloom mip texture (`R11F_G11F_B10F`, 6 mips). Build the auto-exposure luminance texture + 2× PBO double buffer. Generate the SSAO sample kernel + noise texture. Construct CSM, point shadow maps, depth reducer, skybox, light-probe manager, SH probe grid, environment map, color-grading LUT, instance buffer, mesh pool, indirect buffer, TAA + SMAA. Initialise `SamplerFallback` 1×1 fallbacks (white 2D, black cubemap, 1-layer 2D-array, 1-voxel 3D — Mesa AMD draw-time binding rule).

**One-shot bakes (typically end of scene-load, never per frame):**

- **`captureSHGrid(renderData, camera, aspect, faceSize)`** — for each probe in the grid, render 6 cubemap faces from the probe position, project to L2 SH coefficients, write into the 7 RGBA16F 3D textures. `faceSize` 64 default; 16 used by the radiosity baker for fast bounce iterations.
- **`RadiosityBaker::bake(renderer, renderData, camera, aspect, config)`** — call `captureSHGrid` repeatedly; each iteration the SH grid contributes to the previous bounce's indirect lighting, until energy delta < `convergenceThreshold` or `maxBounces` reached.
- **`captureLightProbe(probeIdx, …)`** — render the scene to a temporary cubemap from the probe's position, then convolve into irradiance + GGX-prefilter cubemaps (shared `runIblPrefilterLoop`).

**Exception path:**

- Shader load failure → `Logger::error` + propagate `false` to `Engine::initialize` → engine aborts (renderer cannot run without shaders).
- FBO incomplete after a resize → `Logger::error("Framebuffer incomplete, status = 0x...")` + the next `beginFrame` rebinds the default framebuffer (degraded but live).
- HDRI load failure → `Logger::warning` + procedural-gradient fallback skybox; IBL cubemaps regenerate from the gradient.
- `WindowResizeEvent` published during `~Renderer` is impossible in normal flow because the destructor unsubscribes (`m_windowResizeSubscription`); see AUDIT M9 for the underlying lifetime fix.

## 6. CPU / GPU placement

This is a GPU-active subsystem (SPEC_TEMPLATE.md §6 pattern C). Multiple workloads, multiple placements. CLAUDE.md Rule 7 + CODING_STANDARDS §17 govern.

| Workload | Placement | Reason |
|----------|-----------|--------|
| Mesh / vertex transform | GPU (vertex shader) | Per-vertex, data-parallel — CODING_STANDARDS §17 default for "per-vertex." |
| Lighting (Blinn-Phong / PBR Cook-Torrance + Smith GGX) | GPU (fragment shader) | Per-pixel, data-parallel. Lighting branch on `MaterialType` is uniform-driven, so divergence is per-draw not per-pixel. |
| Shadow depth pass (CSM × 4 cascades, point cubemap × MAX_POINT_SHADOW=2 × 6 faces) | GPU (vertex + null fragment) | Per-fragment depth-only; engine of choice for the workload. |
| Frustum cull (per-cascade + per-camera) for `RenderItem` lists | CPU (main thread, per frame) | Branching, sparse, decision-heavy — CODING_STANDARDS §17 default for "branching/sparse." Test scaffolding (`tests/test_instanced_rendering.cpp`) covers the cull. |
| Per-instance frustum cull on the GPU | Future GPU (compute) | Designed for `assets/shaders/frustum_cull.comp.glsl` (already used by foliage) — see `engine/renderer/renderer.h:692` ROADMAP-E3 reference. |
| Instance-batching (`buildInstanceBatches`) | CPU (main thread) | Hash-table grouping by `(Mesh*, Material*)`; pure CPU bookkeeping; `m_batchIndexMap` capacity preserved across frames. |
| Skinning (bone matrices) | GPU (vertex shader) | Per-vertex × 4 bone weights; SSBO upload per draw. CPU writes the SSBO on the main thread; GPU consumes. |
| Morph-target blending | GPU (vertex shader, SSBO of deltas) | Per-vertex × N targets; `m_dummyMorphSSBO` is the Mesa fallback when no morphs. |
| TAA history blend + neighborhood clamp | GPU (fragment shader) | Per-pixel; full-screen pass. |
| TAA reprojection (motion vector pass) | GPU (full-screen + per-object overlay) | Per-pixel; `updateMotionOverlayPrevWorld` is the only CPU-side adjacent state, and it just walks the render-item list. |
| SMAA (edge → blend-weight → neighborhood) | GPU (3 full-screen fragment passes) | Per-pixel. |
| Bloom (downsample + upsample mip chain) | GPU (fragment shader) | Per-pixel data-parallel. The CPU mirror in `bloom_downsample_karis.h` exists **only** for the parity test (`tests/test_bloom_downsample_karis.cpp`), not as a runtime path. |
| SSAO (16-sample kernel + blur) | GPU (fragment shader) | Per-pixel. |
| Auto-exposure luminance reduction | GPU (mipmap chain) + CPU readback | GPU writes the mipmapped luminance texture; CPU reads via double-buffered PBO async (one frame latency, no GPU stall). |
| Tone-mapping (Reinhard / ACES Filmic) + color grading + color-vision matrix + fog composite | GPU (fragment shader, single composite pass) | Per-pixel. |
| Depth reduction (SDSM bounds) | GPU (compute) + CPU readback | `glDispatchCompute` writes a double-buffered SSBO; CPU reads with one frame of latency to drive next-frame cascade splits. Same async pattern as auto-exposure. |
| GPU particle pipeline (Emit / Simulate / Compact / Sort / IndirectDraw) | GPU (compute + indirect draw) | Per-particle data-parallel; bitonic sort scales well on the GPU. |
| CPU particle billboards | CPU build, GPU draw (instanced) | < 500-particle threshold per `engine/systems/particle_system.cpp`; GPU pipeline kicks in beyond. |
| IBL pre-filter mip chain | GPU (one-shot, scene-load only) | 6 faces × 5 mips × GGX importance sample (per Karis 2013 Real Shading in UE4); not on the per-frame budget. |
| Radiosity bake | GPU compute + render passes (one-shot) | Multi-pass capture; bounded by `maxBounces` and convergence threshold. |
| SH probe grid capture | GPU render → CPU SH projection (one-shot) | The cubemap render is GPU; the L2 projection per probe is CPU because it is a small reduction (~40 floats) and the sum must be exact. |
| Mouse pick (`pickEntityAt`) | GPU render → CPU `glReadPixels` (single texel) | On-demand, click-driven, not per-frame. |
| Light range computation, attenuation derivation, Kelvin → RGB | CPU (UI / settings callback) | One-shot per-edit; trivially cheap; `light_utils.h` inline functions. |

**Dual implementations.** Two CPU mirrors of GPU shader code exist for parity tests, **not** as runtime paths:

- `bloom_downsample_karis.h`'s `combineBloomKarisGroups` mirrors the GLSL combine in `bloom_downsample.frag.glsl`. Pinned by `tests/test_bloom_downsample_karis.cpp` (energy preservation + Karis weighting).
- `normal_matrix.h`'s `computeNormalMatrix` is the upload-side single-source; the vertex shader still does `normalize(normalMatrix * normal)` so the spec / runtime contract is "the upload path matches what the shader expects." Pinned implicitly by the visual tests (no dedicated parity test yet — Open Q5).

## 7. Threading model

Per CODING_STANDARDS §13. **OpenGL context affinity is single-thread** — every `gl*` call must originate on the thread that created the context. The engine treats this as "main thread = render thread = GL thread"; there is no separate render thread.

| Caller thread | Allowed APIs | Locks held |
|---------------|--------------|------------|
| **Main thread** (the GL-context-owning thread; the only thread that runs `Engine::run`) | All public methods of `Renderer`, `Camera`, `Shader`, `Mesh`, `DynamicMesh`, `Texture`, `Material`, `Framebuffer`, `Skybox`, `EnvironmentMap`, `LightProbe(Manager)`, `SHProbeGrid`, `RadiosityBaker`, `Taa`, `Smaa`, `ColorGradingLut`, `CascadedShadowMap`, `ShadowMap`, `PointShadowMap`, `MeshPool`, `IndirectBuffer`, `InstanceBuffer`, `DepthReducer`, `GPUParticleSystem`, `ParticleRenderer`, `SpriteRenderer`, `TilemapRenderer`, `TextRenderer`, `Font`, `DebugDraw`, `WaterRenderer`, `WaterFbo`, `TerrainRenderer`, `FoliageRenderer`, `TreeRenderer`, `FrameDiagnostics`, `SamplerFallback`, every `Scoped*` RAII guard, every free function. | None — main thread is single-threaded by contract. |
| **Worker threads** (`AsyncTextureLoader`, audio thread, job-system pool) | **Pure-data helpers only** — anything that doesn't touch GL state. In practice that is: the constexpr / inline functions in `bloom_downsample_karis.h`, `normal_matrix.h`, `light_utils.h`, the SH-projection math inside `SHProbeGrid` capture (called from the main thread, but mathematically pure), `colorVisionMatrix`, the `Vertex` + `Material` plain-data manipulations, the `SpriteAtlas::loadFromJson` parser (no GL), the FreeType glyph rasterisation in `Font::loadFromFile` (no GL until the final upload). The GL-resource-creation half of `Texture::loadFromFile`, `Mesh` upload, `Font` glyph atlas upload, `EnvironmentMap` capture, etc. **must** happen on the main thread. | None — purely-CPU helpers are free of locks. |

`AsyncTextureLoader` (in `engine/resource/`) is the canonical pattern: it decodes pixel data on a worker, hands the decoded buffer back to the main thread, and the main thread runs `glTextureStorage2D` / `glTextureSubImage2D`.

**Lock-free / atomic:** none required. The renderer has no shared state with worker threads beyond what is explicitly serialised by the asset loader.

**Render thread = main thread.** A dedicated render thread is **not** in scope for this revision and not on the immediate roadmap; the Vulkan migration (Phase 12+) is where multi-threaded command-buffer recording becomes worth the complexity. Until then, treat the renderer as main-thread-only.

## 8. Performance budget

60 FPS hard requirement → 16.6 ms per frame total (CLAUDE.md). The renderer is the single largest slice of that budget. Honest disclosure: most of the per-pass cells are not yet measured — the renderer pre-dates per-pass GPU-timer instrumentation.

Not yet measured — will be filled by the next instrumented capture (target: end of Phase 10.9 audit); tracked as Open Q1 in §15. Provisional pass-budget targets are listed below as a target, not as measurements; cells will gain real numbers as `engine/profiler/gpu_timer.h` is wired into each pass.

| Path | Target | Measured (RX 6600, 1080p) |
|------|--------|----------------------------|
| `beginFrame` (FBO bind + clear) | < 0.05 ms | TBD — Phase 10.9 audit |
| Frustum cull (CPU, ~1k items) | < 0.5 ms | TBD — Phase 10.9 audit |
| CSM update + 4-cascade depth pass | < 2.0 ms | TBD — Phase 10.9 audit |
| Point shadow pass (`MAX_POINT_SHADOW=2` × 6 faces) | < 1.0 ms | TBD — Phase 10.9 audit |
| Opaque scene (forward, ~1k draws, instanced) | < 4.0 ms | TBD — Phase 10.9 audit |
| Skybox + transparent | < 0.5 ms | TBD — Phase 10.9 audit |
| Cloth + dynamic-mesh draws | < 0.5 ms | TBD — Phase 10.9 audit |
| Per-object motion-vector overlay (TAA only) | < 0.5 ms | TBD — Phase 10.9 audit |
| MSAA resolve (4×) | < 0.3 ms | TBD — Phase 10.9 audit |
| SSAO + blur (full res) | < 1.0 ms | TBD — Phase 10.9 audit |
| Bloom mip chain (6 mips, Karis down + tent up) | < 1.0 ms | TBD — Phase 10.9 audit |
| Composite (tonemap + grade + vision filter + fog) | < 0.4 ms | TBD — Phase 10.9 audit |
| TAA resolve (history sample + neighborhood clamp) | < 0.5 ms | TBD — Phase 10.9 audit |
| SMAA (edge → blend-weight → neighborhood) | < 0.6 ms | TBD — Phase 10.9 audit |
| Auto-exposure (luminance mip + PBO readback) | < 0.1 ms | TBD — Phase 10.9 audit |
| Depth reduction (SDSM bounds compute) | < 0.2 ms | TBD — Phase 10.9 audit |
| `endFrame` total | < 4.0 ms | TBD — Phase 10.9 audit |
| **Renderer total per-frame slice** | **< 12 ms** | **TBD — Phase 10.9 audit** |
| One-shot `loadShaders + initFramebuffers` cold start | < 500 ms | TBD — Phase 10.9 audit |
| One-shot `loadSkyboxHDRI` + IBL regen (1024² cubemap) | < 200 ms | TBD — Phase 10.9 audit |
| One-shot `RadiosityBaker::bake` (4 bounces, 4³ grid) | < 5000 ms | TBD — Phase 10.9 audit |
| `pickEntityAt` (single click, sync `glReadPixels`) | < 5 ms | TBD — Phase 10.9 audit |

**Profiler markers / capture points.** GPU debug markers via `glPushDebugGroup` / `glObjectLabel` are **not** currently emitted anywhere in `engine/renderer/` — see §15 Open Q2 (the honest disclosure of this gap) and CODING_STANDARDS §29 (which makes them normative). RenderDoc / apitrace captures currently distinguish passes by FBO-bind boundaries plus shader-program names, which is recoverable but slow to read. The labels below are the proposed naming scheme to use **once the markers are wired**; they are not yet shipped as CPU-side strings, so a grep for these names in the renderer source will return nothing today:

- `"Renderer/Shadow/Cascade <0|1|2|3>"` — per-cascade CSM pass.
- `"Renderer/Shadow/Point <i>/Face <0..5>"` — point shadow per face.
- `"Renderer/Skybox"` · `"Renderer/Opaque"` · `"Renderer/Transparent"` · `"Renderer/Cloth"`.
- `"Renderer/MotionOverlay"` (TAA only).
- `"Renderer/MSAA-Resolve"` · `"Renderer/SSAO"` · `"Renderer/Bloom/Down <mip>"` · `"Renderer/Bloom/Up <mip>"`.
- `"Renderer/Composite"` · `"Renderer/TAA-Resolve"` · `"Renderer/SMAA-Edge|Blend|Neighborhood"`.
- `"Renderer/AutoExposureMip"` · `"Renderer/DepthReduce"`.
- `"Renderer/IBL/Capture|Irradiance|Prefilter|BRDF-LUT"` (one-shot).
- `"Renderer/SHGrid/Probe<i>/Face<j>"` (one-shot per probe per face).

`engine/profiler/gpu_timer.h` already exists; wiring per-pass `GpuTimer` instances + the matching `glPushDebugGroup` call is the same pull-request-sized change.

## 9. Memory

| Aspect | Value |
|--------|-------|
| Allocation pattern | Heap (mostly `std::unique_ptr` from `Renderer::initFramebuffers`) for long-lived GL handles; **per-frame Polymorphic Memory Resource (PMR) arena** of `FRAME_ARENA_SIZE = 2 MB` for scratch allocations (`std::pmr::monotonic_buffer_resource` over a 64-byte-aligned char buffer; reset via `resetFrameAllocator()` each frame; `engine/renderer/renderer.h:797`). Reusable per-frame `std::vector`s (`m_culledItems`, `m_sortedTransparentItems`, `m_shadowCasterItems`, `m_cascadeCulledCasters`, `m_scratchFoliageChunks`, `m_instanceBatches`) keep capacity across frames so the hot path does not heap-alloc. |
| GPU memory (per-frame) | Reusable: scene MSAA FBO `4 × RGBA16F + D24S8` ≈ 1080p × 4 × 12B ≈ 100 MB · resolve FBO `RGBA16F + D24S8` ≈ 25 MB · output FBO `RGBA8` ≈ 8 MB · TAA scene FBO + history ≈ 50 MB · SSAO + blur ≈ 8 MB · SMAA edge + blend ≈ 16 MB · bloom mip chain (`R11F_G11F_B10F`, 6 mips, mip 0 ~1080p) ≈ 12 MB · auto-exposure luminance + 2× PBO ≈ 0.1 MB · CSM 2k² × 4 cascades × D24 ≈ 64 MB · point shadow cubemaps `MAX_POINT=2 × 1024² × 6 × D24` ≈ 36 MB. Sum: ~320 MB at 1080p. |
| GPU memory (scene-load duration) | IBL: irradiance cube 32² × 6 ≈ 0.025 MB · prefilter cube 128² × 6 × 5 mips RGBA16F ≈ 1 MB · BRDF LUT 512² RGBA16F ≈ 2 MB. SH probe grid: 7 RGBA16F 3D textures × resolution³ — for a 16³ grid, ~0.5 MB total. |
| GPU memory (engine-lifetime) | 4 × 1×1 sampler fallback textures (white 2D, black cube, 1-layer 2D-array, 1-voxel 3D) — ~30 bytes total. SMAA area + search LUTs ≈ 0.5 MB. Color-grading LUTs: 33³ RGB16F per preset × N presets — ~0.3 MB per preset. |
| Peak working set (CPU side) | Single-digit MB for renderer-owned data: `m_prevWorldMatrices` (one mat4 per dynamic entity, capped by scene size), instance-batch map + vector pool, the 2 MB frame arena, the reusable cull/transparent/shadow vectors. |
| Ownership | `Renderer` owns every GL handle via either `std::unique_ptr` (`Framebuffer`, `Taa`, `Smaa`, `CascadedShadowMap`, `LightProbeManager`, `SHProbeGrid`, `EnvironmentMap`, `MeshPool`, `IndirectBuffer`, `InstanceBuffer`, `ColorGradingLut`, `Skybox`, `TextRenderer`, `DepthReducer`, `Framebuffer` for resolveDepth / SSAO / contact-shadow / TAA scene) or as a direct member (`Shader m_*Shader`, `GLuint m_bloomTexture` / `m_bloomFbo` / `m_luminanceTexture` / `m_luminancePbo[2]` / `m_ssaoNoiseTexture` / `m_dummyModelSSBO` / `m_fallbackTexture` / `m_fallbackCubemap` / `m_fallbackTexArray` / `m_boneMatrixSSBO` / `m_dummyMorphSSBO` / `m_causticsTexture`). Satellite renderers (`TerrainRenderer`, `FoliageRenderer`, `TreeRenderer`, `WaterRenderer`, `GPUParticleSystem`, `SpriteRenderer`, etc.) are owned by their respective domain `ISystem` — the renderer **does not** own them. |
| Lifetimes | Scene-load (IBL, SH grid, light probes) · engine-lifetime (FBO chain, shaders, SMAA LUTs, sampler fallbacks, color-grading presets) · per-frame (cull / batch vectors, frame arena resets every `beginFrame`). |

No `new` / `delete` in feature code (CODING_STANDARDS §12). Every GL handle is wrapped or paired with a `glDelete*` in the destructor. Move-only RAII for `Shader`, `Texture`, `Mesh`, `DynamicMesh`, `Framebuffer`, `LightProbe`, `PointShadowMap`, `CascadedShadowMap`, `ColorGradingLut`, `WaterFbo`, `Font`, `InstanceBuffer`, etc.

## 10. Error handling

Per CODING_STANDARDS §11 — no exceptions in the steady-state hot path.

| Failure mode | Reported as | Caller's recourse |
|--------------|-------------|-------------------|
| Shader compile / link failed in `loadShaders` | `Logger::error("Failed to load <name> shader")` + `return false` | Engine aborts cold-start; renderer cannot run without shaders. |
| Compute shader load failed (`DepthReducer::init`, GPU particle pipeline) | `Logger::error` + `return false` from the subsystem `init` | Subsystem disables itself; renderer falls back to non-SDSM cascade splits / CPU particles. |
| FBO incomplete after `glCheckNamedFramebufferStatus` | `Logger::error("Framebuffer incomplete, status = 0x...")` | Affected pass binds default FBO; visible pixel corruption signals to the user that something is wrong (better than a silent black frame). |
| Texture / HDRI / model file missing or corrupt | `Logger::warning` + procedural / fallback substitution (default 1×1 white texture, gradient skybox, neutral LUT) | Engine continues; visual artifact flagged in the editor `ValidationPanel`. |
| Sampler unbound at draw time (Mesa AMD `GL_INVALID_OPERATION`) | Caught via `glGetError` after the draw call → `Logger::error` (debug build only) | `SamplerFallback` binds a 1×1 fallback to every declared sampler unit before each draw — see CODING_STANDARDS §21. The error is the *symptom*; the fix is "did you add a sampler to the shader and forget to add the fallback bind?". |
| Light cap exceeded (`addPointLight` past 16, `addSpotLight` past 8) | Return `false` + `Logger::warning` (rate-limited to once per frame) | Caller drops the light; visual cue is that some lights are missing. Future fix is a clustered/forward+ light-list. |
| Per-frame light count change inside `endFrame`'s composite | The composite uses the count captured at the start of the frame; mid-frame mutation is undefined | Don't mutate light state mid-frame; the engine main loop ensures `clearPointLights` / `addPointLight` runs before `renderScene`. |
| `glReadPixels` from `pickEntityAt` returns 0 | Returned as `entityId = 0` (sentinel — "no entity / background") | Caller treats 0 as "miss." |
| Sub-allocation overflow in the 2 MB frame arena | `std::pmr::null_memory_resource` upstream → `std::bad_alloc` | Renderer is sized to never exceed 2 MB of per-frame scratch; an overflow is a programmer error (e.g. an unbounded loop allocating into `m_frameResource`). Treat as fatal and grow the arena if real data demands it. |
| `WindowResizeEvent` fired during `~Renderer` | Cannot happen — destructor unsubscribes via `m_windowResizeSubscription = 0` (AUDIT M9) | n/a |
| Programmer error (null pointer, out-of-range cascade index) | `assert` (debug) / UB (release) | Fix the caller. |
| Out of GPU memory (`glTextureStorage*` / `glBufferStorage` failure) | `glGetError() == GL_OUT_OF_MEMORY` → `Logger::fatal` | Engine aborts; treat as fatal at init. Steady-state allocation is bounded (per-frame arena resets). |

`Result<T, E>` / `std::expected` is **not** yet used in `engine/renderer` — the codebase pre-dates the policy. Migration is on the broader engine-wide list, not a renderer-specific debt.

## 11. Testing

Test files relevant to `engine/renderer/`:

| Concern | Test file | Coverage |
|---------|-----------|----------|
| Camera projection (reverse-Z, culling-projection finite far) | `tests/test_camera.cpp`, `tests/test_camera_mode.cpp`, `tests/test_camera_2d.cpp`, `tests/test_camera_component.cpp` | Public API contract |
| Shadow-map (`ShadowMap` + CSM lifecycle) | `tests/test_shadow_map.cpp` | Public API contract |
| TAA jitter sequence + projection jitter | `tests/test_taa.cpp` | Halton sequence determinism |
| Bloom (Karis combine, 13-tap downsample energy) | `tests/test_bloom_downsample_karis.cpp`, `tests/test_bloom.cpp` | CPU-mirror parity + smoke |
| SSAO kernel determinism | `tests/test_ssao.cpp` | Deterministic kernel + noise tile |
| IBL split-sum (`EnvironmentMap` capture sequence + prefilter loop) | `tests/test_ibl.cpp`, `tests/test_ibl_capture_sequence.cpp` | Bracket contract (mock guard) + smoke |
| SH probe grid (`SHProbeGrid` configuration + projection) | `tests/test_sh_probe_grid.cpp` | Coefficient layout + binding |
| Instanced rendering (`buildInstanceBatches` → `(mesh, material)` grouping) | `tests/test_instanced_rendering.cpp` | Static-mode batch-build contract |
| Skybox (procedural + cubemap load) | `tests/test_skybox.cpp` | Public API contract |
| Color grading (`ColorGradingLut::loadCubeFile` + neutral LUT) | `tests/test_color_grading.cpp` | `.cube` round-trip |
| Color-vision matrix (Viénot 1999) | `tests/test_color_vision_filter.cpp` | Identity for Normal, dichromacy projections |
| Fog (linear / exp / exp² + height + sun-inscatter, accessibility transform) | `tests/test_fog.cpp` | Closed-form math + Quílez integral |
| GPU particle data layout | `tests/test_gpu_particle_system.cpp`, `tests/test_particle_data.cpp` | SSBO struct sizes / behaviour map |
| Sampler fallback lazy-init + caching (Mesa rule) | `tests/test_sampler_fallback.cpp` | Mock `GlTextureCreator` |
| Scoped GL state guards | `tests/test_scoped_blend_state.cpp`, `tests/test_scoped_cull_face.cpp`, `tests/test_scoped_shadow_depth_state.cpp` | Save/restore via mock IO |
| Sprite + tilemap + text + sprite-atlas + sprite-renderer + sprite-animation | `tests/test_sprite_renderer.cpp`, `tests/test_sprite_atlas.cpp`, `tests/test_sprite_panel.cpp`, `tests/test_sprite_animation.cpp`, `tests/test_tilemap.cpp`, `tests/test_text_rendering.cpp` | Atlas JSON-Array load, instance build, oblique shear |
| Texture filtering (anisotropic / trilinear) | `tests/test_texture_filter.cpp` | Public API contract |
| Foliage chunking (cull + LOD selection) | `tests/test_foliage_chunk.cpp` | Public API contract |
| Water surface | `tests/test_water_surface.cpp` | Public API contract (component-side; renderer covered by visual harness) |
| Terrain size cap | `tests/test_terrain_size_caps.cpp` | Heightmap size validation |

**Adding a test for `engine/renderer/`:** drop a new `tests/test_<thing>.cpp` next to its peers, link against `vestige_engine` + `gtest_main` in `tests/CMakeLists.txt` (auto-discovered via `gtest_discover_tests`). Use `Camera`, `Material`, `Mesh` (CPU-side state), and the pure-math helpers (`computeNormalMatrix`, `combineBloomKarisGroups`, `colorVisionMatrix`, `applyFogAccessibilitySettings`, `light_utils.h`) directly without an `Engine` instance — these are the headlessly-unit-testable primitives. GL-bound types (`Renderer`, `Shader`, `Framebuffer`, `Texture`, every `*Renderer`, `EnvironmentMap`, `SHProbeGrid`, `Taa`, `Smaa`) need a GL context — exercise via `engine/testing/visual_test_runner.h` for visual-output verification, or via the **template-IO injection pattern** the `Scoped*`, `SamplerFallback`, `IblCaptureSequence`, and `MotionOverlayPrevWorld` tests use to mock the GL surface.

**Coverage gap:** the full `Renderer::renderScene` / `endFrame` orchestration is not unit-tested headlessly — it requires a GL context. Coverage comes through the visual-test runner and the smoke tests above. Pixel-perfect output is not asserted automatically; visual regressions are caught by the user's eye + `FrameDiagnostics::capture` (PNG + brightness/luminance text report) — see project memory `feedback_visual_test_each_feature.md`.

## 12. Accessibility

`engine/renderer/` is the **terminal consumer of every renderer-side accessibility setting** (CLAUDE.md, project memory `user_accessibility.md`, `feedback_editor_realtime.md`). The user is partially sighted — accessibility is non-negotiable for this engine.

Renderer-side accessibility surfaces (each is wired through one of `engine/core/settings_apply.h`'s sinks; the renderer is the downstream side):

| Setting | Renderer endpoint | Effect |
|---------|-------------------|--------|
| Color-vision filter (protanopia / deuteranopia / tritanopia) | `Renderer::setColorVisionMode` | Final composite multiplies by `colorVisionMatrix(mode)` post-tonemap, pre-gamma. Identity for `Normal`. Reference: Viénot, Brettel, Mollon 1999 (`color_vision_filter.h:43`). |
| High contrast / contrast multiplier | `Material::setContrast` (per-material) + tone-mapper exposure scale | Scenes can elevate contrast for readability without re-authoring; the editor surfaces a per-scene control. |
| Reduce motion | `Renderer::setPostProcessAccessibility` (`PostProcessAccessibilitySettings::reducedMotion`) | Sun-inscatter lobe is dampened (Phase 10 fog accessibility) ; TAA jitter remains on (per-pixel sub-pixel motion is not the source of vestibular discomfort) but motion-blur is gated off. Camera shake: see `engine/core/first_person_controller.h` — handled at the camera, not here. |
| Photosensitive safe mode (max flash α, max strobe Hz, bloom-intensity scale) | `Renderer::setPhotosensitive(enabled, limits)` | Per-frame bloom intensity is clamped at the GLSL uniform-upload site (`renderer.cpp` `limitBloomIntensity()`); coupled flickering point-lights are clamped by `Scene::collectRenderData(photosensitiveEnabled, limits)`. Defaults are conservative (`maxFlashAlpha = 0.25`, `maxStrobeHz = 2.0`, `bloomIntensityScale = 0.6`) per `engine/core/settings.h:210`. |
| Post-process gating (master fog toggle + intensity + reduce-motion sun-inscatter) | `Renderer::setPostProcessAccessibility` | `applyFogAccessibilitySettings` runs each frame between authored state and GPU upload (`fog.cpp`); authored scene-look is preserved. |
| HDR debug mode (false-color luminance) | `Renderer::setDebugMode(1)` | Renders a luminance histogram heat map — diagnostic, not user-facing in shipped builds. |
| Cascade debug visualisation | `Renderer::setCascadeDebug(true)` | Tints each cascade so users debugging shadow seams can see cascade boundaries. |

**Constraint summary for downstream UI (consumed by `engine/editor/` Settings panel + `engine/ui/` accessibility tab):**

- Color: contrast ratio ≥ 4.5:1 for in-scene HUD text; never color-only encoding (the cascade-debug palette is dev-only and is allowed because it is paired with a numeric overlay).
- Motion: reduced-motion respects camera shake, parallax overshoot, sun-inscatter pulse, and motion-blur. TAA sub-pixel jitter is **not** disabled by reduced-motion — sub-pixel motion is the AA source, not the vestibular-discomfort source; if a user does need it off, the answer is `setAntiAliasMode(AntiAliasMode::SMAA)` or `MSAA_4X`, not a TAA jitter-disable knob.
- Photosensitive defaults: never raise without an explicit design-doc revision. The bloom clamp is on the **uploaded** intensity, so a scene-authored 0.10 stays 0.10 (already below the 0.6× cap); the cap kicks in only when authored intensity exceeds the threshold.
- Color-vision filter: applied **post-tonemap, pre-gamma** in `screen_quad.frag` so the simulation runs in the same linear-display-RGB the tonemapped colour occupies (`color_vision_filter.h` rationale).

## 13. Dependencies

| Dependency | Type | Why |
|------------|------|-----|
| `engine/core/event_bus.h` | engine subsystem | Subscribes to `WindowResizeEvent` to resize FBOs in lock-step with the GL viewport. |
| `engine/scene/scene.h` (`SceneRenderData`, `RenderItem`, `ClothRenderItem`) | engine subsystem | The renderer's input format. Renderer only reads; scene fills. |
| `engine/scene/water_surface.h`, `engine/scene/particle_emitter.h` | engine subsystem | Component types consumed by the satellite renderers. |
| `engine/animation/morph_target.h` | engine subsystem | `Vertex` + morph SSBO upload path. |
| `engine/environment/foliage_chunk.h`, `engine/environment/terrain.h` | engine subsystem | Foliage + tree + terrain renderer inputs. |
| `engine/utils/aabb.h` | engine subsystem | Frustum culling primitive. |
| `engine/accessibility/photosensitive_safety.h`, `post_process_accessibility.h` | engine subsystem | `PhotosensitiveLimits` + `PostProcessAccessibilitySettings` accessibility-sink targets. |
| `engine/core/logger.h` | engine subsystem | Diagnostic logging. |
| `<GLFW/glfw3.h>` | external (transitive via `engine/core/window.h`, not direct in renderer code) | Context only — the renderer's only direct interface to GLFW is via `WindowResizeEvent`. |
| `<glad/gl.h>` (vendored at `external/glad/`) | external | OpenGL 4.5 function loader. |
| `<glm/glm.hpp>`, `<glm/gtc/matrix_inverse.hpp>` | external | Math primitives (vec / mat / quat); reverse-Z infinite-far perspective; per-draw normal matrix. |
| `<freetype/...>` (via `external/`) | external | Glyph rasterisation for `Font`. |
| `<stb_image.h>` (vendored at `external/stb/`) | external | Texture file decoding (PNG / JPG / TGA / HDR). |
| `<tinyexr.h>` (vendored at `external/`) | external | EXR HDRI loading. |
| `<nlohmann/json.hpp>` | external | `SpriteAtlas` JSON-Array parser. |
| `<memory>`, `<memory_resource>`, `<unordered_map>`, `<vector>`, `<array>`, `<string>`, `<functional>` | std | Owning pointers, per-frame PMR arena, hash maps, dynamic arrays, fixed arrays for cascade matrices, paths, callbacks for the IBL prefilter loop. |

**Direction.** `engine/renderer` is depended on by `engine/scene` (component types include renderer headers — `MeshRenderer`, `Material`, `Camera` component, light components), `engine/editor` (viewport panels, gizmos, debug-draw), and the domain `ISystem`s in `engine/systems/` (lighting, water, particle, vegetation, terrain). `engine/renderer` itself depends on `engine/core` (event bus, logger), `engine/utils` (AABB), `engine/scene` (`SceneRenderData` types — note: scene includes renderer headers AND renderer reads scene render-data structs; this is a deliberate two-way header coupling that has lived since Phase 4 and is not a smell because `SceneRenderData` is defined in `scene.h` while renderer-side component implementations are in `engine/scene/components/`), `engine/animation` (morph deltas + bone matrices), `engine/environment` (foliage / terrain chunks), `engine/accessibility` (settings struct types). It must **not** depend on `engine/physics`, `engine/audio`, `engine/ui`, `engine/scripting`, `engine/navigation`.

## 14. References

Cited research / authoritative external sources:

- Brian Karis, *Real Shading in Unreal Engine 4* (SIGGRAPH 2013) — split-sum IBL approximation, GGX importance sampling, "fireflies" weight in the bloom downsample. <https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf>
- Jorge Jimenez et al., *Next Generation Post Processing in Call of Duty: Advanced Warfare* (SIGGRAPH 2014) — 13-tap bloom downsample, 5 sample-group weighting (slide 147), tent-filter upsample.
- Inigo Quílez, *Better Fog / Coloured Fog* (2010) — closed-form analytic integral for exponential height fog. <https://iquilezles.org/articles/fog>
- Viénot, Brettel, Mollon (1999), *Digital video colourmaps for checking the legibility of displays by dichromats*, Color Research & Application 24(4):243–252 — color-vision-deficiency simulation matrices used by `color_vision_filter.h`. (IGDA GA-SIG canonical reference.)
- MJP, *A Sampling of Shadow Techniques* (2025) — current-generation comparison of CSM, PCF, VSM, PCSS. <https://therealmjp.github.io/posts/shadow-maps/>
- NVIDIA GPU Gems 3, *Summed-Area Variance Shadow Maps* — SAVSM motivation for VSM-vs-PCF benchmark cited in §3 (40 FPS vs 25 FPS at 1600×1200). <https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-8-summed-area-variance-shadow-maps>
- Bruno Opsenica, *Image Based Lighting with Multiple Scattering* (2025) — energy-conservation extension to the split-sum approximation; informs the `m_iblMultiplierOverride` diagnostic. <https://bruop.github.io/ibl/>
- Technik90, *IBL Optimization Study* (2025-12) — 64-sample-budget GGX prefilter benchmark; relevant when deciding the engine's BRDF-LUT resolution + sample count. <http://technik90.blogspot.com/2025/12/ibl-optimization-study.html>
- Microsoft Win32, *Cascaded Shadow Maps* — canonical CSM algorithm reference. <https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps>
- The Code Corsair, *Temporal AA and the Quest for the Holy Trail* — TAA neighborhood clamp / clip / variance-clip taxonomy used by `Taa::resolve`. <https://www.elopezr.com/temporal-aa-and-the-quest-for-the-holy-trail/>
- Alex Tardif, *Temporal Antialiasing Starter Pack* — per-pixel motion-vector + history-confidence reference. <https://alextardif.com/TAA.html>
- LearnOpenGL, *Specular IBL* — split-sum walkthrough used as the working reference during initial implementation. <https://learnopengl.com/PBR/IBL/Specular-IBL>
- Vulkan Tutorial, *Forward, Forward+, and Deferred — choosing the right path* (2025) — current trade-off framing supporting the engine's "forward + clustered light list when scaling needed" stance vs. early-deferred. <https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Advanced_Topics/Forward_ForwardPlus_Deferred.html>
- Diligent Engine, *Temporal Anti Aliasing* — current-generation TAA implementation reference. <https://diligentgraphics.github.io/docs/db/d24/DiligentFX_PostProcess_TemporalAntiAliasing_README.html>
- KhronosGroup, *KHR_debug* — `glPushDebugGroup` / `glObjectLabel` / `glDebugMessageCallback` reference (CODING_STANDARDS §29). <https://registry.khronos.org/OpenGL/extensions/KHR/KHR_debug.txt>
- Mesa3D RadeonSI driver source — implicit reference for the Mesa AMD draw-time sampler-binding rule (CODING_STANDARDS §21; project memory `feedback_mesa_sampler_binding.md`).

Internal cross-references:

- `CODING_STANDARDS.md` §11 (errors), §12 (memory), §13 (threading), §17 (CPU/GPU placement), §18 (public API), §21 (GLSL — sampler-binding rule, std140 / std430 packing), §24 (OpenGL RAII), §29 (GPU debug markers — currently a debt; see Open Q2), §31 (GL state — `glClipControl` reverse-Z restoration, viewport / scissor at pass entry, blend / depth / stencil restoration).
- `ARCHITECTURE.md` §6 (rendering pipeline), §15 (GPU compute particles).
- `CLAUDE.md` rules 1, 5, 6, 7 (research-first, no workarounds, Formula Workbench, design-time CPU/GPU placement).
- `docs/research/gi_roadmap.md` — current GI roadmap (SH grid + radiosity shipped; SSGI next; hybrid RT later).
- `docs/phases/phase_10_fog_design.md` — fog accessibility transform + composition order.

## 15. Open questions

| # | Question | Owner | Target |
|---|----------|-------|--------|
| 1 | Performance budget §8 cells are pass-target placeholders — wire `engine/profiler/gpu_timer.h` into every named pass and replace TBDs with real measurements on the RX 6600 / 1080p reference scene. | milnet01 | end of Phase 10.9 audit |
| 2 | `glPushDebugGroup` / `glObjectLabel` are not yet emitted by the renderer despite CODING_STANDARDS §29 being normative. Add an RAII helper (`ScopedDebugGroup`) and label every pass per the §8 marker list. Free in release; tooling-critical for RenderDoc reads. | milnet01 | end of Phase 10.9 audit |
| 3 | SMAA path is wired (edge → blend-weight → neighborhood) but lacks depth-aware reprojection — produces a small amount of temporal flicker on disoccluded geometry. Either bring it up to TAA-ish reprojection quality or document SMAA as "spatial only, TAA recommended for moving cameras." | milnet01 | Phase 11 entry |
| 4 | MDI (`m_mdiEnabled`) path is staged behind a default-off flag pending mesh-pool population in `SceneManager`. Land the populator + default `mdiEnabled = true` for static scene content, fall back to the instance-batch path for dynamic / animated meshes. | milnet01 | Phase 11 entry |
| 5 | `computeNormalMatrix` uniform-scale fast path has no dedicated parity test — only implicit visual coverage. Add a unit test that asserts uniform-scale output equals `mat3(model)` to within float tolerance and non-uniform fallback equals `transpose(inverse(mat3(model)))`. | milnet01 | end of Phase 10.9 audit |
| 6 | Spot-light shadow maps: `SpotLight` struct + `addSpotLight` plumbing exist, but the shadow pass currently only handles directional CSM + `MAX_POINT_SHADOW = 2` point lights. Wire spot-light shadows (single-cascade ortho with cone projection) before any scene asset *needs* them — Tabernacle interior is the first candidate. | milnet01 | Phase 11 entry |
| 7 | Contact shadows (`m_contactShadowsEnabled = false`) — the implementation is in-tree but disabled because the screen-space ray-march needs G-buffer normals which forward+ doesn't produce. Either deferred-decide to ship a thin G-buffer (depth + packed normal) for the post-process passes, or delete the contact-shadow code path. | milnet01 | Phase 11 entry |
| 8 | `Result<T, E>` / `std::expected` is not yet used; subsystem `init` returns `bool`, asset loaders return `bool`. Migrate alongside the engine-wide policy adoption. | milnet01 | post-MIT release (Phase 12) |
| 9 | Renderer-thread split: GL context is single-thread; a future Vulkan migration is the natural place to land multi-threaded command-buffer recording. Current renderer is correct as main-thread-only — defer until Vulkan. | milnet01 | triage (Phase 12+) |
| 10 | Forward+ clustered light list: 16 point + 8 spot uniform caps are tight for big interior scenes (Solomon's Temple sanctuary lampstands). Forward+ tiled / clustered light culling is the standard 2025 answer. | milnet01 | Phase 11 entry |

## 16. Spec change log

| Date | Spec version | Author | Change |
|------|--------------|--------|--------|
| 2026-04-28 | 1.0 | milnet01 | Initial spec — `engine/renderer` covering forward+ pipeline, CSM + point shadows, split-sum IBL, SH probe grid + radiosity, post-process chain (TAA / SMAA / MSAA / bloom / SSAO / tonemap / grade / fog / vision filter / photosensitive), satellite renderers (foliage, trees, terrain, water, sprites, particles, text). Foundation since Phase 4; formalised post-Phase 10.9 audit. |
