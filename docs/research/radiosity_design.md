# Radiosity Design Document

**Date:** 2026-03-30
**Status:** Implemented. `engine/renderer/radiosity_baker.{h,cpp}` ships the iterative gathering bake described here (2–3 bounce convergence, ~12 s on RX 6600). This document is retained as the original design record.
**Based on:** gi_roadmap.md, sh_probe_grid_design.md, web research
**Engine:** Vestige (C++17, OpenGL 4.5)

---

## Problem

The SH probe grid currently captures ambient lighting by rendering cubemaps at each probe position. This only captures **direct light** — there is no light bounce. In enclosed spaces like the Tabernacle tent interior, this means:

- Areas not directly lit by any light source are pitch black
- Light entering through the doorway does not bounce off interior walls
- Gold surfaces on the Menorah and Ark do not reflect warm light onto nearby walls
- The space feels artificially dark despite being lit by the Menorah and having a large doorway

Radiosity solves this by simulating multi-bounce diffuse light transport and injecting the results into the existing SH probe grid.

---

## References

### Academic / Books
- Cohen & Wallace (1993), *Radiosity and Realistic Image Synthesis*
- Ramamoorthi & Hanrahan (2001), *Efficient Representation for Irradiance Environment Maps*

### GPU Implementation
- GPU Gems 2, Chapter 39: *Global Illumination Using Progressive Refinement Radiosity* — https://developer.nvidia.com/gpugems/gpugems2/part-v-image-oriented-computing/chapter-39-global-illumination-using-progressive
- MJP, *Radiosity DX11 Style* — https://therealmjp.github.io/posts/radiosity-dx11-style/
- TamasKormendi, *opengl-radiosity-tutorial* (C++17, OpenGL 4.5) — https://github.com/TamasKormendi/opengl-radiosity-tutorial

### Light Leak Prevention
- DDGI (Majercik et al., JCGT 2019) — https://www.jcgt.org/published/0008/02/01/paper-lowres.pdf
- Morgan McGuire, *DDGI Overview* — https://morgan3d.github.io/articles/2019-04-01-ddgi/overview.html

### Other
- lightmapper.h (single-header C radiosity baker) — https://github.com/ands/lightmapper
- Hugo Elias Radiosity Tutorial (mirror) — https://www.jmeiners.com/Hugo-Elias-Radiosity/

---

## Architecture Overview

The radiosity system has four stages, all running at scene load time (offline bake):

```
1. Patch Creation     — Discretize scene surfaces into radiosity patches
2. Radiosity Solve    — Progressive refinement with GPU hemicubes
3. SH Probe Injection — Render radiosity-lit patches into cubemaps at probe positions,
                         project to L2 SH
4. Upload             — Store updated SH data in the existing 3D textures
```

### New Files

| File | Purpose |
|------|---------|
| `engine/renderer/radiosity_baker.h` | `RadiosityBaker` class declaration |
| `engine/renderer/radiosity_baker.cpp` | Full radiosity bake implementation |
| `assets/shaders/radiosity_id.vert.glsl` | Hemicube vertex shader (patch ID output) |
| `assets/shaders/radiosity_id.frag.glsl` | Hemicube fragment shader (writes patch ID) |
| `assets/shaders/radiosity_viz.vert.glsl` | SH injection vertex shader (radiosity color output) |
| `assets/shaders/radiosity_viz.frag.glsl` | SH injection fragment shader (writes patch radiosity color) |

### Modified Files

| File | Change |
|------|--------|
| `engine/renderer/sh_probe_grid.h/cpp` | Add `bakeRadiosity()` method that owns the full pipeline |
| `engine/core/engine.cpp` | Call `bakeRadiosity()` after initial SH capture |
| `engine/renderer/renderer.h/cpp` | Expose scene render data for hemicube passes |

---

## Stage 1: Patch Creation

### What is a Patch

A radiosity patch is a small surface element with:
- **Position** (center point in world space)
- **Normal** (surface orientation)
- **Area** (in square meters)
- **Albedo** (RGB reflectance from PBR material, 0-1)
- **Emission** (RGB, non-zero only for light-emitting surfaces)
- **Unique ID** (integer, for hemicube rendering)

### Triangle Subdivision

Scene triangles are subdivided to a target patch size of **0.5m** world-space edge length:

```
For each mesh entity in the scene:
    world_transform = entity.getWorldTransform()
    For each triangle (v0, v1, v2):
        Transform vertices to world space
        If max_edge_length > TARGET_PATCH_SIZE:
            Subdivide recursively (midpoint subdivision)
        Create patch from triangle:
            position = centroid
            normal = cross(v1-v0, v2-v0).normalized
            area = 0.5 * length(cross(v1-v0, v2-v0))
            albedo = material.getAlbedo()
            emission = material.getEmissive()
```

