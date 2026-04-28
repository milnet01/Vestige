# Phase 4: Visual Quality — Design Document

## Goal
Make the engine look good — shadows, better materials, post-processing. By the end of this phase, the demo scene should have realistic lighting with shadows, physically based materials, an environment skybox, and post-processing effects (HDR, bloom, ambient occlusion).

**Milestone:** A visually polished scene with realistic lighting, shadows, and materials.

---

## Current State (End of Phase 3)

The engine currently has:
- **Forward rendering** with a single Blinn-Phong shader
- **Lights:** 1 directional + up to 8 point + 4 spot lights
- **Materials:** Diffuse color, specular color, shininess, optional diffuse texture
- **Mesh:** Position, normal, vertex color, texture coordinates (no tangents)
- **Rendering:** Direct to the default framebuffer (no FBOs, no render-to-texture)
- **No shadows, no HDR, no post-processing**

Everything renders in a single pass: clear screen, draw all objects with lighting, swap buffers.

---

## Implementation Plan

Phase 4 is split into 7 sub-phases (4A through 4G). Each builds on the previous one and results in a working, testable engine at every step.

### Sub-phase 4A: Framebuffer Infrastructure + MSAA

**What it does:** Creates a reusable Framebuffer Object (FBO) class so the engine can render to off-screen textures instead of directly to the screen. This is the foundation for shadows, HDR, bloom, and all post-processing. Also adds MSAA (multisample anti-aliasing) to eliminate jagged edges.

**Why this comes first:** Almost every other feature in Phase 4 needs the ability to render to a texture. Without FBOs, we can't do shadow maps, HDR rendering, bloom, or SSAO.

#### Concepts (plain English)

**Framebuffer Object (FBO):** Normally, OpenGL draws directly to the screen. An FBO lets us draw to a texture in memory instead. Think of it like rendering a photo to film before printing it — we can look at the film and process it before showing the final image.

**MSAA (Multisample Anti-Aliasing):** Without anti-aliasing, diagonal edges of 3D objects look like staircases. MSAA takes multiple samples per pixel at polygon edges and averages them, producing smooth edges. The cost is more memory (each color/depth buffer is multiplied by the sample count) and a small GPU cost.

**Fullscreen Quad:** A rectangle that covers the entire screen. After rendering a scene to an FBO, we draw that FBO's texture onto a fullscreen quad to display it. This is also how post-processing works — render scene to FBO, apply effects to the texture, draw result to screen.

#### New Files

| File | Purpose |
|------|---------|
| `engine/renderer/framebuffer.h` | `Framebuffer` class — wraps OpenGL FBO creation and management |
| `engine/renderer/framebuffer.cpp` | Implementation |
| `engine/renderer/fullscreen_quad.h` | `FullscreenQuad` class — renders a texture to the screen |
| `engine/renderer/fullscreen_quad.cpp` | Implementation |
| `assets/shaders/screen_quad.vert.glsl` | Vertex shader for the fullscreen quad |
| `assets/shaders/screen_quad.frag.glsl` | Fragment shader — initially just passes through the texture |
| `tests/test_framebuffer.cpp` | Unit tests for Framebuffer configuration |

#### Framebuffer Class Design

```cpp
/// Configuration for creating a framebuffer.
struct FramebufferConfig
{
    int width = 1280;
    int height = 720;
    int samples = 1;              // 1 = no MSAA, 4 = 4x MSAA
    bool hasColorAttachment = true;
    bool hasDepthAttachment = true;
    bool isFloatingPoint = false;  // true = GL_RGBA16F (for HDR), false = GL_RGBA8
};

/// Wraps an OpenGL framebuffer object.
class Framebuffer
{
public:
    explicit Framebuffer(const FramebufferConfig& config);
    ~Framebuffer();

    // Non-copyable, movable
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;
    Framebuffer(Framebuffer&& other) noexcept;
    Framebuffer& operator=(Framebuffer&& other) noexcept;

    /// Bind this FBO — subsequent draws go here instead of the screen.
    void bind();

    /// Unbind — return to the default framebuffer (the screen).
    static void unbind();

    /// Resolve a multisampled FBO to a regular FBO (needed for MSAA).
    void resolve(Framebuffer& destination);

    /// Resize the FBO (e.g., when the window resizes).
    void resize(int width, int height);

    /// Bind the color texture for reading (e.g., as input to a post-process shader).
    void bindColorTexture(int textureUnit = 0);

    /// Bind the depth texture for reading (needed for shadow mapping).
    void bindDepthTexture(int textureUnit = 0);

    /// Getters.
    GLuint getId() const;
    int getWidth() const;
    int getHeight() const;
    bool isMultisampled() const;

private:
    void create();
    void cleanup();

    FramebufferConfig m_config;
    GLuint m_fboId = 0;
    GLuint m_colorAttachment = 0;  // Texture or renderbuffer
    GLuint m_depthAttachment = 0;  // Texture or renderbuffer
};
```

