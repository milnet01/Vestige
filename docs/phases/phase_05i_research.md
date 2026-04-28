# Phase 5I Research: Terrain System

**Date:** 2026-03-23
**Scope:** Heightmap-based terrain rendering, LOD systems, texturing, sculpting, collision, normals, integration with existing systems, performance, serialization
**Engine:** Vestige (C++17, OpenGL 4.5, ImGui docking)
**Target Hardware:** AMD Radeon RX 6600 (RDNA2), 60 FPS minimum
**Existing systems:** Chunked foliage/vegetation with LOD, environment painting brushes, water renderer, cascaded + point shadow mapping, TAA, editor with gizmos/inspector/hierarchy/asset browser, scene serialization

---

## Table of Contents

1. [Heightmap-Based Terrain](#1-heightmap-based-terrain)
2. [Terrain LOD Systems](#2-terrain-lod-systems)
3. [Terrain Texturing](#3-terrain-texturing)
4. [Terrain Sculpting Tools](#4-terrain-sculpting-tools)
5. [Terrain Collision and Height Queries](#5-terrain-collision-and-height-queries)
6. [Terrain Normals](#6-terrain-normals)
7. [Integration with Existing Systems](#7-integration-with-existing-systems)
8. [Performance Considerations](#8-performance-considerations)
9. [Serialization](#9-serialization)
10. [Open Source References](#10-open-source-references)
11. [Recommendations for Vestige](#11-recommendations-for-vestige)

---

## 1. Heightmap-Based Terrain

### 1.1 Concept

A heightmap is a 2D grid of elevation values that defines terrain geometry. Each pixel maps to a vertex position where X and Z come from the grid coordinates and Y (height) comes from the pixel value. This is the dominant approach in game engines because it is memory-efficient (one scalar per vertex), GPU-friendly (regular grid structure enables predictable memory access), and compatible with image-editing workflows.

**Sources:**
- [LearnOpenGL - Tessellation Height Map](https://learnopengl.com/Guest-Articles/2021/Tessellation/Height-map)
- [LearnOpenGL/DeepWiki - Terrain Rendering](https://deepwiki.com/JoeyDeVries/LearnOpenGL/5.2-terrain-rendering)

### 1.2 Heightmap Formats and Bit Depths

| Format | Bits | Range | Notes |
|--------|------|-------|-------|
| 8-bit grayscale (PNG/BMP) | 8 | 0-255 | Produces terraced appearance, only suitable for prototyping |
| RAW / R16 | 16 | 0-65535 | Industry standard. Headerless binary file, x*y stream of 16-bit unsigned values. Used by Unity, Unreal, Terrain3D, World Machine, World Creator |
| 16-bit PNG | 16 | 0-65535 | Unreal Engine uses 16-bit grayscale PNG (with red MSB, green LSB for some engines like Urho3D) |
| EXR (OpenEXR) | 16/32 float | arbitrary | Full floating-point range. Stores real-world heights directly in meters. Preferred by Terrain3D for import |
| R32F | 32 float | arbitrary | Maximum precision. Internal GPU format for heightmap textures. Best for sculpting where accumulated edits need precision |

**Key insight from Terrain3D documentation:** "8-bit will give you an ugly terraced terrain and will require a lot of smoothing to be usable." A minimum of 16-bit is required for production-quality terrain. The standard assumption is **1 pixel = 1 meter** of lateral space.

**For Vestige:** Store heightmaps internally as **R32F textures** on the GPU (one float per texel) for maximum sculpting precision. Export/import as R16 (`.r16` or `.raw`) for interoperability with external tools, and EXR for lossless float exchange.

**Sources:**
- [Terrain3D Heightmaps Documentation](https://terrain3d.readthedocs.io/en/latest/docs/heightmaps.html)
- [Terrain3D Import/Export Documentation](https://terrain3d.readthedocs.io/en/stable/docs/import_export.html)
- [Rastertek - RAW Height Maps Tutorial](https://www.rastertek.com/dx11ter08.html)
- [Unreal Engine Terrain Heightmaps](https://docs.unrealengine.com/udk/Three/TerrainHeightmaps.html)
- [GameDev.net - Best Terrain Heightmap File Format](https://gamedev.net/forums/topic/326037-best-terrain-heightmap-file-format/)

### 1.3 Resolution and Size

Typical heightmap resolutions used in game engines:

| Resolution | Vertices | Terrain Size (1m/px) | Memory (R16) | Memory (R32F) |
|------------|----------|----------------------|--------------|---------------|
| 257x257 | 66,049 | 256m x 256m | 129 KB | 258 KB |
| 513x513 | 263,169 | 512m x 512m | 514 KB | 1 MB |
| 1025x1025 | 1,050,625 | 1024m x 1024m | 2 MB | 4 MB |
| 2049x2049 | 4,198,401 | 2048m x 2048m | 8 MB | 16 MB |
| 4097x4097 | 16,785,409 | 4096m x 4096m | 32 MB | 64 MB |

Urho3D requires dimensions of **power-of-two + 1** (e.g., 257, 513, 1025). This is a common convention because it ensures that the outer edges of adjacent terrain tiles share vertices exactly. Unity and Unreal follow similar conventions.

For an architectural walkthrough engine, 1025x1025 (1km x 1km at 1m resolution) or 2049x2049 (2km x 2km) is more than sufficient. Finer sub-meter resolution can be achieved with vertex spacing < 1.0 (e.g., 0.5m spacing with a 1025x1025 heightmap gives 512m x 512m with half-meter detail).

**Sources:**
- [Urho3D Terrain Class Reference](https://urho3d.github.io/documentation/1.6/class_urho3_d_1_1_terrain.html)

### 1.4 GPU Tessellation vs CPU Mesh Generation

There are two fundamental approaches to generating terrain geometry from a heightmap:

**CPU Mesh Generation:**
- Generate a full vertex buffer on the CPU from heightmap data
- Each vertex gets position (x, height, z), normal, and UV
- Upload to GPU as a static VBO
- Pros: Simple, no shader complexity, works on all hardware
- Cons: Fixed resolution (O(n^2) vertices), high memory, no dynamic LOD without re-uploading

**GPU Tessellation (Hardware):**
- Send a coarse grid of patches (4 vertices each for GL_PATCHES) to the GPU
- Tessellation Control Shader (TCS) sets tessellation levels per-edge based on camera distance
- Tessellation Evaluation Shader (TES) generates new vertices and samples the heightmap texture for displacement
- Pros: Dynamic LOD, low CPU overhead, reduced memory bandwidth
- Cons: Maximum tessellation factor is 64 in OpenGL (hardware limit), requires careful crack prevention, more complex shader pipeline

**GPU Tessellation Detail (from LearnOpenGL):**

The TCS computes tessellation factors per edge:
```glsl
// In TCS
float GetTessLevel(float dist0, float dist1) {
    float avgDist = (dist0 + dist1) / 2.0;
    if (avgDist <= 20.0)  return 64.0;
    if (avgDist <= 50.0)  return 32.0;
    if (avgDist <= 100.0) return 16.0;
    return 8.0;
}
```

The TES samples the heightmap:
```glsl
// In TES
layout(quads, fractional_even_spacing, cw) in;
uniform sampler2D heightMap;
void main() {
    // Interpolate position from patch corners
    vec4 p = mix(mix(p0, p1, u), mix(p3, p2, u), v);
    // Sample heightmap
    p.y = texture(heightMap, texCoord).r * heightScale;
    gl_Position = MVP * p;
}
```

**Crack prevention** between patches at different tessellation levels requires either `fractional_even_spacing` (symmetric vertex distribution) or explicit edge factor matching with neighboring patches.

**Hybrid Approach (recommended for Vestige):** Use CPU-generated mesh with a quadtree LOD system (CDLOD) rather than hardware tessellation. The CDLOD approach is simpler, more predictable, works within OpenGL 4.5 without tessellation stage complexity, and achieves comparable results. Hardware tessellation is better suited for planetary-scale terrain where the 64x subdivision per patch is needed.

**Sources:**
- [LearnOpenGL - Tessellation Height Map](https://learnopengl.com/Guest-Articles/2021/Tessellation/Height-map)
- [Tessellated Terrain with Dynamic LOD - Victor Bush](https://victorbush.com/2015/01/tessellated-terrain/)
- [GameDev.net - Terrain by Heightmap: CPU vs GPU](https://www.gamedev.net/forums/topic/685339-terrain-by-heightmap-on-cpu-vs-gpu/)
- [NVIDIA Terrain Tessellation Sample](https://docs.nvidia.com/gameworks/content/gameworkslibrary/graphicssamples/opengl_samples/terraintessellationsample.htm)
- [Procedural Terrain Generator OpenGL (GitHub)](https://github.com/stanislawfortonski/Procedural-Terrain-Generator-OpenGL)

---

## 2. Terrain LOD Systems

### 2.1 Overview of Approaches

| Algorithm | Complexity | GPU Friendliness | Crack Handling | Best For |
|-----------|-----------|-------------------|----------------|----------|
| ROAM (Real-time Optimally Adapting Meshes) | High | Low (CPU-heavy) | Implicit | Historical interest only |
| Geoclipmaps | Medium | High | Ring fixups + trims | Very large terrains, planetary |
| CDLOD (Continuous Distance-Dependent LOD) | Low-Medium | High | Vertex morphing | General purpose, easy to implement |
| Quadtree Adaptive | Medium | Medium | Stitching required | Variable-detail terrains |
| GPU Tessellation LOD | Low | Very High | Edge factor matching | Small-to-medium terrains |

### 2.2 CDLOD (Recommended)

CDLOD (Continuous Distance-Dependent Level of Detail) by Filip Strugar is the best fit for Vestige. It uses a quadtree of regular grids with distance-based LOD selection and smooth vertex morphing in the vertex shader.

**Core Architecture:**

1. **Quadtree Structure:** The terrain area is subdivided into a uniform quadtree. Each node covers a square region. The depth of the quadtree corresponds to LOD levels -- deeper nodes = finer detail. During construction, each node stores a bounding box with min/max height values sampled from the heightmap.

2. **LOD Range Computation:** Ranges increase geometrically (power of two) to match the quadtree subdivision:
```cpp
float minLodDistance = 15.0f;  // Tunable base distance
int lodLevels = 10;
float lodRanges[10];
for (int i = 0; i < lodLevels; i++) {
    lodRanges[i] = minLodDistance * pow(2.0f, i);
}
```
This gives ranges like: 15, 30, 60, 120, 240, 480, 960, 1920, 3840, 7680 meters.

3. **Node Selection (per frame):** Recursive traversal from the root:
   - Skip nodes outside the camera frustum (AABB vs frustum test)
   - Skip nodes outside the current LOD range (sphere intersection test)
   - At LOD 0 (finest): always select nodes within range
   - At higher LODs: select nodes that do NOT intersect the previous (finer) LOD range; if they do intersect, recurse into children

4. **Single Mesh Grid:** A single uniform grid mesh (e.g., 32x32 or 64x64 vertices) is reused for every selected node. The vertex shader transforms grid positions to cover each node's world-space area, then samples the heightmap for elevation. This means one VBO serves the entire terrain.

5. **Vertex Morphing (crack prevention):** Instead of stitching edges between different LOD levels, CDLOD smoothly morphs vertices toward coarser positions as they approach the LOD boundary. This is done entirely in the vertex shader:
```glsl
float getMorphValue(float dist, int lodLevel) {
    float low = (lodLevel != 0) ? lodRanges[lodLevel - 1] : 0.0;
    float high = lodRanges[lodLevel];
    float delta = high - low;
    float factor = (dist - low) / delta;
    return clamp(factor / 0.5 - 1.0, 0.0, 1.0);
}

vec2 morphVertex(vec2 vertex, vec2 meshPos, float morphValue) {
    vec2 fraction = fract(meshPos * meshDim * 0.5) * 2.0 / meshDim;
    return vertex - fraction * morphValue;
}
```
Morphing activates at 50% of the distance toward the LOD edge, snapping vertices to coarser grid positions. This eliminates cracks without any CPU-side mesh stitching.

**Key Advantages of CDLOD:**
- LOD function is based on precise 3D distance (not just 2D ring position like geoclipmaps)
- No mesh stitching required -- morphing handles all transitions
- Works on Shader Model 3.0+ (trivially supported by OpenGL 4.5)
- Simple quadtree data structure with predictable memory usage
- Better screen-triangle distribution than geoclipmaps (adapts to terrain, not just viewer position)
- Single grid mesh reused for all nodes -- minimal VBO memory

**Sources:**
- [CDLOD GitHub Repository (Filip Strugar)](https://github.com/fstrugar/CDLOD)
- [CDLOD Paper (PDF)](https://aggrobird.com/files/cdlod_latest.pdf)
- [CDLOD Terrain - svnte.se (Implementation Walkthrough)](https://svnte.se/cdlod-terrain)
- [CDLOD Terrain Demo (GitHub)](https://github.com/tschie/terrain-cdlod)
- [CDLOD Paper by Octavian Vasilovici](https://3dsurroundgaming.com/octavianvasilovici/projects/CDLODPaper.pdf)
- [GameDev.net - CDLOD Quadtree Discussion](https://gamedev.net/forums/topic/684278-cdlod-question-about-the-quadtree/)

### 2.3 Geoclipmaps (Alternative)

Geometry clipmaps use concentric rings of regular grids centered on the viewer. Each ring has progressively coarser resolution. Originated from Hugues Hoppe's GPU Gems 2 chapter.

**Structure:**
- L levels of nested grids, each n x n vertices (typically n = 255 = 2^8 - 1)
- Each level is 2x the scale of the previous
- Mesh consists of fixed "footprints": 12 blocks of m x m vertices per ring (m = (n+1)/4 = 64), plus 4 fixup regions (m x 3), L-shaped interior trims, and degenerate triangles on the outer perimeter
- Vertex (x, z) coordinates are constant (stored as SHORT2 = 4 bytes/vertex); Y comes from heightmap texture lookup in vertex shader
- Elevation stored in floating-point 2D texture per level

**LOD Transitions:** Blending parameter alpha ramps from 0 to 1 near the outer edge of each ring (transition width = n/10). Vertex height is blended: `z' = (1 - alpha) * z_fine + alpha * z_coarse`.

**Performance:** Original implementation achieved 130 FPS at 60M triangles/sec on GeForce 6800 GT. Draw calls per frame: approximately 6L + 5 (71 for L=11), reducible to 3L + 2 with instancing.

**Used in practice by:** The Witcher 3 (via Terrain3D's implementation), ARM's OpenGL ES terrain sample.

**Why not recommended for Vestige:** More complex to implement than CDLOD (multiple footprint types, trim regions, degenerate triangles). LOD is viewer-centric rings rather than distance-adaptive quadtree, which wastes triangles on flat areas far from the viewer. Better suited for very large open-world or planetary terrain.

**Sources:**
- [GPU Gems 2, Chapter 2 - Terrain Rendering Using GPU-Based Geometry Clipmaps](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry)
- [Hugues Hoppe - GPU Geometry Clipmaps](https://hhoppe.com/proj/gpugcm/)
- [ARM OpenGL ES Terrain Clipmaps](https://arm-software.github.io/opengl-es-sdk-for-android/terrain.html)
- [Igalia Blog - OpenGL Terrain Renderer](https://blogs.igalia.com/itoral/2016/10/13/opengl-terrain-renderer-rendering-the-terrain-mesh/)
- [Leadwerks - Large-Scale Terrain Algorithms Comparison](https://www.leadwerks.com/community/blogs/entry/1163-large-scale-terrain-algorithms/)

### 2.4 Adaptive Quadtree Meshing

Thatcher Ulrich's approach generates a single continuous mesh from the foreground to the horizon using an adaptive quadtree. Vertices are enabled/disabled based on screen-space error relative to distance:

```
L1 = max(abs(vertx - viewx), abs(verty - viewy), abs(vertz - viewz))
enabled = (error * Threshold) < L1
```

This produces "a few thousand triangles" per frame from large datasets. The main challenge is maintaining consistency across subdivision boundaries -- shared edge vertices must be simultaneously enabled to prevent cracks. More complex to implement than CDLOD due to per-vertex enable/disable logic and dependency propagation.

**Sources:**
- [Gamedeveloper.com - Continuous LOD Terrain Meshing Using Adaptive Quadtrees](https://www.gamedeveloper.com/programming/continuous-lod-terrain-meshing-using-adaptive-quadtrees)

---

## 3. Terrain Texturing

### 3.1 Splatmap-Based Multi-Texture Blending

A splatmap is a low-resolution texture where each channel (R, G, B, A) stores the blend weight of a terrain texture layer. The RGBA channels at each texel must sum to 1.0 (normalized weights).

**Basic GLSL implementation (4 textures, 1 splatmap):**
```glsl
uniform sampler2D splatMap;
uniform sampler2D tex0, tex1, tex2, tex3;

vec4 splat = texture(splatMap, terrainUV);
vec3 color = texture(tex0, tiledUV).rgb * splat.r
           + texture(tex1, tiledUV).rgb * splat.g
           + texture(tex2, tiledUV).rgb * splat.b
           + texture(tex3, tiledUV).rgb * splat.a;
```

**Scaling up to 16+ textures:** Use multiple splatmaps (4 splatmaps = 16 texture layers) combined with **texture arrays** (GL_TEXTURE_2D_ARRAY). Texture arrays store all terrain material layers in a single texture object, indexed by layer in the shader:
```glsl
uniform sampler2DArray terrainTextures;
// Sample layer i at tiled UV:
vec3 layerColor = texture(terrainTextures, vec3(tiledUV, float(i))).rgb;
```

Benefits of texture arrays over individual textures:
- Single bind point instead of N separate texture units
- No atlas boundary bleeding issues
- All layers must share the same resolution (e.g., 1024x1024)
- Eliminates multiple render passes -- single-pass blending

**Sources:**
- [NVIDIA Texture Array Terrain Sample](https://archive.docs.nvidia.com/gameworks/content/gameworkslibrary/graphicssamples/opengl_samples/texturearrayterrainsample.htm)
- [Khronos Forum - Splatmap Edge Blending](https://community.khronos.org/t/splat-map-edge-blending-issues/105450)
- [Khronos Forum - Blending 4 Textures with Splatmap](https://community.khronos.org/t/blending-4-textures-with-splat-map-webgl/71279)
- [Wikipedia - Texture Splatting](https://en.wikipedia.org/wiki/Texture_splatting)

### 3.2 Advanced Blending: Height/Depth-Based

Simple linear blending produces unrealistic "ghosting" between materials. Height-based (depth-based) blending uses the heightmap/bump channel of each texture to determine which material "wins" at each pixel:

**Progression of techniques (from "Advanced Terrain Texture Splatting" by Gamedeveloper.com):**

1. **Linear blend:** `color = tex0 * w0 + tex1 * w1` -- smooth but unrealistic transitions
2. **Depth selection:** `color = (tex0.a > tex1.a) ? tex0.rgb : tex1.rgb` -- uses alpha channel as depth/height, creates hard but realistic edges (sand fills cracks between rocks)
3. **Depth + weight:** `color = (tex0.a + w0 > tex1.a + w1) ? tex0.rgb : tex1.rgb` -- opacity affects perceived depth
4. **Smooth depth range:** Compute `ma = max(tex0.a + w0, tex1.a + w1) - depthRange`, then normalize contributions within that range -- produces smooth but natural transitions

The depth value is stored in the alpha channel of each terrain texture (generated from a grayscale/bump version of the texture).

**Sources:**
- [Gamedeveloper.com - Advanced Terrain Texture Splatting](https://www.gamedeveloper.com/programming/advanced-terrain-texture-splatting)
- [Polycount - Terrain Splatmaps How They Get Applied](https://polycount.com/discussion/214342/terrain-splatmaps-how-do-they-get-applied)

### 3.3 Triplanar Mapping

Standard UV-based texturing produces severe stretching on steep slopes because the texture is projected along a single axis (typically Y-up for terrain). Triplanar mapping projects textures along all three world axes and blends based on surface normal direction.

**GLSL approach:**
```glsl
vec3 blendWeights = abs(normal);
blendWeights /= (blendWeights.x + blendWeights.y + blendWeights.z); // Normalize

vec3 xProj = texture(terrainTex, worldPos.zy * texScale).rgb; // YZ plane
vec3 yProj = texture(terrainTex, worldPos.xz * texScale).rgb; // XZ plane (top-down)
vec3 zProj = texture(terrainTex, worldPos.xy * texScale).rgb; // XY plane

vec3 color = xProj * blendWeights.x + yProj * blendWeights.y + zProj * blendWeights.z;
```

**Blending control:**
- **Blend offset** (0-0.5): Subtract from weights before normalization to narrow transition zones
- **Blend exponent** (1-8): Apply power function for sharper transitions: `blendWeights = pow(blendWeights, vec3(exponent))`
- At exponent=8, transitions become very narrow -- good for distinct cliff vs ground materials

**Performance cost:** 3x the texture samples (9 total for albedo + normal + roughness per layer). For terrain, a common optimization is to only apply triplanar on steep slopes (where `blendWeights.y < threshold`) and use standard XZ projection elsewhere.

**Normal map handling with triplanar:** Tangent-space normals must be swizzled per projection axis:
- X projection: `normal = tangentNormal.zyx`
- Y projection: `normal = tangentNormal.xzy`
- Z projection: `normal = tangentNormal.xyz` (no change)

Mirroring must be handled by negating the U coordinate when the surface normal points negative along the projection axis.

**Sources:**
- [Catlike Coding - Triplanar Mapping (detailed tutorial)](https://catlikecoding.com/unity/tutorials/advanced-rendering/triplanar-mapping/)
- [GLSL Triplanar Mapping Gist (GitHub)](https://gist.github.com/patriciogonzalezvivo/20263fe85d52705e4530)
- [Envato Tuts+ - Tri-Planar Texture Mapping for Terrain](https://gamedevelopment.tutsplus.com/articles/use-tri-planar-texture-mapping-for-better-terrain--gamedev-13821)
- [Harry Alisavakis - Cliff Terrain Shader](https://halisavakis.com/my-take-on-shaders-cliff-terrain-shader/)

### 3.4 PBR Terrain Materials

Each terrain layer requires a full PBR material set:
- **Albedo** (RGB) + **height/depth** (A) -- for depth-based blending
- **Normal map** (RGB)
- **Roughness** (R) + **Metallic** (G) + **AO** (B) -- packed into single texture (commonly called ORM or ARM)

With texture arrays, each PBR property gets its own array:
- `sampler2DArray albedoHeightArray` -- layers x albedo+height
- `sampler2DArray normalArray` -- layers x normal maps
- `sampler2DArray ormArray` -- layers x occlusion/roughness/metallic

For 8 terrain layers at 1024x1024 resolution:
- Albedo+height: 8 layers * 1024^2 * 4 bytes = 32 MB (uncompressed)
- Normal: 8 layers * 1024^2 * 4 bytes = 32 MB
- ORM: 8 layers * 1024^2 * 4 bytes = 32 MB
- **Total: ~96 MB** (can be halved with BC compression or smaller textures)

Practical recommendation: Use **BC7 compression** (available in OpenGL 4.5 via GL_COMPRESSED_RGBA_BPTC_UNORM) which provides excellent quality at 1 byte/texel (8:1 compression ratio), reducing the 96 MB to ~12 MB.

---

## 4. Terrain Sculpting Tools

### 4.1 Brush Tool Architecture

Terrain sculpting operates by modifying the heightmap texture in response to brush input. The standard workflow used by Unity, Unreal, Godot, and dedicated tools like World Machine:

1. **Mouse raycast** against terrain to find the brush center position (world XZ)
2. **Convert world position** to heightmap texel coordinates
3. **Apply brush function** to a circular region around the center
4. **Update the heightmap texture** on the GPU (partial upload)
5. **Recompute normals** for the affected region
6. **Optionally update collision data** for the affected region

### 4.2 Brush Modes

Standard brush modes found across engines:

| Mode | Operation | Formula |
|------|-----------|---------|
| **Raise** | Add height | `h += strength * falloff(dist) * dt` |
| **Lower** | Subtract height | `h -= strength * falloff(dist) * dt` |
| **Smooth** | Average neighbors | `h = average(neighbors) * blend + h * (1 - blend)` |
| **Flatten** | Set to target height | `h = lerp(h, targetH, strength * falloff(dist) * dt)` |
| **Level** | Average within brush | `h = lerp(h, avgInBrush, strength * falloff(dist) * dt)` |
| **Noise** | Add procedural noise | `h += noise(pos) * strength * falloff(dist)` |
| **Erode** | Simulate erosion | Morphological operation on neighborhood |

**Falloff functions:**
- Linear: `falloff = 1.0 - dist/radius`
- Smooth (cosine): `falloff = cos(dist/radius * PI/2)`
- Gaussian: `falloff = exp(-dist^2 / (2 * sigma^2))`
- Hard: `falloff = dist < radius ? 1.0 : 0.0`

### 4.3 Implementation Approaches

**Approach A: CPU-side modification + partial texture upload**

This is the simplest and most common approach:
1. Maintain a CPU-side copy of the heightmap as a `float[]` array
2. On brush stroke, modify the affected region on CPU
3. Upload only the changed rectangle via `glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RED, GL_FLOAT, data)`
4. Mark affected normals for recomputation

Pros: Simple, debuggable, no GPU readback needed
Cons: CPU-GPU sync on upload, but `glTexSubImage2D` for small regions (32x32 to 128x128 pixels) is fast

**Approach B: GPU compute shader modification**

Use a compute shader to apply the brush directly to the heightmap texture:
```glsl
layout(r32f, binding = 0) uniform image2D heightmap;
uniform vec2 brushCenter;
uniform float brushRadius;
uniform float brushStrength;

void main() {
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    float dist = distance(vec2(texel), brushCenter);
    if (dist > brushRadius) return;

    float falloff = 1.0 - dist / brushRadius;
    float currentHeight = imageLoad(heightmap, texel).r;
    float newHeight = currentHeight + brushStrength * falloff;
    imageStore(heightmap, texel, vec4(newHeight));
}
```

Pros: No CPU-GPU data transfer, very fast for large brush radii
Cons: Need `glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT)` before rendering, smoothing requires ping-pong (cannot read and write same texel in one dispatch)

**Approach C: Render-to-texture (FBO-based)**

Render a fullscreen quad over the heightmap using an FBO, with the brush applied in the fragment shader. Good for smoothing operations where you need to read the "before" state while writing the "after" state -- use two heightmap textures and ping-pong between them.

**For Vestige (recommended):** Start with **Approach A** (CPU modification + `glTexSubImage2D`). It is simplest, fully debuggable, and the partial upload of a brush-sized region is negligible in cost. If profiling shows the CPU edit is a bottleneck (unlikely for interactive brush sizes), upgrade to compute shaders.

**Sources:**
- [GameDev.net - How Do I Create a Terrain Editor with OpenGL?](https://www.gamedev.net/forums/topic/712625-how-do-i-create-a-terrain-editor-with-opengl/)
- [GameDev.net - Best Way to Sculpt Terrain C++](https://www.gamedev.net/forums/topic/711489-best-way-to-sculpt-terrain-c-directx/)
- [HTerrain Plugin Documentation (Brush Tools)](https://hterrain-plugin.readthedocs.io/en/latest/)
- [Procedural Terrain Estimator (GitHub)](https://github.com/Snowapril/Procedural-Terrain-Estimator)
- [EarthSculptor Terrain Editor](https://www.earthsculptor.com/)
- [Urho3D Terrain Editor (GitHub)](https://github.com/JTippetts/U3DTerrainEditor)

### 4.4 Texture Weight Painting

Splatmap painting follows the same brush architecture but modifies the splatmap texture instead of the heightmap. Key constraint: weights must be normalized (sum to 1.0) at every texel after painting.

**Algorithm for painting layer N with strength S:**
1. Read current weights at texel (r, g, b, a)
2. Add S * falloff to channel N
3. Reduce all other channels proportionally so the sum equals 1.0
4. Write back normalized weights

For multi-splatmap systems (>4 layers), the same normalization applies across all splatmap texels at the corresponding position.

---

## 5. Terrain Collision and Height Queries

### 5.1 Height Query Methods

The Terrain3D documentation identifies five approaches, ordered from simplest to most complex:

1. **Direct heightmap lookup** -- For known (x, z) world coordinates, convert to heightmap texel coordinates and read the height value directly from the CPU-side float array. O(1) per query.

2. **Interpolated height query (`getHeight()`)** -- Same as above but bilinearly interpolates between the four surrounding heightmap texels for sub-texel accuracy. Essential for smooth object placement.

3. **Physics raycast** -- Cast a ray through a physics collision mesh generated from the heightmap. More expensive but works for arbitrary ray directions (not just vertical).

4. **GPU depth buffer readback** -- Read the depth buffer at a screen position and unproject to world space. Returns previous-frame data (one frame latency). Only useful for screen-space queries.

5. **Raymarching** -- Step along a ray, querying `getHeight()` at each step until the ray intersects the terrain surface. Works without physics but is iterative.

**For Vestige:** Implement **method 2** (interpolated height query) as the primary interface. This serves:
- Object/foliage placement on terrain
- Camera ground collision
- Editor picking (convert mouse ray to XZ intersection, then query height)

**Bilinear interpolation formula:**
```cpp
float Terrain::getHeight(float worldX, float worldZ) const {
    // Convert world position to heightmap coordinates
    float hx = (worldX - m_origin.x) / m_spacing.x;
    float hz = (worldZ - m_origin.z) / m_spacing.z;

    // Get integer grid cell and fractional part
    int ix = static_cast<int>(std::floor(hx));
    int iz = static_cast<int>(std::floor(hz));
    float fx = hx - ix;
    float fz = hz - iz;

    // Clamp to heightmap bounds
    ix = std::clamp(ix, 0, m_width - 2);
    iz = std::clamp(iz, 0, m_depth - 2);

    // Sample four corners
    float h00 = m_heightData[iz * m_width + ix];
    float h10 = m_heightData[iz * m_width + ix + 1];
    float h01 = m_heightData[(iz + 1) * m_width + ix];
    float h11 = m_heightData[(iz + 1) * m_width + ix + 1];

    // Bilinear interpolation
    float h0 = h00 * (1 - fx) + h10 * fx;
    float h1 = h01 * (1 - fx) + h11 * fx;
    return h0 * (1 - fz) + h1 * fz;
}
```

### 5.2 Terrain Raycast for Editor Picking

For the editor, mouse clicks need to be converted to terrain positions. Two approaches:

**Approach A: XZ plane intersection + height query**
Cast a ray from the camera through the mouse position. Find where the ray intersects the XZ plane (y=0), then query `getHeight()` at that XZ. Fast but inaccurate for non-flat terrain or steep viewing angles.

**Approach B: Iterative raymarching against heightmap**
Step along the ray in fixed increments. At each step, compare the ray's Y position with `getHeight(ray.x, ray.z)`. When the ray goes below the terrain, binary search between the last two steps for precise intersection. More accurate, handles steep slopes.

```cpp
glm::vec3 Terrain::raycast(const Ray& ray, float maxDist, float stepSize) const {
    for (float t = 0; t < maxDist; t += stepSize) {
        glm::vec3 p = ray.origin + ray.direction * t;
        float terrainH = getHeight(p.x, p.z);
        if (p.y < terrainH) {
            // Binary search refinement
            float lo = t - stepSize, hi = t;
            for (int i = 0; i < 8; i++) {
                float mid = (lo + hi) * 0.5f;
                glm::vec3 mp = ray.origin + ray.direction * mid;
                if (mp.y < getHeight(mp.x, mp.z)) hi = mid;
                else lo = mid;
            }
            return ray.origin + ray.direction * ((lo + hi) * 0.5f);
        }
    }
    return glm::vec3(0); // No intersection
}
```

**Performance note from Terrain3D:** Use raycasting only when you do not already know the X/Z coordinates. For vertical height queries (placement, gravity), direct `getHeight()` is far more efficient. For editor picking where the user clicks the screen, raymarching with ~0.5m steps and binary refinement converges quickly.

**Sources:**
- [Terrain3D Collision Documentation](https://terrain3d.readthedocs.io/en/latest/docs/collision.html)
- [Ogre Forums - Fast Terrain Collision](https://forums.ogre3d.org/viewtopic.php?t=73299)
- [GameDev.net - Collision Detection with Heightmaps](https://www.gamedev.net/forums/topic/474111-collision-detection-with-heightmaps/)
- [GameDev.net - Simple Terrain Collision](https://www.gamedev.net/forums/topic/684574-simple-method-for-implementing-terrain-collision/)

---

## 6. Terrain Normals

### 6.1 Central Differences Method

The simplest and most common approach. Sample the heightmap at the four cardinal neighbors and compute the gradient:

```glsl
vec3 computeNormal(sampler2D heightmap, vec2 uv, vec2 texelSize, float heightScale) {
    float hL = texture(heightmap, uv - vec2(texelSize.x, 0)).r * heightScale;
    float hR = texture(heightmap, uv + vec2(texelSize.x, 0)).r * heightScale;
    float hD = texture(heightmap, uv - vec2(0, texelSize.y)).r * heightScale;
    float hU = texture(heightmap, uv + vec2(0, texelSize.y)).r * heightScale;

    vec3 normal = normalize(vec3(hL - hR, 2.0 * texelSize.x, hD - hU));
    return normal;
}
```

The Y component (`2.0 * texelSize`) represents the spacing between samples. Using central differences (sampling both sides) gives a derivative approximation over `2 * texelSize`, so the Y component is `2.0 * spacing` to maintain correct proportions.

**CPU-side equivalent (for pre-computing normal map):**
```cpp
glm::vec3 computeNormal(int x, int z) {
    float hL = getHeight(x - 1, z);
    float hR = getHeight(x + 1, z);
    float hD = getHeight(x, z - 1);
    float hU = getHeight(x, z + 1);
    return glm::normalize(glm::vec3(hL - hR, 2.0f * m_spacing, hD - hU));
}
```

### 6.2 Sobel Filter Method

The Sobel filter uses a 3x3 weighted kernel that incorporates diagonal neighbors, producing smoother normals with better noise resistance:

**Horizontal gradient kernel (Gx):**
```
-1  0  1
-2  0  2
-1  0  1
```

**Vertical gradient kernel (Gy):**
```
-1 -2 -1
 0  0  0
 1  2  1
```

**GLSL implementation:**
```glsl
vec3 computeNormalSobel(sampler2D heightmap, vec2 uv, vec2 texelSize, float heightScale) {
    // Sample 3x3 neighborhood
    float h00 = textureOffset(heightmap, uv, ivec2(-1,-1)).r;
    float h10 = textureOffset(heightmap, uv, ivec2( 0,-1)).r;
    float h20 = textureOffset(heightmap, uv, ivec2( 1,-1)).r;
    float h01 = textureOffset(heightmap, uv, ivec2(-1, 0)).r;
    float h21 = textureOffset(heightmap, uv, ivec2( 1, 0)).r;
    float h02 = textureOffset(heightmap, uv, ivec2(-1, 1)).r;
    float h12 = textureOffset(heightmap, uv, ivec2( 0, 1)).r;
    float h22 = textureOffset(heightmap, uv, ivec2( 1, 1)).r;

    // Sobel gradients
    float gx = (h20 + 2.0*h21 + h22) - (h00 + 2.0*h01 + h02);
    float gz = (h02 + 2.0*h12 + h22) - (h00 + 2.0*h10 + h20);

    vec3 normal = normalize(vec3(-gx * heightScale, 8.0, -gz * heightScale));
    return normal;
}
```

The Y component (8.0) comes from the Sobel filter scale: the maximum weight sum is 4 per axis, and with spacing of 2 texels, the denominator is 8. Adjust the constant to control normal "sharpness" -- larger values produce flatter normals.

### 6.3 Pre-computed Normal Map vs Real-Time Computation

**Pre-computed normal map:**
- Store normals as an RGB texture (same resolution as heightmap, or higher for detail)
- Compute once on load and after each sculpting edit
- The HTerrain plugin stores normals in a dedicated texture and "automatically recomputes normals" during sculpting, with the benefit that "keeping the same amount of details in the distance independently from geometry" -- distant LOD levels still display full normal detail
- Format: RGB8 or RGB16F; encode as `(normal * 0.5 + 0.5)` for unsigned formats

**Real-time computation in vertex/fragment shader:**
- Sample heightmap neighbors per-vertex or per-fragment
- No extra texture needed
- Higher shader cost (4-8 extra texture samples per vertex/fragment)
- Useful when heightmap changes frequently (sculpting)

**Recommendation for Vestige:** Use a **pre-computed normal map texture** (RGB8, same resolution as heightmap). Recompute only the affected rectangular region after sculpting edits. This gives the best visual quality at lowest runtime cost, and normals remain detailed even at distant LOD levels where geometry is coarsened. During active sculpting, recompute the brush-affected region on CPU and upload via `glTexSubImage2D`.

**Tip from Khronos forum:** "For best results, your normalmap should be at least 4x bigger than the heightmap" -- though this is primarily relevant for displacement-mapped meshes. For terrain where the heightmap defines vertex positions directly, matching resolution is fine.

**Sources:**
- [Khronos Forum - HeightMap to NormalMap](https://community.khronos.org/t/heightmap-to-normalmap/58862)
- [GameDev.net - Calculate Normals from Displacement Map](https://www.gamedev.net/forums/topic/594457-calculate-normals-from-a-displacement-map/)
- [GameDev.net - Sobel Filter Normal Map Issue](https://www.gamedev.net/forums/topic/646301-sobel-filter-normal-map-issue/)
- [Rastertek - Terrain Normal Mapping](https://www.rastertek.com/dx11ter06.html)
- [Code Trip - Heightmapped Terrain and the Sobel Filter](https://codetrip.weebly.com/blog/year-3-semester-1-a-heightmapped-tessellated-terrain-and-the-sobel-filter)
- [HTerrain Plugin Documentation](https://hterrain-plugin.readthedocs.io/en/latest/)
- [Inigo Quilez - Normals for SDF](https://iquilezles.org/articles/normalsSDF/)

---

## 7. Integration with Existing Systems

### 7.1 Shadow Mapping Integration

The terrain must both **cast** and **receive** shadows in the existing cascaded shadow map system.

**Rendering terrain into shadow maps:**
- Render the terrain mesh into each cascade's shadow map FBO during the shadow pass
- Use the same LOD mesh as the main pass to prevent self-shadowing artifacts (mismatched geometry between shadow and color pass causes shadow acne or peter-panning)
- If tessellation is used: the terrain often gets heavily over-tessellated for shadow maps because tessellation levels are designed for the camera view, not the light view

**Terrain-specific shadow optimization (from AMD GPUOpen):**
- Only backfacing geometry (relative to the sun) can cast shadows. Frontfacing terrain tiles only receive shadows.
- Pre-compute per-tile/per-patch facing direction relative to the sun. Cull front-facing tiles from shadow map rendering.
- "Even though I removed roughly 50% of the geometry from the shadow map, there's no missing shadow" -- this optimization is free once you have per-patch normal data.
- Apply this culling per cascade for cumulative gains.

**For Vestige:** Since Vestige already has cascaded shadow maps, terrain integration requires:
1. Add terrain draw calls to the shadow pass (same VBO, simplified shader without texturing)
2. Ensure terrain uses the same geometry in shadow and color passes to avoid artifacts
3. Consider backface culling optimization if shadow performance becomes an issue

**Sources:**
- [AMD GPUOpen - Optimizing Terrain Shadows](https://gpuopen.com/learn/optimizing-terrain-shadows/)
- [NVIDIA - Cascaded Shadow Maps](https://developer.download.nvidia.com/SDK/10.5/opengl/src/cascaded_shadow_maps/doc/cascaded_shadow_maps.pdf)
- [Igalia Blog - Terrain Renderer Bloom and CSM](https://blogs.igalia.com/itoral/2016/10/20/opengl-terrain-renderer-update-bloom-and-cascaded-shadow-maps/)
- [LearnOpenGL - Cascaded Shadow Mapping](https://learnopengl.com/Guest-Articles/2021/CSM)

### 7.2 Foliage Placement on Terrain

The existing foliage/vegetation system needs to query terrain height and slope for placement:

**Height placement:** Each foliage instance's Y position comes from `terrain.getHeight(x, z)`. This must be called when:
- Foliage is initially placed (via environment painting brush)
- Terrain is sculpted (foliage in affected region must be re-placed)
- Scene is loaded (foliage positions may need terrain height validation)

**Slope-based filtering:**
- Compute terrain normal at each foliage position: `terrain.getNormal(x, z)`
- The slope angle is `acos(normal.y)` (angle from vertical)
- Filter: grass only on slopes < 30 degrees, rocks allowed on slopes < 60 degrees, nothing on near-vertical
- Align foliage to terrain normal using `Align to Ground` percentage (0% = world up, 100% = terrain normal)

**Automatic foliage from splatmap:**
- Advanced systems (Unreal Landscape Grass Type) spawn foliage based on splatmap weights -- if the "grass" layer weight > 0.5 at a position, spawn grass instances there
- This creates automatic foliage coverage that updates when the terrain texture is painted

**For Vestige:** The existing environment painting brush should query `terrain.getHeight()` and `terrain.getNormal()` for each painted instance. When terrain is sculpted, emit an event with the affected AABB so the foliage system can re-query heights for instances in that region.

**Sources:**
- [Unity Manual - Grass and Other Details](https://docs.unity3d.com/Manual/terrain-Grass.html)
- [Unreal Landscape Grass Type System](https://aaronneal.online/docs/m4/procedural-foliage-spawning/landscape-grass-type-system/)
- [Terrain3D Foliage Instancing](https://terrain3d.readthedocs.io/en/stable/docs/instancer.html)
- [Rastertek - Slope Based Texturing](https://rastertek.com/tertut14.html)

### 7.3 Water Edge Integration

Terrain and water interact at shorelines. The existing water renderer needs terrain depth information for:

**Shore foam:** Compare water surface height with terrain height at each fragment. Where the difference is small (shallow water), render foam/edge effects. This uses the depth buffer -- the terrain renders first, then the water shader reads the depth buffer to compute scene depth at each water pixel.

**Depth-based transparency:** Shallow water (terrain close to water surface) should be more transparent. Deep water (terrain far below surface) should be opaque and darker. The depth difference drives both color and opacity:
```glsl
float terrainDepth = linearDepth(texture(depthBuffer, screenUV).r);
float waterDepth = linearDepth(gl_FragCoord.z);
float depth = terrainDepth - waterDepth;
float foamFactor = 1.0 - smoothstep(0.0, foamRange, depth);
float alpha = smoothstep(0.0, deepRange, depth);
```

**For Vestige:** The existing water renderer likely already reads the depth buffer. Terrain integration means ensuring the terrain renders before water in the pipeline so its depth values are available. Shoreline foam width can be adjusted based on terrain slope -- steeper banks get narrower foam.

**Sources:**
- [Roystan - Toon Water Shader](https://roystan.net/articles/toon-water/)
- [Cyanilux - Shoreline Shader Breakdown](https://www.cyanilux.com/tutorials/shoreline-shader-breakdown/)
- [GameDev.net - Ideas for Water Foam](https://gamedev.net/forums/topic/522165-ideas-for-water-foam/)

---

## 8. Performance Considerations

### 8.1 Vertex Buffer Strategy

**Single shared grid mesh (CDLOD approach):**
- One VBO containing a fixed-size grid (e.g., 32x32 or 64x64 vertices)
- Each CDLOD node reuses this mesh, transformed in the vertex shader
- Vertex format: `vec2` position (XZ grid coords) = 8 bytes/vertex
- 32x32 grid = 1024 vertices * 8 bytes = 8 KB
- 64x64 grid = 4096 vertices * 8 bytes = 32 KB
- Heights come from heightmap texture lookup in vertex shader (vertex texture fetch)

**Index buffer for triangle strips:**
- Use `GL_TRIANGLE_STRIP` with degenerate triangles to link rows, or
- Use `GL_TRIANGLES` with an index buffer (simpler, negligible performance difference on modern GPUs)
- A 32x32 grid has ~1800 triangles per node; ~50 nodes visible = ~90,000 triangles total
- Well within budget for 60 FPS on RX 6600

**Buffer usage hints:**
- Grid mesh VBO: `GL_STATIC_DRAW` (never changes)
- Heightmap: `GL_TEXTURE_2D` with `GL_R32F` internal format, updated via `glTexSubImage2D` during sculpting
- Per-node instance data (transforms, LOD level): uniform buffer or SSBO, updated per frame

### 8.2 Draw Call Optimization

With CDLOD, each visible quadtree node requires one draw call using the shared grid mesh. Typical visible node count is 30-80 depending on view distance and quadtree depth.

**Instanced rendering:** All nodes at the same LOD level can be drawn with a single `glDrawElementsInstanced()` call if per-instance data (world offset, scale, LOD level) is passed via:
- Uniform buffer array: `layout(std140) uniform NodeData { vec4 nodeParams[MAX_NODES]; };`
- Or SSBO: `layout(std430, binding = 0) buffer NodeBuffer { NodeData nodes[]; };`

This can reduce draw calls to as few as `LOD_LEVELS` calls (e.g., 6-10 draw calls for the entire terrain).

**Frustum culling:** Cull quadtree nodes against the view frustum during node selection (CPU-side). AABB vs frustum plane test. Typically reduces visible nodes by 50-70% for a 90-degree FOV.

### 8.3 GPU Memory Budget

For a 1025x1025 terrain on RX 6600 (8 GB VRAM):

| Resource | Size |
|----------|------|
| Heightmap (R32F, 1025x1025) | 4 MB |
| Normal map (RGB8, 1025x1025) | 3 MB |
| Splatmap (RGBA8, 1025x1025) | 4 MB |
| Terrain texture array (8 layers, 1024x1024, BC7) | ~8 MB |
| Normal array (8 layers, 1024x1024, BC7) | ~8 MB |
| ORM array (8 layers, 1024x1024, BC7) | ~8 MB |
| Grid mesh VBO + IBO | ~50 KB |
| **Total** | **~35 MB** |

This is a trivial fraction of the RX 6600's 8 GB VRAM. Even doubling the terrain to 2049x2049 and using 16 texture layers would stay under 100 MB.

### 8.4 Texture Streaming (Not Needed Initially)

Texture streaming (loading/unloading heightmap and texture tiles on demand) is needed for terrains larger than ~8km x 8km. For Vestige's architectural walkthrough scope (likely < 2km x 2km), the entire terrain fits in VRAM comfortably. Implement streaming only if scope expands to open-world scale.

### 8.5 AMD RDNA2-Specific Optimizations

From the AMD RDNA Performance Guide:

- **Use compute shaders for full-screen passes** over fullscreen quads -- up to 2% improvement from removing helper lane overhead
- **Separate position data into its own vertex stream** for depth-only passes (shadow maps, z-prepass) -- improves cache locality
- **Use `Gather4` for single-channel texture sampling** -- reduces texture unit traffic, improves cache hit rate. Ideal for heightmap sampling
- **Avoid tessellation for culling** -- instead, cull in vertex shader by setting positions to NaN. This is a strong argument for CDLOD over hardware tessellation on RDNA2
- **Create large memory heaps (256 MB) and sub-allocate** -- reduces allocation overhead
- **Use non-linear texture layouts** for hardware compression eligibility

**Sources:**
- [AMD RDNA Performance Guide](https://gpuopen.com/learn/rdna-performance-guide/)
- [Khronos OpenGL Wiki - Vertex Specification Best Practices](https://www.khronos.org/opengl/wiki/Vertex_Specification_Best_Practices)
- [NVIDIA GPU Gems 2 - Geometry Instancing](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-3-inside-geometry-instancing)
- [NVIDIA GPU Gems 2 - Multistreaming](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-5-optimizing-resource-management-multistreaming)
- [GameDev.net - Terrain VBO + LOD + Draw Calls](https://gamedev.net/forums/topic/619709-terrain-vertex-buffers-index-buffer-lod-draw-calls/)
- [Igalia Blog - OpenGL Terrain Renderer Mesh](https://blogs.igalia.com/itoral/2016/10/13/opengl-terrain-renderer-rendering-the-terrain-mesh/)

---

## 9. Serialization

### 9.1 Terrain Data Files

A terrain requires several data files on disk:

| File | Format | Purpose |
|------|--------|---------|
| `terrain_heightmap.r32` | Raw 32-bit float, headerless | Height values, 1 float per texel |
| `terrain_normalmap.png` | RGB8 PNG | Pre-computed normals (encoded as `n * 0.5 + 0.5`) |
| `terrain_splatmap.png` | RGBA8 PNG | Texture layer weights |
| `terrain_settings.json` | JSON | Width, depth, spacing, height scale, texture layer assignments, LOD parameters |

**Alternative: single terrain file with header**

Some engines pack everything into one file with a custom header:
```
[Header: 64 bytes]
  magic: "VTER" (4 bytes)
  version: uint32
  width: uint32
  depth: uint32
  heightScale: float
  spacingX: float
  spacingZ: float
  numTextureLayers: uint32
  splatmapWidth: uint32
  splatmapHeight: uint32
  ... padding to 64 bytes ...

[Heightmap Data]
  width * depth * sizeof(float) bytes

[Normal Map Data]
  width * depth * 3 bytes (RGB8)

[Splatmap Data]
  splatmapWidth * splatmapHeight * 4 bytes (RGBA8)
```

**For Vestige:** Use **separate files** (heightmap + splatmap + settings JSON). This matches the existing scene serialization pattern, allows external editing of heightmaps with image tools, and avoids custom binary format complexity. The settings JSON integrates naturally with the existing scene serializer.

### 9.2 Integration with Scene Serialization

The terrain settings (file paths, transform, material assignments) should be serialized as part of the scene file, similar to how other entities are stored:

```json
{
    "terrain": {
        "heightmapPath": "assets/terrain/heightmap.r32",
        "normalmapPath": "assets/terrain/normalmap.png",
        "splatmapPath": "assets/terrain/splatmap.png",
        "width": 1025,
        "depth": 1025,
        "spacing": [1.0, 1.0],
        "heightScale": 100.0,
        "origin": [0.0, 0.0, 0.0],
        "textureLayers": [
            {"name": "Grass", "albedo": "grass_albedo.png", "normal": "grass_normal.png", "orm": "grass_orm.png", "tiling": 10.0},
            {"name": "Rock", "albedo": "rock_albedo.png", "normal": "rock_normal.png", "orm": "rock_orm.png", "tiling": 8.0},
            {"name": "Dirt", "albedo": "dirt_albedo.png", "normal": "dirt_normal.png", "orm": "dirt_orm.png", "tiling": 12.0},
            {"name": "Sand", "albedo": "sand_albedo.png", "normal": "sand_normal.png", "orm": "sand_orm.png", "tiling": 15.0}
        ],
        "lodSettings": {
            "maxLodLevels": 6,
            "baseLodDistance": 15.0,
            "gridResolution": 32
        }
    }
}
```

### 9.3 Undo/Redo for Terrain Edits

Terrain sculpting needs integration with the existing undo/redo system. Since heightmap edits affect a rectangular region:

- **Before** each brush stroke, copy the affected heightmap region (and splatmap region for paint operations)
- Store the "before" snapshot as the undo data
- On undo, restore the snapshot region via `glTexSubImage2D`
- On redo, re-apply the brush stroke or store the "after" snapshot

Memory management: For a 128x128 brush region at R32F, each undo snapshot is 64 KB. Capping at 100 undo steps = 6.4 MB -- negligible.

**Sources:**
- [GameDev.net - Best Terrain Heightmap File Format](https://gamedev.net/forums/topic/326037-best-terrain-heightmap-file-format/326037/)
- [Unity Discussions - Terrain Data Serialization](https://discussions.unity.com/t/terrain-data-serialization-textures/750986)
- [Unity Discussions - Save Terrain Height and Splat](https://discussions.unity.com/t/how-save-terrain-height-and-splat-to-file-use-c/824323)
- [World Creator - Conventional Export](https://docs.world-creator.com/reference/export/conventional-export)
- [CrySplatter - CRYENGINE Splatmap Importer (GitHub)](https://github.com/xulture/CrySplatter)

---

## 10. Open Source References

### 10.1 Terrain3D (Godot 4, C++)

**Repository:** [github.com/TokisanGames/Terrain3D](https://github.com/TokisanGames/Terrain3D)
**License:** MIT

The most feature-complete open-source terrain implementation available. Written in C++ as a Godot GDExtension.

**Key architectural decisions:**
- **Geometry Clipmap mesh** (as used in The Witcher 3) -- concentric rings of geometry, not quadtree
- **GPU-driven** -- heightmap stored as texture, vertex displacement in shader
- Supports terrain sizes from 64x64m to 65.5x65.5km
- Up to 32 texture layers, 10 LOD levels
- Sculpting tools: raise, lower, smooth, flatten, paint textures, paint colors/wetness
- Import/export: R16, EXR heightmaps
- 1 pixel = 1 meter assumption

**Relevance to Vestige:** Excellent reference for sculpting tool implementation, texture painting normalization, and terrain data management. The clipmap mesh code (credited to Mike J Savage) is a good reference even though Vestige should use CDLOD instead.

### 10.2 Urho3D Terrain (C++)

**Repository:** [github.com/urho3d/Urho3D](https://github.com/urho3d/Urho3D) -- `Source/Urho3D/Graphics/Terrain.cpp`, `Terrain.h`
**License:** MIT

A simpler, more traditional terrain implementation.

**Key architectural decisions:**
- **Patch-based system:** Terrain split into patches (configurable size, power of two), each a separate drawable
- **CPU-generated mesh:** Vertex buffers built on CPU from heightmap data
- **1-4 LOD levels per patch:** Coarser geometry at distance, with neighbor stitching for seamless boundaries
- **Neighbor terrain support:** `setNorthNeighbor()`, etc. for tiling multiple terrain blocks
- **Heightmap format:** Power-of-two + 1 dimensions (257, 513, etc.), 8-bit grayscale or 16-bit (R=MSB, G=LSB)
- **Height queries:** `getHeight(worldPos)`, `getNormal(worldPos)` -- interpolated
- **Coordinate conversion:** `worldToHeightMap()`, `heightMapToWorld()`
- **Smoothing:** Optional heightmap smoothing on load
- **Shadow support:** Configurable shadow distance, shadow mask, cast shadows flag

**Relevance to Vestige:** Excellent reference for the terrain API surface (`getHeight`, `getNormal`, neighbor management, patch system). The codebase is clean C++ and directly applicable. The patch-based approach is simpler than CDLOD but lacks smooth LOD transitions.

### 10.3 HTerrain (Godot Plugin)

**Repository:** [github.com/Zylann/godot_heightmap_plugin](https://github.com/Zylann/godot_heightmap_plugin)
**Documentation:** [hterrain-plugin.readthedocs.io](https://hterrain-plugin.readthedocs.io/en/latest/)

**Key architectural decisions:**
- Heights stored as **32-bit float textures** (red channel)
- Normals stored in a separate pre-computed texture, auto-recomputed on sculpt
- Quadtree LOD with chunks of 16x16 or 32x32 scaled by powers of two
- Splatmap texturing: CLASSIC4 (1 splatmap, 4 textures), MULTISPLAT16 (4 splatmaps + texture arrays, 16 textures), ARRAY (index/weight maps, 256 textures)
- Channel-based data notification: `notify_region_changed(rect, CHANNEL_HEIGHT)` triggers GPU upload and dependent recomputation
- Brush tools: raise, lower, smooth, level, flatten, erode

**Relevance to Vestige:** The `notify_region_changed()` pattern is excellent for partial updates. The multi-splatmap scaling approach (CLASSIC4 -> MULTISPLAT16 -> ARRAY) provides a clear upgrade path. The documentation is thorough and explains design trade-offs.

### 10.4 Other References

- **CDLOD Demo (OpenGL):** [github.com/tschie/terrain-cdlod](https://github.com/tschie/terrain-cdlod) -- Focused CDLOD implementation with quadtree selection and vertex morphing
- **CDLOD Original (DirectX 9, C++):** [github.com/fstrugar/CDLOD](https://github.com/fstrugar/CDLOD) -- Filip Strugar's reference implementation with paper
- **Terrain Sandbox (OpenGL ES 3.0):** [github.com/sduenasg/terrain-sandbox](https://github.com/sduenasg/terrain-sandbox) -- CDLOD on mobile
- **Procedural Terrain Generator (OpenGL 4.1):** [github.com/stanislawfortonski/Procedural-Terrain-Generator-OpenGL](https://github.com/stanislawfortonski/Procedural-Terrain-Generator-OpenGL) -- Tessellation-based approach
- **OpenGL Terrain Demo:** [github.com/MadEqua/opengl-terrain-demo](https://github.com/MadEqua/opengl-terrain-demo) -- Modern OpenGL terrain for learning

**Sources:**
- [Terrain3D GitHub](https://github.com/TokisanGames/Terrain3D)
- [Urho3D Terrain.h](https://github.com/urho3d/Urho3D/blob/master/Source/Urho3D/Graphics/Terrain.h)
- [Urho3D Terrain.cpp](https://github.com/urho3d/Urho3D/blob/master/Source/Urho3D/Graphics/Terrain.cpp)
- [HTerrain Plugin Docs](https://hterrain-plugin.readthedocs.io/en/latest/)
- [Godot Heightmap Plugin (GitHub)](https://github.com/Zylann/godot_heightmap_plugin)

---

## 11. Recommendations for Vestige

### 11.1 LOD System: CDLOD

**Use CDLOD** as the terrain LOD system. It is the best balance of simplicity, performance, and visual quality for Vestige's scope:

- Simpler than geoclipmaps (no ring fixups, trims, or degenerate triangles)
- Distance-based LOD adapts to terrain shape, not just viewer position
- Vertex morphing in the vertex shader eliminates CPU-side mesh stitching
- Single grid mesh VBO reused for all nodes -- minimal memory
- Compatible with OpenGL 4.5 (no tessellation required)
- The AMD RDNA Performance Guide recommends against using tessellation for culling, favoring vertex shader approaches -- this aligns with CDLOD

**Recommended parameters:**
- Grid resolution: 32x32 vertices (1024 vertices, ~1800 triangles per node)
- LOD levels: 6 (covers ~15m to ~480m distance ranges)
- Base LOD distance: 15-20 meters

### 11.2 Heightmap: R32F Internal, R16 External

- Internal GPU texture: `GL_R32F` (32-bit float per texel) for maximum sculpting precision
- CPU-side array: `std::vector<float>` matching heightmap dimensions
- Export format: `.r16` (16-bit unsigned, interoperable with Unity/Unreal/World Machine)
- Default resolution: 1025x1025 (power-of-two + 1) for a ~1km terrain at 1m spacing

### 11.3 Texturing: Splatmap + Texture Arrays

- Start with **1 splatmap (RGBA8) = 4 texture layers** -- sufficient for most architectural walkthroughs
- Use `GL_TEXTURE_2D_ARRAY` for terrain material textures (albedo+height, normal, ORM)
- Implement **height-based blending** (depth in alpha channel) for realistic material transitions
- Add **triplanar mapping** as an option for steep slopes (cliff faces, embankments)
- Use **BC7 compression** for texture arrays to keep VRAM usage low

### 11.4 Sculpting: CPU-Side + Partial Upload

- Maintain CPU-side heightmap and splatmap arrays
- Brush tools modify CPU arrays, then upload affected rectangle via `glTexSubImage2D`
- Recompute normals for the affected region + 1-texel border (central differences need neighbors)
- Integrate with existing undo/redo system via region snapshots
- Brush modes: raise, lower, smooth, flatten, paint texture

### 11.5 Collision/Height Queries: Interpolated Lookup

- `getHeight(worldX, worldZ)` with bilinear interpolation from CPU-side array
- `getNormal(worldX, worldZ)` from pre-computed normal data
- Raycast via iterative raymarching with binary refinement for editor picking
- Emit terrain-changed events (affected AABB) so foliage system can re-query heights

### 11.6 Normals: Pre-computed + Partial Update

- Store normals in an RGB8 texture, same resolution as heightmap
- Compute using central differences (4-neighbor) -- simpler and sufficient
- After sculpting, recompute only the modified rectangle + 1-texel border
- Upload via `glTexSubImage2D`

### 11.7 Shadow Integration

- Render terrain into CSM shadow passes using the same CDLOD mesh
- Use a simplified vertex-only shader (position + heightmap lookup, no texturing)
- Consider per-patch backface culling against sun direction as optimization

### 11.8 Serialization

- Separate files: heightmap (.r32), splatmap (.png), normal map (.png)
- Terrain settings in scene JSON (paths, dimensions, spacing, layer assignments, LOD config)
- Undo snapshots: copy affected heightmap/splatmap region before each brush stroke

### 11.9 Implementation Order

1. **Basic terrain rendering** -- Load heightmap, generate CDLOD quadtree, render with single grid mesh, heightmap texture lookup in vertex shader
2. **Normals + basic lighting** -- Pre-compute normal map, pass to fragment shader, basic diffuse/specular
3. **Splatmap texturing** -- Add splatmap + texture array blending in fragment shader
4. **Shadow integration** -- Add terrain to existing CSM shadow pass
5. **Sculpting tools** -- Brush raise/lower/smooth/flatten with CPU modification + partial upload
6. **Texture painting** -- Splatmap brush with weight normalization
7. **Height queries + foliage integration** -- `getHeight()`/`getNormal()` API, wire to foliage placement
8. **Editor UI** -- ImGui terrain inspector (heightmap settings, brush settings, layer management)
9. **Serialization** -- Save/load terrain data with scene
10. **Polish** -- Triplanar mapping, height-based blending, LOD tuning, water edge integration