For a triangle with one edge > target size, midpoint subdivision splits it into 4 sub-triangles. This recurse until all edges are under the target.

### Patch Geometry Storage

Each patch also stores its 3 vertices (in world space) for hemicube rendering. We need to render the patches as geometry with their unique IDs.

### Expected Patch Count

For the Tabernacle courtyard scene (~300 entities):
- Ground plane: ~2000m^2 surface, but we only need patches within the courtyard (~1000m^2 at 0.5m = ~4000 patches)
- Walls, tent fabric, furniture: ~2000-4000 patches
- **Total estimate: 5,000-10,000 patches**

### Data Structure

```cpp
struct RadiosityPatch
{
    glm::vec3 vertices[3];  // World-space triangle
    glm::vec3 center;       // Centroid
    glm::vec3 normal;       // Surface normal
    float area;             // Surface area (m^2)
    glm::vec3 albedo;       // Material reflectance (RGB)
    glm::vec3 emission;     // Material emission (RGB)
    glm::vec3 accumulated;  // Total radiosity (accumulated)
    glm::vec3 residual;     // Unshot energy
    uint32_t id;            // Unique patch ID
};
```

---

## Stage 2: Progressive Refinement Radiosity

### Algorithm

```
Initialize:
    For each patch i:
        accumulated[i] = emission[i]
        residual[i]    = emission[i]

Repeat until convergence:
    1. Select shooter = patch with max(residual[i] * area[i])
       (highest unshot power = fastest convergence)

    2. Render hemicube at shooter position:
       - 5 faces (top + 4 sides), 64x64 each
       - Fragment shader outputs patch ID as color
       - Depth buffer handles visibility automatically

    3. Read back hemicube, compute form factors:
       For each pixel:
           target_id = hemicube_color[pixel]
           F[shooter -> target_id] += delta_form_factor[pixel]

    4. Distribute energy:
       For each receiving patch j:
           delta = albedo[j] * residual[shooter] * F[shooter->j] * (area[shooter] / area[j])
           accumulated[j] += delta
           residual[j]    += delta

    5. residual[shooter] = (0, 0, 0)

Convergence: max(residual * area) < 1% of total initial emitted power
```

### Hemicube Rendering

The hemicube is a half-cube centered at a patch, oriented along the patch normal. We render 5 faces:

- **Top face:** Camera looks along patch normal, 90-degree FOV, aspect 1:1
- **4 Side faces:** Camera looks along 4 perpendicular directions (tangent, bitangent, -tangent, -bitangent), each with 90-degree FOV. Only the **upper half** (above the patch plane) contributes — the lower half is below the surface.

For OpenGL implementation:
- FBO: 64x64, `R32UI` color attachment (patch IDs), `DEPTH_COMPONENT24` depth
- Render all patch geometry with a flat shader that outputs the patch ID
- 5 draw calls per hemicube (one per face)

### Delta Form Factor Weights

Precomputed once at initialization. For a 64x64 hemicube face:

**Top face** (pixel at normalized coordinates u, v in [-1, +1]):
```
delta_ff_top(u, v) = 1.0 / (pi * (u*u + v*v + 1.0)^2) * pixel_area
```

**Side face** (pixel at u, z in [-1, +1], only z > 0 contributes):
```
delta_ff_side(u, z) = z / (pi * (u*u + z*z + 1.0)^2) * pixel_area
```

Where `pixel_area = (2.0 / resolution)^2` is the area of one pixel in normalized hemicube coordinates.

### Convergence

For the Tabernacle scene with 5 point lights + directional light:
- Each light source creates emissive patches (or we inject direct lighting into patches)
- 4-6 bounces are needed for enclosed spaces
- Estimated 3,000-5,000 shooting iterations to reach 95% convergence
- At ~1ms per hemicube render on the RX 6600: **~5-15 seconds total**

### Direct Light Injection

Light sources in the scene (point/spot/directional) are not geometry — they don't have patches. We need to inject their direct contribution:

```
For each patch:
    direct_light = compute_direct_lighting(patch.center, patch.normal,
                                            directional_lights, point_lights, spot_lights)
    patch.accumulated += direct_light
    patch.residual    += direct_light * patch.albedo  // reflected direct light is the initial bounce
```

This makes every directly-lit surface an "emitter" for bounce light. The progressive refinement then distributes this reflected light further.

---

## Stage 3: SH Probe Injection

After radiosity converges, we inject results into the existing SH probe grid.