#### Rendering Pipeline Change

**Before (Phase 3):**
```
Clear default framebuffer → Draw scene → Swap buffers
```

**After (Sub-phase 4A):**
```
Bind MSAA FBO → Clear → Draw scene → Resolve to regular FBO → Draw fullscreen quad to screen → Swap buffers
```

This adds one extra step (resolve + fullscreen quad) but gives us the ability to insert post-processing between the scene render and the final display.

#### MSAA Strategy

- Use **4x MSAA** (good balance of quality vs. cost for our hardware)
- Create a multisampled FBO for the main scene render
- Create a non-multisampled "resolve" FBO
- After rendering the scene, blit (copy) from multisampled → resolve FBO
- The resolve FBO's texture goes to the fullscreen quad

#### Tests

- `FramebufferConfig` default values are correct
- `Framebuffer` creation with various configs (MSAA, HDR, depth-only)
- Resize updates dimensions correctly
- Multisampled detection works

---

### Sub-phase 4B: Shadow Mapping (Directional Light)

**What it does:** Objects cast shadows from the sun (directional light). The ground, walls, and objects will have shadows projected onto them, adding enormous depth and realism.

**Why this order:** Needs the FBO from 4A. Directional shadows are the simplest form and provide the biggest visual impact.

#### Concepts (plain English)

**Shadow Mapping:** To know if a point is in shadow, we ask: "Can the light see this point?" We render the scene from the light's point of view into a depth-only texture (the "shadow map"). Then, during the normal render, for each pixel we check: "Is this point farther from the light than what the shadow map recorded?" If yes, something is blocking the light — the point is in shadow.

**Depth Map:** A texture where each pixel stores how far the closest surface is from the camera (or light). It's like a height map from the light's perspective.

**Light Space Matrix:** A view + projection matrix from the light's point of view. For a directional light (like the sun), we use an orthographic projection since the light rays are parallel.

**Shadow Acne:** A visual artifact where surfaces incorrectly shadow themselves, causing a zebra-stripe pattern. Fixed with a small depth bias.

**Peter Panning:** If the bias is too large, shadows detach from their objects and "float." We use back-face culling during the shadow pass to minimize this.

**PCF (Percentage Closer Filtering):** Instead of checking one point against the shadow map (which gives hard, pixelated edges), we sample multiple nearby points and average the results. This produces soft shadow edges.

#### New Files

| File | Purpose |
|------|---------|
| `engine/renderer/shadow_map.h` | `ShadowMap` class — manages shadow map FBO and light-space calculation |
| `engine/renderer/shadow_map.cpp` | Implementation |
| `assets/shaders/shadow_depth.vert.glsl` | Vertex shader for the shadow pass (transforms to light space) |
| `assets/shaders/shadow_depth.frag.glsl` | Fragment shader for the shadow pass (empty — depth is written automatically) |
| `tests/test_shadow_map.cpp` | Unit tests |

#### ShadowMap Class Design

```cpp
/// Configuration for shadow mapping.
struct ShadowConfig
{
    int resolution = 2048;        // Shadow map texture size (2048x2048)
    float orthoSize = 20.0f;      // Half-size of the orthographic projection
    float nearPlane = 0.1f;
    float farPlane = 50.0f;
    float biasMin = 0.001f;       // Minimum depth bias
    float biasMax = 0.01f;        // Maximum depth bias (angle-dependent)
    int pcfSamples = 1;           // 0 = hard shadows, 1 = 3x3 PCF, 2 = 5x5 PCF
};

class ShadowMap
{
public:
    explicit ShadowMap(const ShadowConfig& config = ShadowConfig());

    /// Calculate the light-space matrix for a directional light.
    void update(const DirectionalLight& light, const glm::vec3& sceneCenter);

    /// Bind the shadow FBO for the shadow pass.
    void beginShadowPass();

    /// Unbind and return to normal rendering.
    void endShadowPass();

    /// Bind the shadow map texture for reading during the lighting pass.
    void bindShadowTexture(int textureUnit);

    /// Get the light-space matrix (for use as a shader uniform).
    const glm::mat4& getLightSpaceMatrix() const;

    ShadowConfig& getConfig();

private:
    ShadowConfig m_config;
    Framebuffer m_depthFbo;       // Depth-only FBO
    glm::mat4 m_lightSpaceMatrix;
};
```

#### Rendering Pipeline Change

**After (Sub-phase 4B):**
```
1. SHADOW PASS:   Bind shadow FBO → Draw scene depth from light's view
2. SCENE PASS:    Bind MSAA FBO → Draw scene with lighting + shadow sampling
3. DISPLAY:       Resolve → Fullscreen quad → Swap
```

#### Shader Changes

