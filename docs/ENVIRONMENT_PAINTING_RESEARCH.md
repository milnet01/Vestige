# Environment Painting & Vegetation System Research

**Date:** 2026-03-21
**Purpose:** Inform the design of terrain painting, vegetation placement, water tools, and biome systems for the Vestige engine, targeting biblical-era environments (Tabernacle/Solomon's Temple).

---

## Table of Contents

1. [Tree Rendering with LOD](#1-tree-rendering-with-lod)
2. [Tree Species for Biblical Environments](#2-tree-species-for-biblical-environments)
3. [Biome/Ecosystem Presets](#3-biomeecosystem-presets)
4. [Water Painting Tools](#4-water-painting-tools)
5. [Terrain Texture Blending for Paths](#5-terrain-texture-blending-for-paths)
6. [Brush Tool UX Patterns](#6-brush-tool-ux-patterns)
7. [Performance Budgets](#7-performance-budgets)
8. [Recommended Approach for Vestige](#8-recommended-approach-for-vestige)

---

## 1. Tree Rendering with LOD

### The LOD Pipeline for Trees

Trees are among the most expensive objects in outdoor scenes. The standard approach is a multi-tier LOD pipeline:

| LOD Level | Distance | Representation | Typical Triangle Count |
|-----------|----------|---------------|----------------------|
| LOD0 | 0-30m | Full 3D mesh with leaves as alpha-tested quads | 5,000-50,000 |
| LOD1 | 30-80m | Simplified mesh, merged leaf clusters | 1,000-5,000 |
| LOD2 | 80-200m | Hybrid billboard cloud (multiple intersecting quads) | 200-1,000 |
| LOD3 | 200m+ | Single billboard impostor (1-2 quads) | 4-8 |

### Billboard Techniques

**Axis-Aligned Billboard:**
- The simplest approach. A single textured quad always rotates around the Y-axis to face the camera.
- Works well for distant trees when viewed from roughly eye-level.
- Fails at steep viewing angles (looking down from a cliff).

**Cross Billboard (X-Billboard):**
- Two or three quads intersecting at right angles through the trunk.
- Provides depth from any horizontal viewing angle.
- Still fails at extreme vertical angles.

**Billboard Cloud (Hybrid):**
- Multiple small billboards arranged to approximate the tree's 3D volume.
- Each billboard is a small quad textured with a portion of the tree.
- Bridges the gap between full 3D mesh and flat billboard, preventing visible "popping."
- InstaLOD and Simplygon both generate these automatically from high-poly meshes.

**Impostor (Octahedral/Spherical):**
- Pre-renders the tree from many viewing angles into an atlas texture.
- At runtime, selects the closest pre-rendered view based on camera angle.
- More expensive than simple billboards but handles all viewing angles.
- Requires atlas memory: typically 512x512 to 1024x1024 per tree species.

### LOD Transition Techniques

**Hard Cut (Pop):**
- Simplest: swap mesh instantly at distance threshold.
- Visually jarring. Not recommended for trees where popping is very noticeable.

**Cross-Fade (Alpha Blend):**
- During a short transition (0.2-0.5 seconds), render both LOD levels simultaneously.
- Fade out the old LOD while fading in the new one.
- Requires rendering both meshes briefly, doubling draw cost during transition.
- Looks smooth but adds overdraw.

**Dither Fade (Screen-Door Transparency):**
- Instead of true alpha blending, use a dither pattern to discard pixels.
- Old LOD has increasingly sparse pixels; new LOD fills in the gaps.
- When temporal anti-aliasing (TAA) is enabled, the noise changes each frame, creating a smooth blend over several frames.
- **No sorting required** (unlike alpha blending), no overdraw cost.
- Recommended approach for vegetation LOD transitions.

```glsl
// Pseudocode: Dither-based LOD cross-fade
uniform float u_lodFade; // 0.0 = fully this LOD, 1.0 = fully next LOD
// Bayer matrix or blue noise threshold
float threshold = bayerMatrix4x4(gl_FragCoord.xy);
if (u_lodFade > threshold) discard;
```

### How Major Engines Handle Tree LOD

**Unity:**
- LOD Groups auto-detect and instance each LOD level separately.
- GPU Instancer plugin uses `DrawMeshInstancedIndirect` with compute shaders.
- Automatic 2D billboard generation added as the last LOD.
- Cross-fading support with animation or fade transition width.

**Unreal Engine:**
- Hierarchical LOD (HLOD) merges distant meshes into combined proxy meshes.
- Nanite (UE5.5+) handles foliage with virtualized geometry -- no manual LOD needed.
- Foliage instancing system with per-instance culling.

**Godot (Terrain3D):**
- Auto-generates LODs on imported meshes (up to 10 levels, 4 recommended).
- MultiMesh3D nodes for instancing, generated per mesh type per 32m tile.
- Geometric clipmap terrain (same approach as The Witcher 3).

### Key Open-Source Implementations

- **Terrain3D** (Godot, C++ GDExtension): Full terrain + instancer with LOD. [GitHub](https://github.com/TokisanGames/Terrain3D)
- **AZDO OpenGL** (C++): Multi-draw indirect with GPU culling and LOD selection. [GitHub](https://github.com/potato3d/azdo)
- **TerrainEngine-OpenGL** (C++, OpenGL 4): Terrain with LOD, water, volumetric clouds. [GitHub](https://github.com/fede-vaccaro/TerrainEngine-OpenGL)
- **Procedural-Terrain-Estimator** (C++, OpenGL, ImGui): Heightmap + splatmap generation. [GitHub](https://github.com/Snowapril/Procedural-Terrain-Estimator)
- **Tempest** (C++11, OpenGL): Terrain editor with ImGui. [GitHub](https://github.com/CobaltXII/Tempest)

### Wind Animation for Trees

Wind is essential for believable vegetation. The standard approach is entirely GPU-based:

- **Vertex shader displacement:** Apply sine-wave-based displacement scaled by vertex height (higher vertices sway more).
- **Multi-layer wind:** Trunk sway (slow, large amplitude), branch sway (medium), leaf flutter (fast, small amplitude).
- **Vertex color encoding:** Red channel = branch flexibility, Blue channel = leaf wiggle amount, Green channel = phase offset.
- **Wind field texture:** A 2D scrolling noise texture sampled in the vertex shader provides spatial variation.

Reference: [GPU Gems 3, Chapter 6: GPU-Generated Procedural Wind Animations for Trees](https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-6-gpu-generated-procedural-wind-animations-trees)

Reference: [GPU Gems 3, Chapter 16: Vegetation Procedural Animation and Shading in Crysis](https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-16-vegetation-procedural-animation-and-shading-crysis)

---

## 2. Tree Species for Biblical Environments

### Primary Tree Species

The Vestige engine needs to render environments for two main settings:

**Setting 1: Tabernacle / Wilderness (Sinai Desert)**
- Arid, semi-desert landscape
- Wadis (dry riverbeds) with occasional oases
- Sparse vegetation concentrated near water sources

**Setting 2: Solomon's Temple / Jerusalem**
- Mediterranean climate with terraced hillsides
- Gardens, courtyards, cultivated groves
- More lush vegetation than the desert setting

### Species Reference Guide

| Species | Setting | Visual Characteristics | Modeling Notes |
|---------|---------|----------------------|----------------|
| **Acacia (Vachellia tortilis)** | Tabernacle | Flat umbrella canopy, thorny, gnarled trunk, sparse foliage | 2-10m height. Desert form is bush-like. Iconic silhouette. |
| **Acacia (Faidherbia albida)** | Temple | Tall, straight trunk (up to 30m), white bark | The "upright acacia" of the Torah. Used for Tabernacle construction. |
| **Cedar of Lebanon (Cedrus libani)** | Temple | Massive spreading canopy, flat-topped in maturity, horizontal layers of branches | Up to 30m+. Distinctive layered silhouette. The primary building timber for Solomon's Temple. |
| **Date Palm (Phoenix dactylifera)** | Both | Tall single trunk, crown of fronds at top, no branches | Up to 23m. Simple to model: textured cylinder trunk + alpha-tested frond planes. Used as temple ornamentation. |
| **Olive (Olea europaea)** | Temple | Twisted, gnarled trunk; silver-green leaves; wide spreading form | 5-15m. Ancient specimens have hollow, sculptural trunks. Symbol of peace. |
| **Cypress (Cupressus sempervirens)** | Temple | Tall, narrow, columnar form; dark green | Up to 35m. Very simple silhouette -- essentially a tall dark cone. Timber used for Temple floor boards. |
| **Pomegranate (Punica granatum)** | Temple | Small tree/large shrub; dense foliage; bright red fruit | 5-10m. 200 pomegranates engraved on Temple pillars. |
| **Fig (Ficus carica)** | Temple | Broad, spreading canopy; large distinctive leaves; smooth gray bark | 3-10m. Common garden tree. Broad leaf shape is recognizable. |
| **Tamarisk (Tamarix sp.)** | Both | Feathery, wispy foliage; multiple trunks; pink flowers | 1-12m. Desert/oasis tree. Delicate, transparent canopy. |

### Stylistic Approaches

For biblical-era environments, a **stylized realism** approach is recommended:
- Not photorealistic (too expensive and uncanny for historical settings).
- Not cartoon/low-poly (undermines the reverence of sacred spaces).
- **Target:** Hand-painted texture style with accurate silhouettes and proportions. Think "museum diorama" quality.
- Color palette: Warm earth tones, dusty greens, golden highlights, weathered wood.

### Modeling Strategy

- **Procedural generation** for trunk/branch structure using L-systems or simple recursive branching.
- **Alpha-tested quads** for leaf clusters (standard game approach).
- Each species needs 2-3 mesh variants to avoid repetition.
- Texture atlas per biome (desert trees share one atlas, garden trees share another) to minimize material switches.

### Sources

- [Trees of the Holy Land - BibleWalks](https://www.biblewalks.com/trees/)
- [Cedar of Lebanon - BiblePlaces.com](https://www.bibleplaces.com/cedar-of-lebanon/)
- [Woods of the Bible - Legacy Icons](https://legacyicons.com/blog/biblical-woods-orthodox-tradition)
- [Timber for the Tabernacle - Torah Flora](https://torahflora.org/2008/08/timber-for-the-tabernacle-today/)
- [The Cedars of Lebanon and the Temple of Solomon](https://arboriculture.wordpress.com/2016/05/18/the-cedars-of-lebanon-and-the-temple-of-solomon/)
- [Solomon's Splendor - Garden in Delight](https://www.gardenindelight.com/solomons-splendor/)

---

## 3. Biome/Ecosystem Presets

### What Is a Biome Preset?

A biome preset packages together everything needed to paint a coherent environment:

1. **Ground textures** (albedo, normal, roughness, height) for the splatmap layers
2. **Foliage rules** (which grass/flowers/bushes spawn, density, scale range)
3. **Tree species** (which trees appear, density, clustering rules)
4. **Scatter objects** (rocks, fallen branches, pottery shards, etc.)
5. **Color grading** (atmospheric fog color, ambient light tint)
6. **Audio ambience** (optional: wind, birds, insects)

### How Major Engines Package Biomes

**Unity (Vegetation Studio / Vista):**
- Define biomes as scriptable objects containing vegetation rules.
- Biomes are placed as shapes in the scene (painted regions or volumes).
- Overlapping biomes blend together with configurable transition widths.
- Vegetation Studio automatically creates billboards and batches for performance.
- The Vegetation Engine uses splatmap channels to control vegetation color, size, overlay, wetness, and wind per terrain layer.

**Unreal Engine (Procedural Biomes / PCG):**
- Each biome contains unique foliage sets (trees, vegetation, rocks, ground meshes, grass types).
- Landscape auto-material with 6+ landscape layers and 10+ grass types per biome.
- Procedural Content Generation (PCG) framework enables rule-based biome placement.
- Sparse foliage placed offline; dense foliage (grass) generated at runtime.

**Godot (Terrain3D):**
- Splatmap painting with up to 32 texture slots.
- Instancer system scatters foliage per painted region.
- Regions are 256x256m tiles, each with independent splatmap data.

### Biome Presets for Vestige

Based on biblical settings, the engine needs these biome presets:

| Biome | Ground Layers | Trees | Foliage | Scatter |
|-------|--------------|-------|---------|---------|
| **Desert Sand** | Sand, packed earth, rocky ground | Sparse acacia, tamarisk | Scrub brush, dried grass | Rocks, pebbles |
| **Oasis** | Wet sand, mud, green grass | Date palms, acacia, tamarisk | Reeds, ferns, flowers | Stones, pottery |
| **Rocky Hillside** | Limestone, dry grass, bare rock | Olive, fig, scattered cypress | Low scrub, wildflowers | Boulders, rubble |
| **Temple Garden** | Paved stone, manicured earth, grass | Cedar, cypress, pomegranate, olive | Trimmed hedges, lilies | Carved stone, urns |
| **River/Wadi Bank** | Mud, wet clay, gravel, grass | Willows, palms, tamarisk | Reeds, papyrus | Smooth river stones |
| **Cultivated Grove** | Tilled soil, grass paths | Olive orchard, fig, vine trellises | Ground cover, flowers | Stone walls, tools |

### Architecture for a Biome System

```
BiomePreset
  |-- GroundLayerSet (4-8 terrain textures with blending rules)
  |-- FoliageRuleSet (grass/flower types, density, scale range, slope limits)
  |-- TreeRuleSet (species, spacing, clustering, slope/altitude limits)
  |-- ScatterRuleSet (rocks, debris, props)
  |-- TransitionConfig (blend width, height-based rules)
```

Each biome preset is a data file (JSON or custom format) that the editor loads. Painting a biome onto terrain applies its splatmap weights and marks the region for vegetation spawning.

---

## 4. Water Painting Tools

### Water Body Types

Game engines typically support these water body types:

| Type | Shape Definition | Key Properties |
|------|-----------------|----------------|
| **River/Stream** | Open spline with start/end points | Width per point, depth per point, flow velocity, bank slope |
| **Lake/Pool** | Closed loop spline (all points same height) | Water level, depth map, shore blend width |
| **Ocean** | Infinite plane with optional coastline spline | Wave parameters, far mesh, horizon |
| **Waterfall** | Vertical spline connecting two water bodies | Particle effect + mesh hybrid |

### Unreal Engine's Water System Architecture

Unreal's water system is the industry reference for spline-based water:

- **Spline-defined shapes:** Rivers use open splines; lakes use closed-loop splines; oceans extend to horizon.
- **Unified mesh:** All water bodies in a level share a single Water Mesh Actor. The mesh is generated via a quadtree, traversed each frame to produce an optimized set of visible tiles.
- **Terrain deformation:** River splines automatically carve into the landscape, deforming the heightmap along the path.
- **Flow maps:** Rivers use per-point velocity values to write a flow map that drives surface animation.
- **Bank blending:** Shoreline rendering uses depth-based blending where the water meets terrain.
- **Seamless transitions:** Because all water zones use the same mesh, different water bodies blend perfectly where they overlap.

### Unity Water Approaches

- **Universal Water System (UWS):** Spline-based union and intersection for creating oceans, rivers, lakes, islands, and pools. Includes automatic flowmap generation and manual flowmap editing.
- Water surfaces typically use reflection probes or planar reflections + refraction via screen-space techniques.

### Water Rendering Techniques for OpenGL

The standard water rendering pipeline for OpenGL:

1. **Reflection Pass:** Render the scene from a reflected camera position (flip across water plane) into an FBO texture.
2. **Refraction Pass:** Render the scene below the water plane into another FBO texture.
3. **Depth Pass:** Capture depth buffer to determine water depth at each pixel.
4. **Water Surface Pass:** Render water mesh with a shader that:
   - Samples DuDv map (scrolling distortion texture) to animate UV coordinates
   - Samples normal map for surface detail and lighting
   - Uses Fresnel equation to blend between reflection and refraction
   - Modulates color/fog by depth (deeper = darker/more opaque)
   - Applies foam/shoreline effect where depth is shallow

```glsl
// Fresnel: More reflection at glancing angles, more refraction looking straight down
float fresnel = dot(viewDir, surfaceNormal);
fresnel = clamp(pow(1.0 - fresnel, fresnelPower), 0.0, 1.0);
vec3 color = mix(refractionColor, reflectionColor, fresnel);
```

### Water Painting Workflow for Vestige

For biblical environments, water features include:
- The **Laver** (bronze basin) in the Tabernacle courtyard -- a simple static water surface
- **Pools** in Solomon's Temple gardens -- closed-loop spline lakes
- **Streams/wadis** in desert scenes -- open spline rivers with seasonal variation

### Key Open-Source References

- [OpenGL Water Tutorial - BonzaiSoftware](https://blog.bonzaisoftware.com/tnp/gl-water-tutorial/)
- [OpenGL-Water (C++, GitHub)](https://github.com/teodorplop/OpenGL-Water)
- [Water Rendering C++ OpenGL (GitHub)](https://github.com/Nomyo/water_rendering)
- [Water Simulation with Specular Reflection (GitHub)](https://github.com/MauriceGit/Water_Simulation)
- [WebGL Water Tutorial (Rust, but technique is universal)](https://www.chinedufn.com/3d-webgl-basic-water-tutorial/)

---

## 5. Terrain Texture Blending for Paths

### Splatmap Fundamentals

A splatmap (also called weightmap or alphamap) is an RGBA texture where each channel represents the weight of a different terrain material:

- **R channel:** Material 0 (e.g., road/path)
- **G channel:** Material 1 (e.g., grass)
- **B channel:** Material 2 (e.g., dirt)
- **A channel:** Material 3 (e.g., rock)

Multiple splatmaps can be stacked for more than 4 materials (8 materials = 2 splatmaps, 12 = 3, etc.).

### Basic Alpha Blending (Naive)

```glsl
vec3 color = tex0.rgb * splat.r + tex1.rgb * splat.g + tex2.rgb * splat.b + tex3.rgb * splat.a;
```

This produces smooth, even transitions -- but they look unnatural. Sand blends evenly into stone, which never happens in reality.

### Height-Based Blending (Recommended)

Each material texture stores a height/depth value in its alpha channel. This height represents the physical surface profile (stone cracks are low, stone tops are high).

```glsl
// Height-aware blend between two materials
vec3 blend(vec4 texture1, float a1, vec4 texture2, float a2)
{
    float depth = 0.2; // Transition sharpness
    float ma = max(texture1.a + a1, texture2.a + a2) - depth;
    float b1 = max(texture1.a + a1 - ma, 0.0);
    float b2 = max(texture2.a + a2 - ma, 0.0);
    return (texture1.rgb * b1 + texture2.rgb * b2) / (b1 + b2);
}
```

**Result:** Sand fills cracks between stones; grass grows over road bricks from the edges; dirt accumulates in low spots. Much more natural-looking transitions.

### Path Blending Workflow

For painting paths on terrain:

1. **Path spline** defines the centerline and width.
2. **Generate splatmap weights** along the spline: full path material at center, gradient falloff to edges.
3. **Height-based blending** in the shader makes the transition look natural (path material fills crevices in surrounding terrain).
4. **Automatic foliage clearing** along the path: any vegetation within the path spline's influence zone is suppressed.

### Foliage Exclusion Along Paths

This is a known challenge across all engines:

- **Unreal:** Users frequently request automatic foliage avoidance along landscape splines. Solutions include exclusion landscape layers and foliage blocking volumes, but both require manual setup.
- **Common approach:** When spawning vegetation, check each candidate position against all path splines. If within the path's influence radius, skip the spawn. This can be done:
  - At paint time (CPU, when user paints vegetation)
  - At load time (CPU, when populating a biome)
  - At render time (GPU, cull instances in compute shader based on path distance)

### Splatmap Resolution

- Typical: 1 texel per meter (512x512 splatmap for a 512x512m terrain)
- Higher quality: 2-4 texels per meter
- For Vestige's relatively small scenes (Tabernacle courtyard is ~50m x 25m), even a 256x256 splatmap gives excellent resolution.

### Sources

- [Advanced Terrain Texture Splatting - Game Developer](https://www.gamedeveloper.com/programming/advanced-terrain-texture-splatting)
- [Terrain Rendering in Games - Basics](https://kosmonautblog.wordpress.com/2017/06/04/terrain-rendering-overview-and-tricks/)
- [Texture Splatting - Wikipedia](https://en.wikipedia.org/wiki/Texture_splatting)
- [Terrain Shader in Unity - InnoBlog](https://blog.innogames.com/terrain-shader-in-unity/)
- [Splatmap Edge Blending - Khronos Forums](https://community.khronos.org/t/splat-map-edge-blending-issues/105450)

---

## 6. Brush Tool UX Patterns

### Standard Brush Parameters

Every terrain/painting tool in professional engines uses these core parameters:

| Parameter | Description | Typical Range | UI Widget |
|-----------|-------------|--------------|-----------|
| **Radius/Size** | Brush area of effect | 0.1m - 100m | Slider + viewport preview circle |
| **Strength/Opacity** | How much effect per stroke | 0.0 - 1.0 | Slider |
| **Falloff** | Edge softness (hard edge vs. smooth gradient) | Linear, smooth, custom curve | Curve editor or preset dropdown |
| **Rotation** | Brush mask rotation | 0 - 360 degrees | Slider or mouse drag |
| **Spacing** | Distance between brush stamps along stroke | 0.1 - 5.0 (multiplier of radius) | Slider |
| **Scatter/Jitter** | Random offset per stamp | 0.0 - 1.0 | Slider |

### Keyboard Shortcuts (Industry Standard)

Unity Terrain Tools uses:
- **A** key + mouse horizontal: Adjust brush strength
- **S** key + mouse horizontal: Adjust brush size
- **D** key + mouse horizontal: Adjust brush rotation
- **Shift** hold: Temporarily switch to smoothing brush
- **Ctrl** hold: Invert brush effect (raise/lower, add/remove)

### Viewport Brush Preview

The brush preview is rendered as an overlay on the terrain showing the brush's area of effect. Two approaches:

**Approach 1: Fragment Shader Overlay (Recommended for Vestige)**

Add brush visualization directly to the terrain shader:

```glsl
// In terrain fragment shader
uniform vec3 u_brushPosition;  // World-space brush center
uniform float u_brushRadius;
uniform float u_brushFalloff;   // 0 = hard edge, 1 = fully soft
uniform vec4 u_brushColor;      // e.g., vec4(0.3, 0.6, 1.0, 0.4)

// Calculate brush influence at this fragment
float dist = length(v_worldPos.xz - u_brushPosition.xz);
float normalizedDist = dist / u_brushRadius;

// Outer ring (border)
float ring = smoothstep(0.95, 1.0, normalizedDist) - smoothstep(1.0, 1.05, normalizedDist);

// Inner fill with falloff
float fill = 1.0 - smoothstep(u_brushFalloff, 1.0, normalizedDist);
fill *= 0.15; // Subtle fill

// Combine
float brushAlpha = max(ring * 0.8, fill);
if (normalizedDist <= 1.05) {
    finalColor.rgb = mix(finalColor.rgb, u_brushColor.rgb, brushAlpha);
}
```

**Approach 2: Decal Projection**

Render a projected decal (a screen-space quad that reads depth buffer and projects a brush texture). More flexible for complex brush shapes but requires an extra draw pass.

### Falloff Curve Editor

For custom falloff curves, use an ImGui Bezier widget:

- [ImGui Bezier Widget (GitHub)](https://github.com/TuTheWeeb/ImGui-Bezier-Widget)
- The widget allows control points to define the falloff shape.
- Presets: Linear, Smooth (S-curve), Hard, Pinch, Gaussian.
- Store as 4 float values (2 control points of a cubic Bezier).

### ImGui Brush Controls Panel Layout

```
[Brush Tool] ─────────────────────────────
  Mode:  [Raise] [Lower] [Smooth] [Paint]

  Size:     [====|==========] 5.2m    [A]
  Strength: [========|======] 0.65    [S]
  Falloff:  [Smooth ▼]  [Edit Curve...]

  ┌─Falloff Preview────┐
  │     ╱──╲           │  (Mini curve display)
  │   ╱      ╲         │
  │ ╱          ╲       │
  └────────────────────┘

  [ ] Auto-clear foliage
  [ ] Follow terrain normal
─────────────────────────────────────────
```

### Sources

- [Unity Terrain Tools - Brush Controls](https://docs.unity3d.com/Packages/com.unity.terrain-tools@5.0/manual/brush-controls-shortcut-keys.html)
- [Terrain Editing Brush Indicator - Khronos Forums](https://community.khronos.org/t/terrain-editing-brush-indicator/103872)
- [ImGui Bezier Widget - GitHub](https://github.com/TuTheWeeb/ImGui-Bezier-Widget)
- [ImGui Useful Extensions Wiki](https://github.com/ocornut/imgui/wiki/Useful-Extensions)
- [Terrain Editor C++/OpenGL - GitHub](https://github.com/Aggroo/TerrainEditor)

---

## 7. Performance Budgets

### Draw Call Budget

| Platform | Draw Calls/Frame Target | Notes |
|----------|------------------------|-------|
| Desktop (mid-range, DX11/OpenGL 4.5) | 500 - 2,000 | RX 6600 falls here |
| Desktop (high-end) | 2,000 - 5,000 | With modern drivers |
| Mobile | 40 - 160 | Not relevant for Vestige |

**Key insight:** Draw calls are a CPU-side bottleneck, not GPU-side. Each `glDrawElements` call requires driver overhead. The GPU itself can handle millions of triangles -- the bottleneck is how many times you *ask* it to draw.

### Reducing Draw Calls with Instancing

| Technique | Draw Calls | Setup Complexity | Flexibility |
|-----------|-----------|-----------------|-------------|
| Individual draws | N (1 per object) | None | Full |
| CPU batching | N/batch_size | Low | Moderate |
| `glDrawElementsInstanced` | 1 per mesh type | Medium | Per-instance data via attributes |
| `glMultiDrawElementsIndirect` | 1 total | High | Full GPU-driven pipeline |

For vegetation, **instanced rendering** is essential:
- 10,000 identical trees with individual draws = 10,000 draw calls (far too many)
- 10,000 identical trees with instancing = 1 draw call
- 5 tree species x 3 LOD levels = 15 draw calls maximum for an entire forest

### Triangle Budget

| Scene Element | Budget (60 FPS on RX 6600) | Notes |
|---------------|---------------------------|-------|
| Total scene | 2-5 million triangles/frame | Conservative target |
| Terrain | 100,000 - 500,000 | With clipmap LOD |
| Trees (all visible) | 500,000 - 1,500,000 | Instanced, with LOD |
| Foliage/grass | 200,000 - 500,000 | Instanced quads |
| Architecture | 200,000 - 500,000 | Tabernacle/Temple |
| Water | 10,000 - 50,000 | Simple quad grid |
| Characters/props | 50,000 - 200,000 | If applicable |

### GPU-Driven Rendering Pipeline (Advanced)

For maximum vegetation performance, the GPU-driven approach:

1. **Store all instance data in SSBOs:** Position, rotation, scale, LOD level for every tree/bush/grass instance.
2. **Compute shader culling:** Dispatch a compute shader that:
   - Tests each instance against the view frustum
   - Selects LOD level based on distance
   - Writes visible instances into a draw indirect buffer
3. **Single draw call:** `glMultiDrawElementsIndirect` draws everything in one call.
4. **Persistent mapped buffers:** Use `GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT` for fast CPU-to-GPU data transfer.

```
DrawElementsIndirectCommand {
    uint count;         // Index count for this mesh
    uint instanceCount; // Set by compute shader (0 = culled)
    uint firstIndex;    // Offset into index buffer
    int  baseVertex;    // Added to each index
    uint baseInstance;  // Base instance for vertex attributes
};
```

Reference: [Multi-Draw Indirect Tutorial](https://ktstephano.github.io/rendering/opengl/mdi)
Reference: [AZDO Techniques - GitHub](https://github.com/potato3d/azdo)

### Mesh Size Guidelines

- **Minimum triangles per draw call on AMD GPUs:** ~256 triangles. Drawing fewer wastes GPU cycles.
- **Optimal mesh size for instancing:** 1,000 - 20,000 triangles. A mesh around 5,000 triangles works well across different GPUs.
- **Batch small meshes:** If grass blades are 8 triangles each, batch 500-1000 blades into a single mesh (4,000-8,000 triangles), then instance that batch.

### Vestige-Specific Performance Notes

The Tabernacle scene is relatively contained (~50m x 25m courtyard with surrounding desert). Expected instance counts:
- Trees: 20-100 (sparse desert scene) to 200-500 (garden scene)
- Ground foliage: 1,000-5,000 grass/shrub instances
- Scatter objects: 100-500

This is well within budget for the RX 6600, even without GPU-driven rendering. Simple `glDrawElementsInstanced` with CPU frustum culling should be sufficient initially. GPU-driven pipeline can be added later for larger scenes.

### Sources

- [LearnOpenGL - Instancing](https://learnopengl.com/Advanced-OpenGL/Instancing)
- [OpenGL Performance Wiki](https://www.khronos.org/opengl/wiki/Performance)
- [GPU Instancing Deep Dive](https://gpu-benchmark.org/article-gpu-instancing-deep-dive.html)
- [Efficient WebGL Vegetation Rendering](https://dev.to/keaukraine/efficient-webgl-vegetation-rendering-4g2g)
- [SSBOs Tutorial](https://ktstephano.github.io/rendering/opengl/ssbos)
- [Persistent Mapped Buffers - C++ Stories](https://www.cppstories.com/2015/01/persistent-mapped-buffers-in-opengl/)
- [Indirect Rendering: A Way to a Million Draw Calls](https://cpp-rendering.io/indirect-rendering/)

---

## 8. Recommended Approach for Vestige

### Phase 1: Foundation (Start Here)

1. **Terrain splatmap painting** with height-based blending (4 layers per splatmap).
2. **Basic brush tool** with radius, strength, and smooth falloff. Fragment shader brush preview overlay.
3. **Instanced vegetation** using `glDrawElementsInstanced` with 2 LOD levels (mesh + billboard).
4. **Simple water plane** with reflection/refraction FBOs, DuDv normal scrolling, and Fresnel blending.
5. **3-4 tree species** modeled with alpha-tested leaf quads and 2 LOD levels each.

### Phase 2: Polish

1. **Dither-based LOD transitions** to eliminate popping.
2. **Biome presets** as data files (JSON) defining ground layers + vegetation rules.
3. **Path spline tool** with splatmap generation and automatic foliage exclusion.
4. **Wind animation** via vertex shader displacement.
5. **Falloff curve editor** using ImGui Bezier widget.

### Phase 3: Advanced (If Needed)

1. **GPU-driven rendering** with compute shader culling and `glMultiDrawElementsIndirect`.
2. **Billboard cloud** generation for LOD2 of complex tree canopies.
3. **Water spline tool** for rivers with flow maps.
4. **Procedural tree generation** with L-systems for variety.

### Key Trade-offs

| Decision | Recommendation | Reason |
|----------|---------------|--------|
| LOD transitions | Dither fade | No sorting, no overdraw, works with TAA |
| Instancing method | `glDrawElementsInstanced` initially | Simple, sufficient for Vestige's scene scale |
| Splatmap blending | Height-based | Much more natural than linear blend; small shader cost |
| Brush preview | Fragment shader overlay | No extra pass needed; integrates into terrain shader |
| Water rendering | Planar reflection + refraction FBOs | Standard technique, well-documented, good quality |
| Tree modeling | Hand-modeled with alpha quads | L-systems are complex; 4-5 species can be hand-made |
| Biome data format | JSON | Already used in scene serialization; human-readable |

---

## All Sources

### Tutorials and Articles
- [LearnOpenGL - Instancing](https://learnopengl.com/Advanced-OpenGL/Instancing)
- [LearnOpenGL - Frustum Culling](https://learnopengl.com/Guest-Articles/2021/Scene/Frustum-Culling)
- [Advanced Terrain Texture Splatting - Game Developer](https://www.gamedeveloper.com/programming/advanced-terrain-texture-splatting)
- [Terrain Rendering in Games - Basics](https://kosmonautblog.wordpress.com/2017/06/04/terrain-rendering-overview-and-tricks/)
- [Terrain Shader in Unity - InnoBlog](https://blog.innogames.com/terrain-shader-in-unity/)
- [Multi-Draw Indirect Tutorial](https://ktstephano.github.io/rendering/opengl/mdi)
- [SSBOs Tutorial](https://ktstephano.github.io/rendering/opengl/ssbos)
- [Persistent Mapped Buffers - C++ Stories](https://www.cppstories.com/2015/01/persistent-mapped-buffers-in-opengl/)
- [Indirect Rendering: A Way to a Million Draw Calls](https://cpp-rendering.io/indirect-rendering/)
- [OpenGL Water Tutorial - BonzaiSoftware](https://blog.bonzaisoftware.com/tnp/gl-water-tutorial/)
- [Simplest Way to Render Pretty Water in OpenGL](https://medium.com/@vincehnguyen/simplest-way-to-render-pretty-water-in-opengl-7bce40cbefbe)
- [Lighthouse3D Billboarding Tutorial](https://www.lighthouse3d.com/opengl/billboarding/)
- [Billboards - OpenGL Tutorial](http://www.opengl-tutorial.org/intermediate-tutorials/billboards-particles/billboards/)
- [Dither Transparency - Daniel Ilett](https://danielilett.com/2020-04-19-tut5-5-urp-dither-transparency/)
- [Efficient WebGL Vegetation Rendering](https://dev.to/keaukraine/efficient-webgl-vegetation-rendering-4g2g)
- [Waving Grass OpenGL Tutorial](https://www.mbsoftworks.sk/tutorials/opengl3/29-terrain-pt2-waving-grass/)
- [Procedural Foliage with L-systems](https://jysandy.github.io/posts/procedural-trees/)
- [Foliage Optimization in Unity - Eastshade Studios](https://www.eastshade.com/foliage-optimization-in-unity/)
- [Smoother LOD Transitions with Dithered Opacity - Cesium](https://cesium.com/blog/2022/10/20/smoother-lod-transitions-in-cesium-for-unreal/)

### GPU Gems (NVIDIA)
- [GPU Gems 3, Ch. 6: GPU-Generated Wind Animations for Trees](https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-6-gpu-generated-procedural-wind-animations-trees)
- [GPU Gems 3, Ch. 16: Vegetation Animation/Shading in Crysis](https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-16-vegetation-procedural-animation-and-shading-crysis)
- [GPU Gems, Ch. 7: Rendering Countless Blades of Waving Grass](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-7-rendering-countless-blades-waving-grass)

### Open-Source Projects
- [Terrain3D (Godot, C++)](https://github.com/TokisanGames/Terrain3D)
- [AZDO OpenGL Techniques (C++)](https://github.com/potato3d/azdo)
- [TerrainEngine-OpenGL (C++)](https://github.com/fede-vaccaro/TerrainEngine-OpenGL)
- [Procedural-Terrain-Estimator (C++, OpenGL, ImGui)](https://github.com/Snowapril/Procedural-Terrain-Estimator)
- [Terrain Editor (C++, OpenGL)](https://github.com/Aggroo/TerrainEditor)
- [OpenGL-Water (C++)](https://github.com/teodorplop/OpenGL-Water)
- [Water Rendering (C++, OpenGL)](https://github.com/Nomyo/water_rendering)
- [Water Simulation (C++, OpenGL)](https://github.com/MauriceGit/Water_Simulation)
- [Cross-Fading LOD (Unity)](https://github.com/keijiro/CrossFadingLod)
- [Indirect Rendering with Compute Shaders (Unity)](https://github.com/ellioman/Indirect-Rendering-With-Compute-Shaders)
- [GL Occlusion Culling (NVIDIA)](https://github.com/nvpro-samples/gl_occlusion_culling)
- [Modern OpenGL 4.5 Rendering Techniques](https://github.com/potato3d/modern-opengl)
- [ImGui Bezier Widget](https://github.com/TuTheWeeb/ImGui-Bezier-Widget)
- [L-System Tree Generator](https://github.com/manuelpagliuca/l-system)

### Engine Documentation
- [Unity Terrain Tools - Brush Controls](https://docs.unity3d.com/Packages/com.unity.terrain-tools@5.0/manual/brush-controls-shortcut-keys.html)
- [Unity GPU Instancing](https://docs.unity3d.com/6000.1/Documentation/Manual/GPUInstancing.html)
- [Unity LOD Group Transitions](https://docs.unity3d.com/6000.2/Documentation/Manual/lod/lod-transitions-lod-group.html)
- [Unreal Water System](https://dev.epicgames.com/documentation/en-us/unreal-engine/water-system-in-unreal-engine)
- [Unreal Procedural Foliage Tool](https://dev.epicgames.com/documentation/en-us/unreal-engine/procedural-foliage-tool-in-unreal-engine)
- [Terrain3D Documentation](https://terrain3d.readthedocs.io/en/stable/)
- [OpenGL Performance Wiki](https://www.khronos.org/opengl/wiki/Performance)

### Biblical/Historical Reference
- [Trees of the Holy Land - BibleWalks](https://www.biblewalks.com/trees/)
- [Cedar of Lebanon - BiblePlaces.com](https://www.bibleplaces.com/cedar-of-lebanon/)
- [Woods of the Bible - Legacy Icons](https://legacyicons.com/blog/biblical-woods-orthodox-tradition)
- [Timber for the Tabernacle - Torah Flora](https://torahflora.org/2008/08/timber-for-the-tabernacle-today/)
- [Solomon's Splendor - Garden in Delight](https://www.gardenindelight.com/solomons-splendor/)
- [Tabernacle Replica - Timna Park](https://www.holylandsite.com/timna-tabernacle)
- [Trees, Plants, and Flowers - BiblePlaces.com](https://www.bibleplaces.com/16-trees-plants-flowers-of-the-holy-land-revised/)