### Method: Cubemap Rendering at Probe Positions

For each SH probe position in the grid:
1. Render a 6-face cubemap (16x16 per face) at the probe's world position
2. Fragment shader outputs the patch's `accumulated` radiosity value as the color
3. Project the cubemap onto L2 SH coefficients:

```
For each cubemap texel:
    direction = normalize(texel_world_position - probe_position)
    color = texel_color  (= patch accumulated radiosity)
    solid_angle = 4.0 / (sqrt(u*u + v*v + 1.0) * (u*u + v*v + 1.0) * total_texels_per_face)

    For each SH basis i (0..8):
        coeffs[i] += color * Ylm_i(direction) * solid_angle
```

4. Apply cosine convolution (radiance -> irradiance)
5. Store in the existing `SHProbeGrid` via `setProbeIrradiance(x, y, z, coeffs)`

### Performance

For a 10x5x15 grid = 750 probes:
- 750 probes x 6 faces x 16x16 = ~1.15M fragments (trivial for GPU)
- SH projection: 750 x 6 x 256 texels x 9 coefficients = ~10M multiply-adds (instant)
- **Total: <1 second**

---

## Stage 4: Upload

After SH probe injection, call `SHProbeGrid::upload()` which already exists — it uploads the 7 RGBA16F 3D textures to the GPU. No new code needed here.

---

## Light Leak Prevention

### Geometry Requirements
- All walls MUST have thickness (>= 5cm). Single-polygon walls will leak.
- The Tabernacle already uses thick walls (we added this in a previous fix).

### Backface-Weighted Probe Interpolation

When sampling the SH grid in the scene shader, bias the lookup:

```glsl
// Normal bias: pull sampling point away from surface toward correct side
vec3 biasedPos = worldPos + normal * 0.25;
vec3 gridUV = (biasedPos - u_shGridWorldMin) / (u_shGridWorldMax - u_shGridWorldMin);
```

This is a simple one-line change in the existing scene shader that significantly reduces leak through thin walls. The 0.25m bias is tunable.

### Probe Validity Check

During SH injection cubemap rendering, if a probe position is inside solid geometry (majority of rays hit backfaces), mark it invalid:

```cpp
int backfaceCount = 0;
for (each cubemap texel)
    if (hit_backface) backfaceCount++;
if (backfaceCount > total_texels * 0.25)
    // Zero out this probe's SH coefficients
```

Invalid probes contribute nothing during interpolation.

---

## Radiosity Baker API

```cpp
class RadiosityBaker
{
public:
    struct Config
    {
        float patchSize = 0.5f;          // Target patch edge length (meters)
        int hemicubeResolution = 64;     // Hemicube face resolution
        int maxIterations = 5000;        // Max shooting iterations
        float convergenceThreshold = 0.01f; // Stop at 1% remaining energy
        int shCubemapResolution = 16;    // Resolution per face for SH injection
    };

    /// @brief Run the full radiosity bake pipeline.
    /// @param patches Output: the baked patch data (for visualization/debug)
    /// @param grid The SH probe grid to inject results into
    /// @param scene Scene data (entities, transforms, materials)
    /// @param lights Direct light sources for initial injection
    void bake(const Config& config,
              SHProbeGrid& grid,
              const std::vector<Entity*>& entities,
              const DirectionalLight& sun,
              const std::vector<PointLight>& pointLights,
              const std::vector<SpotLight>& spotLights);

private:
    // Stage 1
    void createPatches(const std::vector<Entity*>& entities, float patchSize);

    // Stage 2
    void initializeDirectLight(const DirectionalLight& sun,
                                const std::vector<PointLight>& pointLights,
                                const std::vector<SpotLight>& spotLights);
    void setupHemicubeFBO(int resolution);
    void precomputeDeltaFormFactors(int resolution);
    int selectShooter() const;
    void renderHemicube(int shooterIndex);
    void computeFormFactors();
    void distributeEnergy(int shooterIndex);

    // Stage 3
    void injectIntoSHGrid(SHProbeGrid& grid, int cubemapResolution);

    // Data
    std::vector<RadiosityPatch> m_patches;
    GLuint m_hemicubeFBO = 0;
    GLuint m_hemicubeColorTex = 0;  // R32UI
    GLuint m_hemicubeDepthTex = 0;
    std::vector<float> m_deltaFFTop;      // 64x64
    std::vector<float> m_deltaFFSide;     // 64x32 (upper half only)
    std::unique_ptr<Shader> m_idShader;   // Outputs patch ID
    std::unique_ptr<Shader> m_vizShader;  // Outputs patch radiosity color

    // Patch geometry (uploaded as a single VBO for hemicube rendering)
    GLuint m_patchVAO = 0;
    GLuint m_patchVBO = 0;
};
```