The `blinn_phong.frag.glsl` shader gets new uniforms and a shadow calculation function:

```glsl
// New uniforms
uniform sampler2D u_shadowMap;
uniform mat4 u_lightSpaceMatrix;
uniform float u_shadowBiasMin;
uniform float u_shadowBiasMax;

// New function
float calcShadow(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir)
{
    // Transform to shadow map UV space
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    // Angle-dependent bias to reduce shadow acne
    float bias = max(u_shadowBiasMax * (1.0 - dot(normal, lightDir)), u_shadowBiasMin);

    // PCF: sample a 3x3 grid around the point
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(u_shadowMap, 0);
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            float closestDepth = texture(u_shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += (projCoords.z - bias > closestDepth) ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;

    // No shadow outside the shadow map's far plane
    if (projCoords.z > 1.0)
        shadow = 0.0;

    return shadow;
}
```

The directional light calculation then becomes:
```glsl
float shadow = calcShadow(u_lightSpaceMatrix * vec4(fragPosition, 1.0), normal, lightDir);
vec3 result = ambient + (1.0 - shadow) * (diffuse + specular);
```

#### Tests

- Light-space matrix calculation is correct for known light directions
- Shadow config defaults are valid
- FBO is depth-only (no color attachment)

---

### Sub-phase 4C: Point Light Shadows (Omnidirectional)

**What it does:** Point lights (like candles, lamps) also cast shadows in all directions. This is critical for the Tabernacle — the golden lampstand should cast shadows of all surrounding objects.

#### Concepts (plain English)

**Omnidirectional Shadow Map:** A point light shines in all directions, so we need a shadow map that covers 360 degrees. We use a **cubemap** — six square textures arranged like the faces of a cube, one for each direction (+X, -X, +Y, -Y, +Z, -Z).

**Cubemap:** Imagine you're standing inside a box and you photograph each wall, the ceiling, and the floor. Those six photos together capture every direction. A cubemap does this with textures. To look up a direction, OpenGL picks the right face and the right pixel automatically.

**Geometry Shader:** Normally we draw the scene 6 times (once per cubemap face). With a geometry shader, we can draw once and have the GPU send each triangle to all 6 faces simultaneously, saving CPU overhead. However, this has mixed performance — we'll start with the 6-pass approach (simpler and often faster on modern GPUs).

**Depth Cubemap:** Instead of storing color, each cubemap face stores the distance from the light to the nearest surface. During the main render, we compare each fragment's distance to the light against the stored depth.

#### New Files

| File | Purpose |
|------|---------|
| `engine/renderer/point_shadow_map.h` | `PointShadowMap` class — manages cubemap shadow for a point light |
| `engine/renderer/point_shadow_map.cpp` | Implementation |
| `assets/shaders/point_shadow_depth.vert.glsl` | Vertex shader for point shadow pass |
| `assets/shaders/point_shadow_depth.frag.glsl` | Fragment shader — writes linear depth |

#### Key Design Decisions

- **Resolution:** 1024x1024 per face (lower than directional since there are up to 8 point lights)
- **Render strategy:** 6 passes per light (one per cubemap face), not geometry shader
- **Limit:** Only the closest 2 point lights to the camera get shadow maps (performance budget)
- **Far plane:** Per-light, based on the light's attenuation radius

#### Shader Changes

The fragment shader gets a cubemap sampler array and a point shadow function:

```glsl
uniform samplerCube u_pointShadowMaps[MAX_SHADOW_CASTING_POINT_LIGHTS];
uniform float u_pointLightFarPlane[MAX_SHADOW_CASTING_POINT_LIGHTS];

float calcPointShadow(vec3 fragPos, vec3 lightPos, int shadowIndex, float farPlane)
{
    vec3 fragToLight = fragPos - lightPos;
    float closestDepth = texture(u_pointShadowMaps[shadowIndex], fragToLight).r;
    closestDepth *= farPlane;  // Convert from [0,1] back to world distance
    float currentDepth = length(fragToLight);
    float bias = 0.05;
    return (currentDepth - bias > closestDepth) ? 1.0 : 0.0;
}
```

---

### Sub-phase 4D: Normal Mapping

**What it does:** Adds surface detail (bumps, grooves, cracks) to objects without adding more geometry. A flat wall can look like rough stone. A wooden surface can show grain. This is essential for the Tabernacle — gold hammering, wood grain, fabric weave.

#### Concepts (plain English)

**Normal Map:** A texture where each pixel stores a direction (a normal vector) encoded as an RGB color. Blue-ish pixels = surface pointing straight out; red/green tints = surface tilted left/right/up/down. When the lighting shader reads this texture, it uses the stored direction instead of the mesh's flat normal, creating the illusion of surface detail.

**Tangent Space:** Normals in a normal map are stored relative to the surface, not the world. To use them, we need a coordinate system at each vertex that describes the surface's orientation: the tangent (along the texture U axis), the bitangent (along V), and the normal (perpendicular). Together they form the **TBN matrix**.

**Tangent and Bitangent:** Think of a tile on a wall. The tangent points along the horizontal direction of the tile's texture. The bitangent points along the vertical. The normal points outward from the wall. These three vectors transform the normal map's directions into world space.

#### Modified Files

| File | Change |
|------|--------|
| `engine/renderer/mesh.h` | Add `tangent` and `bitangent` to `Vertex` struct |
| `engine/renderer/mesh.cpp` | Calculate tangents during mesh upload; update vertex attribute layout |
| `engine/renderer/material.h` | Add `m_normalTexture` (optional normal map) |
| `engine/renderer/material.cpp` | Normal map getter/setter |
| `assets/shaders/blinn_phong.vert.glsl` | Pass TBN matrix to fragment shader |
| `assets/shaders/blinn_phong.frag.glsl` | Sample normal map and transform to world space |

#### New Files

| File | Purpose |
|------|---------|
| (none — this integrates into existing files) | |

#### Vertex Struct Change

```cpp
struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 texCoord;
    glm::vec3 tangent;     // NEW
    glm::vec3 bitangent;   // NEW
};
```

Vertex attribute layout goes from 4 attributes (locations 0-3) to 6 (locations 0-5).

#### Tangent Calculation

For each triangle, compute tangent/bitangent from the edge vectors and UV differences:

```
edge1 = v1.position - v0.position
edge2 = v2.position - v0.position
deltaUV1 = v1.texCoord - v0.texCoord
deltaUV2 = v2.texCoord - v0.texCoord

f = 1.0 / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y)
tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x)
tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y)
tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z)
```

This is done once during mesh loading, not every frame.

#### Shader Changes

Vertex shader outputs the TBN matrix:
```glsl
in vec3 tangent;    // location 4
in vec3 bitangent;  // location 5

out mat3 TBN;

void main()
{
    vec3 T = normalize(mat3(u_model) * tangent);
    vec3 B = normalize(mat3(u_model) * bitangent);
    vec3 N = normalize(mat3(u_normalMatrix) * normal);
    TBN = mat3(T, B, N);
    // ... rest unchanged
}
```

Fragment shader uses TBN to transform sampled normal:
```glsl
uniform sampler2D u_normalMap;
uniform bool u_hasNormalMap;

vec3 getNormal()
{
    if (u_hasNormalMap)
    {
        vec3 n = texture(u_normalMap, texCoord).rgb;
        n = n * 2.0 - 1.0;  // Convert from [0,1] to [-1,1]
        return normalize(TBN * n);
    }
    return normalize(fragNormal);
}
```

---

### Sub-phase 4E: Skybox + Environment Mapping

**What it does:** Adds a sky/environment background to the scene. Instead of a flat color behind everything, the player sees a sky, horizon, and environment. This also provides environment reflections for shiny materials.

#### Concepts (plain English)

**Skybox:** A huge cube surrounding the entire scene, with a different sky/environment photo on each face. The camera is always at the center of the skybox, so it feels infinitely far away. You draw it after the scene with depth testing set so it only shows through empty pixels.

**Cubemap Texture:** The same concept as point light shadow cubemaps, but storing color instead of depth. Six images (right, left, top, bottom, front, back) stitched into one texture.

**Environment Mapping:** Using the skybox cubemap to add reflections to shiny objects. For each pixel, calculate the reflection direction (based on the camera angle and surface normal), then look up that direction in the cubemap. This gives a rough approximation of what the surface would reflect.

#### New Files

| File | Purpose |
|------|---------|
| `engine/renderer/skybox.h` | `Skybox` class — loads cubemap and renders sky |
| `engine/renderer/skybox.cpp` | Implementation |
| `assets/shaders/skybox.vert.glsl` | Vertex shader — positions skybox around camera |
| `assets/shaders/skybox.frag.glsl` | Fragment shader — samples cubemap |

#### Skybox Class Design

```cpp
class Skybox
{
public:
    /// Load skybox from 6 image files (right, left, top, bottom, front, back).
    bool loadFromFiles(const std::array<std::string, 6>& facePaths);

    /// Render the skybox (call after scene, before post-processing).
    void render(const glm::mat4& view, const glm::mat4& projection);

    /// Bind the cubemap texture (for environment reflections in materials).
    void bindCubemap(int textureUnit);

    bool isLoaded() const;

private:
    GLuint m_cubemapTexture = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    Shader m_shader;
};
```

#### Rendering Order

```
1. Shadow pass(es)
2. Bind MSAA FBO → Draw scene
3. Draw skybox (depth test <=, depth write off)
4. Resolve → Post-processing → Swap
```

The skybox is drawn last in the scene pass with `glDepthFunc(GL_LEQUAL)` so it only fills pixels that weren't drawn by scene geometry. Its vertex shader strips the translation from the view matrix so it always surrounds the camera.