---

## Implementation Steps

### Step 1: Patch Creation + Data Structures
- `RadiosityPatch` struct
- Triangle subdivision algorithm
- Walk all scene entities, extract world-space triangles, subdivide, create patches
- Upload patch geometry as a single VBO (position + patch ID per vertex)
- Hemicube ID shader (vertex: transform; fragment: output patch ID)

### Step 2: Direct Light Injection
- For each patch, compute direct lighting from all scene lights
- Standard Blinn-Phong/Lambert calculation at patch center using patch normal
- Shadow testing against cascaded shadow maps (optional — can skip for initial version)
- Store in `accumulated` and `residual`

### Step 3: Hemicube FBO + Delta Form Factors
- Create 64x64 FBO with `R32UI` color + depth
- Precompute delta form factor weight tables (top + side)
- Hemicube camera matrix generation (5 perspective matrices per patch)

### Step 4: Progressive Refinement Loop
- Shooter selection (find max residual*area)
- Render hemicube at shooter
- Read back pixel data (`glReadPixels`)
- Accumulate form factors per visible patch
- Distribute energy to receivers
- Loop until convergence

### Step 5: SH Probe Injection
- After radiosity converges, render cubemaps at each probe position
- Radiosity viz shader: outputs patch accumulated color as fragment color
- Project each cubemap to L2 SH (reuse existing `SHProbeGrid::projectCubemapToSH`)
- Apply cosine convolution (reuse existing `SHProbeGrid::convolveRadianceToIrradiance`)
- Store via `setProbeIrradiance()`
- Upload to GPU

### Step 6: Integration + Light Leak Prevention
- Normal bias in scene shader for SH grid sampling
- Backface detection for probe validity
- Wire `bakeRadiosity()` into engine initialization (after scene load, before first frame)

### Step 7: Debug Visualization (Optional)
- Render patches as flat-colored triangles showing accumulated radiosity
- Toggle via editor UI or hotkey
- Useful for tuning patch size and verifying convergence

---

## Performance Budget

| Stage | Estimated Time (RX 6600) |
|-------|--------------------------|
| Patch creation | <0.5s |
| Direct light injection | <0.5s |
| Hemicube rendering (5000 iterations x 5 faces x 64x64) | 5-15s |
| Form factor readback + distribution | 1-3s |
| SH probe injection (750 probes x 6 faces x 16x16) | <1s |
| **Total** | **~8-20 seconds** |

This fits comfortably within scene load time. A progress bar or log messages should indicate bake progress.

---

## Shader Details

### radiosity_id.vert.glsl
```glsl
#version 450 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in uint a_PatchID;

flat out uint v_PatchID;

uniform mat4 u_ViewProjection;

void main()
{
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
    v_PatchID = a_PatchID;
}
```

### radiosity_id.frag.glsl
```glsl
#version 450 core
flat in uint v_PatchID;

layout(location = 0) out uint o_PatchID;

void main()
{
    o_PatchID = v_PatchID;
}
```

### radiosity_viz.vert.glsl
```glsl
#version 450 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in uint a_PatchID;

uniform mat4 u_ViewProjection;

flat out uint v_PatchID;

void main()
{
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
    v_PatchID = a_PatchID;
}
```

### radiosity_viz.frag.glsl
```glsl
#version 450 core
flat in uint v_PatchID;

layout(std430, binding = 0) readonly buffer PatchRadiosity
{
    vec4 patchColors[];  // .rgb = accumulated radiosity, .a = unused
};

out vec4 FragColor;

void main()
{
    FragColor = vec4(patchColors[v_PatchID].rgb, 1.0);
}
```

---

## Mesa AMD Considerations

- All new texture units and FBOs follow existing patterns
- The `R32UI` integer FBO color attachment is well-supported on Mesa/RADV
- SSBOs (used in viz shader) require OpenGL 4.3+ (we target 4.5)
- Hemicube rendering uses backface culling (`GL_BACK`) — only front faces contribute

---

## Future Extensions

1. **Compute shader acceleration:** Replace `glReadPixels` with a compute shader that reads the hemicube texture directly and accumulates form factors on GPU. Eliminates CPU readback bottleneck.
2. **Adaptive patch subdivision:** After initial bake, subdivide patches at shadow boundaries and re-bake for sharper detail.
3. **Lightmaps:** Output radiosity as UV-mapped textures per surface instead of (or in addition to) SH probes. Higher quality but requires UV2 layouts.
4. **Incremental rebake:** When only one entity moves, only re-bake affected patches instead of the full scene.