---

### Sub-phase 4F: HDR Rendering, Tone Mapping, and Bloom

**What it does:** Switches the entire rendering pipeline to High Dynamic Range, allowing light values above 1.0 (pure white). This enables bright lights that actually glow, realistic light falloff, and the bloom effect (bright areas bleeding light into surrounding pixels).

#### Concepts (plain English)

**HDR (High Dynamic Range):** Normally, colors are clamped to the range 0.0 to 1.0 (black to white). In real life, a candle flame is thousands of times brighter than a dimly lit wall. HDR lets us store those big numbers. We render the scene into a floating-point texture (16 bits per channel instead of 8), keeping the full range.

**Tone Mapping:** We can't display HDR values on a regular monitor (monitors are LDR). Tone mapping compresses the wide HDR range back to 0-1 in a way that preserves detail in both dark and bright areas. Think of it like a photographer adjusting exposure — you want to see both the shadows and the bright sky.

**ACES Tone Mapping:** A specific tone mapping curve used in film and games. It gives a pleasing filmic look — shadows are slightly lifted, midtones are natural, highlights roll off smoothly into white.

**Bloom:** Bright areas of the image "bleed" light into surrounding pixels, simulating how real cameras and eyes perceive very bright light. Implementation: extract bright pixels from the HDR image, blur them heavily, then add the blur back on top of the original image.

**Gaussian Blur:** A blur effect where each pixel is averaged with its neighbors, weighted by a bell curve. We do this in two passes (horizontal then vertical) for efficiency — this is called "separable blur" and turns an O(n^2) operation into O(2n).

#### New Files

| File | Purpose |
|------|---------|
| `assets/shaders/screen_quad_hdr.frag.glsl` | HDR tone mapping + gamma correction shader |
| `assets/shaders/bloom_extract.frag.glsl` | Bright-pixel extraction shader |
| `assets/shaders/bloom_blur.frag.glsl` | Gaussian blur shader (horizontal/vertical) |

#### Pipeline Change

This is the most significant pipeline restructure. The scene FBO becomes floating-point:

```
1. Shadow pass(es)
2. Bind HDR MSAA FBO → Draw scene (all lighting in HDR)
3. Draw skybox
4. Resolve MSAA → HDR resolve FBO
5. BLOOM:
   a. Extract bright pixels (threshold > 1.0) → Bloom FBO
   b. Ping-pong Gaussian blur (horizontal → vertical, repeated ~5 times)
   c. Combine blurred bloom with HDR scene
6. Tone mapping (ACES) + gamma correction → Screen
```

#### Tone Mapping Shader

```glsl
// ACES filmic tone mapping
vec3 acesToneMap(vec3 color)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 hdrColor = texture(u_sceneTexture, texCoord).rgb;
    vec3 bloomColor = texture(u_bloomTexture, texCoord).rgb;

    hdrColor += bloomColor;  // Add bloom contribution
    vec3 mapped = acesToneMap(hdrColor * u_exposure);
    mapped = pow(mapped, vec3(1.0 / 2.2));  // Gamma correction

    fragColor = vec4(mapped, 1.0);
}
```

#### Bloom Implementation Details

1. **Extract:** Render fullscreen quad with HDR scene as input. Shader writes pixels where brightness > threshold (default 1.0) to a smaller (half-resolution) FBO.
2. **Blur:** Ping-pong between two half-resolution FBOs, alternating horizontal and vertical Gaussian blur. 5 iterations = 10 blur passes.
3. **Combine:** During tone mapping, add blurred bloom texture to the HDR scene before tone mapping.

Half-resolution bloom saves significant GPU cost with barely perceptible quality loss.

---

### Sub-phase 4G: PBR Materials (Physically Based Rendering)

**What it does:** Replaces the Blinn-Phong lighting model with a physically based one. Materials are defined by "metallic" and "roughness" values (plus albedo color), matching how real-world surfaces behave. This is the standard in modern game engines (Unreal, Unity, Godot all use PBR).

**Why last in this phase:** PBR is a significant shader rewrite. Having shadows, normal maps, HDR, and bloom in place first means the PBR shader integrates with a mature pipeline. PBR also benefits enormously from HDR — its wide-range lighting calculations look wrong when clamped to LDR.

#### Concepts (plain English)

**PBR (Physically Based Rendering):** Instead of arbitrary "diffuse/specular/shininess" values, materials are described by real physical properties:
- **Albedo:** Base color of the surface (like diffuse, but without baked-in shading)
- **Metallic:** Is the surface metal (1.0) or non-metal/dielectric (0.0)? Metals reflect their own color; non-metals reflect white
- **Roughness:** How smooth is the surface? 0.0 = mirror, 1.0 = chalk. Rough surfaces scatter light widely; smooth surfaces concentrate it into a tight highlight
- **AO (Ambient Occlusion):** How much ambient light reaches this point? Crevices and corners are darker

**Cook-Torrance BRDF:** The math behind PBR. It models how light bounces off a surface using three functions:
- **Normal Distribution Function (NDF):** How many microscopic surface facets are aligned to reflect light toward the camera? (Determines highlight shape)
- **Geometry Function:** How much light is blocked by microscopic surface roughness? (Darkens at glancing angles)
- **Fresnel Equation:** Surfaces reflect more light at glancing angles — everything becomes a mirror if you look at it edge-on

**Metallic-Roughness Workflow:** The most common PBR workflow (used by glTF, Unreal, Substance). Materials have an albedo map, a metallic map, a roughness map, a normal map, and optionally an AO map. The metallic and roughness values are often packed into one texture (metallic in blue channel, roughness in green).

#### Modified Files

| File | Change |
|------|--------|
| `engine/renderer/material.h` | Replace Blinn-Phong properties with PBR properties |
| `engine/renderer/material.cpp` | PBR property defaults and setters |
| `engine/renderer/renderer.cpp` | Upload PBR uniforms instead of Blinn-Phong uniforms |
| `assets/shaders/blinn_phong.vert.glsl` | Renamed to `pbr.vert.glsl` (minimal changes) |
| `assets/shaders/blinn_phong.frag.glsl` | Replaced with `pbr.frag.glsl` (full rewrite) |

#### New Files

| File | Purpose |
|------|---------|
| `assets/shaders/pbr.vert.glsl` | PBR vertex shader (same as Blinn-Phong + TBN) |
| `assets/shaders/pbr.frag.glsl` | PBR fragment shader with Cook-Torrance BRDF |

#### Material Class Changes

```cpp
class Material
{
public:
    // PBR properties
    void setAlbedo(const glm::vec3& albedo);
    void setMetallic(float metallic);
    void setRoughness(float roughness);
    void setAo(float ao);

    // PBR texture maps
    void setAlbedoMap(std::shared_ptr<Texture> texture);
    void setNormalMap(std::shared_ptr<Texture> texture);
    void setMetallicMap(std::shared_ptr<Texture> texture);
    void setRoughnessMap(std::shared_ptr<Texture> texture);
    void setAoMap(std::shared_ptr<Texture> texture);

    // Getters...
    glm::vec3 getAlbedo() const;
    float getMetallic() const;
    float getRoughness() const;
    float getAo() const;

private:
    glm::vec3 m_albedo = glm::vec3(0.8f);
    float m_metallic = 0.0f;     // Non-metal by default
    float m_roughness = 0.5f;    // Medium roughness
    float m_ao = 1.0f;           // Fully lit by default

    std::shared_ptr<Texture> m_albedoMap;
    std::shared_ptr<Texture> m_normalMap;
    std::shared_ptr<Texture> m_metallicMap;
    std::shared_ptr<Texture> m_roughnessMap;
    std::shared_ptr<Texture> m_aoMap;
};
```

#### PBR Shader Core (Cook-Torrance)

The fragment shader implements the full Cook-Torrance reflectance model:

```glsl
// Normal Distribution Function — GGX/Trowbridge-Reitz
float distributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

// Geometry Function — Schlick-GGX
float geometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Combined geometry for view and light directions
float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    return geometrySchlickGGX(max(dot(N, V), 0.0), roughness)
         * geometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

// Fresnel — Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
```

The per-light calculation becomes:
```glsl
vec3 calcPBRLight(vec3 lightDir, vec3 lightRadiance, vec3 N, vec3 V,
                  vec3 albedo, float metallic, float roughness, vec3 F0)
{
    vec3 H = normalize(V + lightDir);

    float NDF = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, lightDir, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    // Specular (Cook-Torrance)
    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, lightDir), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;

    // Diffuse (energy conservation: what isn't reflected is diffused)
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);  // Metals have no diffuse

    float NdotL = max(dot(N, lightDir), 0.0);
    return (kD * albedo / PI + specular) * lightRadiance * NdotL;
}
```

#### Backward Compatibility

The old demo scene's material properties (diffuse color, specular, shininess) will be migrated:
- `diffuse` → `albedo`
- `shininess` low → `roughness` high, `shininess` high → `roughness` low
- Everything non-metallic by default

---

### Sub-phase 4H: glTF Model Loading

**What it does:** Adds support for loading glTF 2.0 models (`.glb`/`.gltf`), the modern standard 3D format. glTF natively supports PBR materials, so models from Blender, Sketchfab, etc. will import with correct metallic/roughness/normal maps automatically.

#### Concepts (plain English)

**glTF (GL Transmission Format):** Created by the Khronos Group (who also make OpenGL). It's like "JPEG for 3D" — a universal, efficient format. Stores meshes, materials (PBR), textures, animations, and scene hierarchy in one file. `.gltf` is JSON + separate binary/image files; `.glb` is everything packed into one binary file.

**Why glTF over OBJ?** OBJ doesn't support PBR materials, has no standard for normal maps, and can't store animations or scene hierarchy. glTF supports everything we need for the Tabernacle project.

#### Implementation Strategy

We already use **Assimp** for OBJ loading. Assimp also supports glTF 2.0, so we extend the existing `ObjLoader` into a more general `ModelLoader` class.

#### Modified Files

| File | Change |
|------|--------|
| `engine/resource/obj_loader.h` | Rename to `model_loader.h`, support glTF + OBJ |
| `engine/resource/obj_loader.cpp` | Rename to `model_loader.cpp`, extract PBR materials from glTF |
| `engine/resource/resource_manager.h` | Update to use `ModelLoader` |
| `engine/resource/resource_manager.cpp` | Load glTF models and their embedded textures/materials |

#### Material Extraction from glTF

When loading a glTF model, for each material:
1. Read `baseColorFactor` → `albedo`
2. Read `metallicFactor` → `metallic`
3. Read `roughnessFactor` → `roughness`
4. Read `baseColorTexture` → `albedoMap`
5. Read `metallicRoughnessTexture` → split into `metallicMap` + `roughnessMap`
6. Read `normalTexture` → `normalMap`
7. Read `occlusionTexture` → `aoMap`

---

### Sub-phase 4I: SSAO (Screen-Space Ambient Occlusion)

**What it does:** Darkens creases, corners, and areas where surfaces meet — simulating how ambient light gets blocked in tight spaces. This adds subtle but important depth to the image. Corners of rooms appear darker, crevices in objects look deeper, and the overall scene gains a sense of volume.

#### Concepts (plain English)

**Ambient Occlusion (AO):** In real life, a corner of a room receives less indirect light than the middle of a wall because the surrounding surfaces block some of the incoming light. AO simulates this darkening effect.

**Screen-Space AO (SSAO):** Instead of calculating AO in 3D (which is very expensive), we do it in 2D using only the depth buffer and normals. For each pixel, we sample nearby points and check how many of them are "inside" geometry (behind the surface). More occluded neighbors = darker pixel.

**Kernel Samples:** We generate a set of random 3D sample points inside a hemisphere oriented along each pixel's surface normal. We project these points onto the depth buffer and check if they're occluded.

**Noise Texture:** To avoid banding artifacts from using the same sample pattern everywhere, we use a small random noise texture (4x4) that rotates the sample hemisphere per-pixel. The noise is then removed with a blur pass.

#### New Files

| File | Purpose |
|------|---------|
| `engine/renderer/ssao.h` | `SSAO` class — manages SSAO generation |
| `engine/renderer/ssao.cpp` | Implementation — kernel generation, FBO setup |
| `assets/shaders/ssao.vert.glsl` | Simple fullscreen quad vertex shader |
| `assets/shaders/ssao.frag.glsl` | SSAO calculation shader |
| `assets/shaders/ssao_blur.frag.glsl` | Blur pass to smooth the SSAO result |

#### SSAO Class Design

```cpp
struct SSAOConfig
{
    int kernelSize = 32;          // Number of sample points
    float radius = 0.5f;          // Sample hemisphere radius in world units
    float bias = 0.025f;          // Prevents self-occlusion on flat surfaces
    float power = 2.0f;           // Controls occlusion intensity
};

class SSAO
{
public:
    explicit SSAO(const SSAOConfig& config = SSAOConfig());

    /// Initialize (generate kernel, noise texture, FBOs).
    void initialize(int width, int height);

    /// Run the SSAO pass.
    /// Requires the scene's depth and normal textures as input.
    void compute(GLuint depthTexture, GLuint normalTexture,
                 const glm::mat4& projection, const glm::mat4& view);

    /// Bind the SSAO result texture for use in the lighting pass.
    void bindResult(int textureUnit);

    void resize(int width, int height);

    SSAOConfig& getConfig();

private:
    void generateKernel();
    void generateNoiseTexture();

    SSAOConfig m_config;
    std::vector<glm::vec3> m_kernel;      // Sample points
    GLuint m_noiseTexture = 0;             // 4x4 random rotation texture
    Framebuffer m_ssaoFbo;                 // Raw SSAO output
    Framebuffer m_blurFbo;                 // Blurred SSAO output
    Shader m_ssaoShader;
    Shader m_blurShader;
};
```

#### Pipeline Integration

SSAO needs the scene's depth buffer and world-space normals. We'll write normals to a second color attachment during the scene pass (or reconstruct them from depth, but explicit normals are higher quality).

```
1. Shadow pass(es)
2. Scene pass → HDR color + depth + normals
3. Skybox
4. SSAO pass (reads depth + normals) → AO texture
5. SSAO blur
6. Bloom extract + blur
7. Final composite: HDR color * AO → tone map + bloom → screen
```

The AO value multiplies ambient lighting in the PBR shader:
```glsl
vec3 ambient = vec3(0.03) * albedo * ao * ssaoValue;
```

---

## Final Rendering Pipeline (End of Phase 4)

```
┌──────────────────────────────────────────────────┐
│ 1. SHADOW PASSES                                 │
│    a. Directional light → 2048x2048 depth map    │
│    b. Point lights (x2) → 1024x1024 cubemaps     │
├──────────────────────────────────────────────────┤
│ 2. SCENE PASS (HDR MSAA FBO)                     │
│    - PBR lighting with shadows + normal maps     │
│    - Output: HDR color + depth + normals         │
├──────────────────────────────────────────────────┤
│ 3. SKYBOX                                        │
│    - Rendered into the same HDR FBO              │
├──────────────────────────────────────────────────┤
│ 4. RESOLVE MSAA → regular HDR FBO               │
├──────────────────────────────────────────────────┤
│ 5. SSAO                                          │
│    a. Compute AO from depth + normals            │
│    b. Blur AO texture                            │
├──────────────────────────────────────────────────┤
│ 6. BLOOM                                         │
│    a. Extract bright pixels (> 1.0)              │
│    b. Gaussian blur (5 iterations, ping-pong)    │
├──────────────────────────────────────────────────┤
│ 7. FINAL COMPOSITE → Default framebuffer         │
│    - Combine: scene * AO + bloom                 │
│    - ACES tone mapping                           │
│    - Gamma correction (sRGB)                     │
│    - Output to screen                            │
└──────────────────────────────────────────────────┘
```

---

## Performance Budget

Target: **60 FPS minimum** on AMD RX 6600 at 1280x720.

| Pass | Estimated Cost | Notes |
|------|---------------|-------|
| Shadow (directional) | ~0.5ms | Single 2048x2048 depth pass |
| Shadow (2 point lights) | ~1.5ms | 12 cubemap face renders at 1024x1024 |
| Scene pass | ~2.0ms | PBR lighting, normal maps, shadow sampling |
| Skybox | ~0.1ms | Single draw call, simple shader |
| MSAA resolve | ~0.2ms | Hardware blit |
| SSAO + blur | ~1.0ms | Half-resolution, 32 samples |
| Bloom (extract + blur) | ~0.5ms | Half-resolution, 5 iterations |
| Tone mapping + composite | ~0.1ms | Single fullscreen quad |
| **Total** | **~5.9ms** | **~170 FPS headroom** |

The RX 6600 with RDNA2 architecture handles these operations very efficiently. We have substantial headroom above the 60 FPS target (16.67ms budget).

---

## Implementation Order Summary

| Sub-phase | Feature | Dependencies | New Files |
|-----------|---------|-------------|-----------|
| **4A** | Framebuffer + MSAA | None | 4 source + 2 shader + 1 test |
| **4B** | Directional shadows | 4A | 2 source + 2 shader + 1 test |
| **4C** | Point light shadows | 4A, 4B | 2 source + 2 shader |
| **4D** | Normal mapping | None (but benefits from 4B) | Modified existing files only |
| **4E** | Skybox | 4A | 2 source + 2 shader |
| **4F** | HDR + tone mapping + bloom | 4A | 3 shaders |
| **4G** | PBR materials | 4D, 4F | 2 shaders, modified existing |
| **4H** | glTF loading | 4G | Modified existing files |
| **4I** | SSAO | 4A, 4F | 2 source + 3 shader |

Each sub-phase produces a working engine build. We can test and iterate at every step.

---

## Demo Scene Updates

The demo scene will be updated progressively:
- **4A:** Same scene, but rendered through FBO pipeline with MSAA (smoother edges)
- **4B:** Sun casts shadows on the ground and between cubes
- **4C:** Closest point lights cast shadows
- **4D:** Stone texture on ground with normal map (visible bumps)
- **4E:** Sky visible in the background
- **4F:** Bright lights actually glow; bloom halos around the cubes closest to point lights
- **4G:** Cubes become PBR materials (gold, stone, wood, metal)
- **4H:** Replace cubes with glTF models
- **4I:** Corners where cubes meet the floor appear darker and more grounded

---

## Questions Before Implementation

1. **Starting skybox:** Should I find/generate a simple sky cubemap for testing, or use a solid gradient sky initially?
2. **PBR test materials:** When we reach 4G, should the demo cubes showcase different materials (gold, stone, bronze, fabric — matching Tabernacle materials)?
3. **MSAA sample count:** 4x is the sweet spot for quality/cost. Want to go higher (8x) or is 4x good?
4. **Shadow resolution:** 2048x2048 for directional, 1024x1024 per cubemap face. These are standard — any preference for higher/lower?
