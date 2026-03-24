# Grass & Foliage Shadow Rendering Research

**Date:** 2026-03-23
**Scope:** Shadow casting, shadow receiving, self-shadowing, translucency, and ambient occlusion for instanced grass in a real-time 3D engine
**Engine:** Vestige (C++17, OpenGL 4.5, forward renderer)
**Target Hardware:** AMD RX 6600 (RDNA2), 60 FPS minimum
**Current State:** Grass rendered as instanced 3-quad star meshes; CSM with 4 cascades at 2048x2048; PCSS soft shadows with Poisson disk for scene objects; grass has NO shadow support

---

## Table of Contents

1. [Grass Receiving Shadows](#1-grass-receiving-shadows)
2. [Grass Casting Shadows](#2-grass-casting-shadows)
3. [Performance Considerations](#3-performance-considerations)
4. [Normal Bias and Self-Shadowing](#4-normal-bias-and-self-shadowing)
5. [Ambient and Diffuse Lighting for Grass](#5-ambient-and-diffuse-lighting-for-grass)
6. [Open Source Implementations](#6-open-source-implementations)
7. [Recommended Approach for Vestige](#7-recommended-approach-for-vestige)
8. [Sources](#8-sources)

---

## 1. Grass Receiving Shadows

### 1.1 Core Technique: Sample the Shadow Map in the Grass Fragment Shader

The fundamental technique for grass receiving shadows is the same as for any other geometry: transform the fragment position into light space, look up the cascaded shadow map, and compare depths. Engines like Unity, Unreal, and Godot all do this when shadow receiving is enabled for foliage materials.

In Unity, grass receiving shadows uses the same `SHADOW_ATTENUATION(i)` macro as any other surface, which samples the shadow map and returns a 0-1 shadow factor. The fragment shader multiplies the final color by this factor. Source: Unity forum discussions and the TerrainGrassShader implementation on GitHub.

In Unreal Engine, foliage set to "dynamic" receives shadows from cascaded shadow maps within the configured Dynamic Shadow Distance. The CSM cascade is selected based on fragment depth, the same as for any other surface. Source: Unreal Engine documentation and community forums.

In Godot, any spatial material with `shadows_disabled` not set will automatically sample the shadow map in both the forward and deferred pipelines. For alpha-scissored grass, the color pass receives shadows through the standard shadow sampling path. Source: Godot documentation and engine source PRs.

### 1.2 No Special Magic Required

There is no fundamentally different technique for grass receiving shadows versus any other geometry. The process is:

1. Pass the fragment's world position from the vertex shader (the foliage vertex shader already computes `worldPos`).
2. In the fragment shader, determine the cascade index from view-space depth.
3. Transform the world position into light space using the cascade's light-space matrix.
4. Sample the cascaded shadow map array.
5. Apply PCF or simplified filtering.
6. Multiply the grass color by `(1.0 - shadow * shadowStrength)`.

The key design question is not whether to sample the shadow map but how many PCF samples to use and whether to use the full PCSS pipeline or a simplified filter (see Section 3 for performance analysis).

### 1.3 What Changes for Grass vs Scene Objects

The grass fragment shader needs access to the same shadow uniforms as the scene shader:
- `u_cascadeShadowMap` (sampler2DArray, the 4-cascade shadow map)
- `u_cascadeCount`, `u_cascadeSplits[]`, `u_cascadeLightSpaceMatrices[]`
- `u_cascadeTexelSize[]`
- Light direction for bias calculation

Since Vestige's foliage shader currently only takes `u_viewProjection`, `u_time`, wind parameters, and the grass texture, these shadow uniforms must be added. The vertex shader must also output `v_fragPosition` (world space) and `v_viewDepth` (view-space Z) so the fragment shader can index into the correct cascade.

---

## 2. Grass Casting Shadows

### 2.1 The Industry Consensus: Usually Don't

The overwhelming consensus across AAA engines and indie implementations is that **grass usually does not cast shadows into shadow maps**, or does so only for nearby grass with significant caveats.

Key evidence:

- **Fortnite (UE5):** Grass was explicitly excluded from Virtual Shadow Maps. The team instead relied on screen-space contact shadows. Reasons: (1) artists wanted control over shadow darkness to simulate transmission through grass, (2) rendering grass into shadow maps had a non-trivial performance cost, and (3) contact shadows from the grass depth buffer looked better for small geometry. Source: Unreal Engine tech blog, "Virtual Shadow Maps in Fortnite Battle Royale Chapter 4."

- **Ghost of Tsushima:** Full compute and vertex-pixel shader pipelines for grass shadow casting were deemed impractical. Instead, impostor height maps approximated grass shadow casting, combined with screen-space contact shadows for fine detail. Source: GDC 2021, "Procedural Grass in Ghost of Tsushima."

- **Godot grass implementations:** Multiple community implementations explicitly disable shadow casting (`Cast Shadow = Off`). The hexaquo grass rendering series states: "Games rarely render real shadows for small herbage: this would require lights to also render the grass, causing a big performance hit." Even when enabled, "it doesn't usually look clean because of how small and detailed grass geometry is." Source: hexaquo.at grass rendering series.

- **Unity terrain grass:** Shadow casting is available but documented as having "very high overhead." Unity provides a Shadow LOD Distance parameter to limit shadow casting to nearby grass only. Many Unity developers disable grass shadow casting entirely. Source: Unity documentation and forum discussions.

### 2.2 Alpha-Tested Shadow Maps for Grass

If grass does cast shadows, the shadow pass must run a fragment shader with alpha testing (since the star mesh quads are mostly transparent). This has significant implications:

**How it works:**
1. During the shadow depth pass, render grass geometry into the shadow map.
2. In the shadow pass fragment shader, sample the grass alpha texture.
3. If `texel.a < threshold`, call `discard` to prevent transparent pixels from writing depth.
4. Opaque grass pixels write their depth normally.

**The problem with `discard`:**
Using `discard` in a fragment shader prevents the GPU from performing early-Z rejection. Normally, a depth-only shadow pass is extremely fast because the GPU can reject fragments before running the fragment shader. With `discard`, the GPU must run the full fragment shader for every single fragment to determine if it should be kept. Tests show this can increase pixel shader invocations by 30-40% even when `discard` is never actually executed, because the mere presence of the instruction in the shader bytecode disables early-Z optimization. Source: therealmjp.github.io "To Early-Z, or Not To Early-Z"; Khronos OpenGL Wiki; GameDev.net forums.

On tile-based GPU architectures (mobile), `discard` is even more expensive as it can disable the tiled renderer optimization. Desktop AMD RDNA2 GPUs handle this better than mobile, but the cost is still measurable.

### 2.3 LOD-Based Shadow Casting

A practical compromise is to only render grass into the shadow map for the nearest cascade(s):

- **Cascade 0 only:** Render grass into the first cascade (highest resolution, covering ~10-20m from camera). Skip grass for cascades 1-3.
- **Shadow LOD distance:** Unity exposes this as a configurable parameter. Typical values are 20-30 meters.
- **Reduced instance count:** Only submit grass instances within the shadow casting distance to the shadow pass, avoiding GPU work for distant grass.

This limits the shadow pass overhead to a small fraction of the total grass instances while providing visible grass shadows where they matter most (close to the camera, where the player actually notices).

### 2.4 Dithered/Stippled Shadows

An alternative to alpha testing for grass shadow casting is **screen-door transparency** (dithered opacity):

**How it works:**
Instead of `discard` based on alpha, compare the alpha value against a Bayer matrix (ordered dither) threshold. Pixels that pass the threshold write depth normally. Pixels that fail are discarded.

```glsl
// 4x4 Bayer matrix threshold
float bayerThreshold(ivec2 pixel) {
    const float bayer[16] = float[16](
         0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
        12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
         3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
        15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0
    );
    int idx = (pixel.x % 4) + (pixel.y % 4) * 4;
    return bayer[idx];
}

// In shadow pass fragment shader:
float alpha = texture(u_texture, v_texCoord).a;
if (alpha < bayerThreshold(ivec2(gl_FragCoord.xy)))
    discard;
```

**Advantages:**
- The resulting shadow map, when filtered with PCF, produces soft semi-transparent shadow edges without requiring true alpha blending in the shadow map.
- Temporal anti-aliasing (if present) further smooths the dither pattern.
- The visual result approximates translucent shadows.

**Disadvantages:**
- Still requires `discard`, so early-Z is still disabled.
- Without TAA, the dither pattern can be visible as stipple noise.
- "Keeping the fading distance short is important because the more dithered area you have, the more expensive it is." Source: Unreal Engine foliage optimization notes.

### 2.5 Screen-Space Contact Shadows as an Alternative

Vestige already has a screen-space contact shadow pass that ray-marches the depth buffer. If grass writes to the depth buffer during the main scene render (which it already does since the foliage shader runs `discard` for transparent pixels), **contact shadows will naturally pick up grass self-shadowing** without any additional shadow map rendering cost.

This is exactly what Fortnite and Ghost of Tsushima do. The contact shadow pass provides:
- Sharp shadows at grass-ground contact points.
- No additional shadow pass draw calls.
- Works from the existing depth buffer.
- Limited range (screen space only, no off-screen shadows).

This is likely the best initial approach for Vestige.

---

## 3. Performance Considerations

### 3.1 Shadow Map Sampling Cost Per Grass Fragment

Each shadow map sample involves:
1. A matrix multiplication (world position to light space) -- 4 multiply-add operations.
2. A texture lookup from `sampler2DArray` -- 1 texture sample.
3. A depth comparison.

For basic shadow mapping (single sample, no PCF), this adds roughly **1 texture fetch + 10-15 ALU ops per fragment**. On RDNA2 at 1080p, this is negligible for individual fragments but becomes significant when multiplied across hundreds of thousands of grass fragments.

**Concrete benchmark data:** Going from no shadow sampling to a 2x2 PCF (4 samples) adds approximately 0.1-0.2ms at 1080p on mid-range desktop GPUs. Going from 2x2 PCF to 7x7 PCF adds approximately 0.4ms on an AMD 7950. Source: therealmjp.github.io "A Sampling of Shadow Techniques."

### 3.2 Simplified Shadow Filtering for Grass

**Do NOT use the full PCSS pipeline for grass.** The scene shader's PCSS uses 16-sample Poisson disk blocker search + 16-sample PCF filtering = 32 shadow map texture samples per fragment. For grass, this is overkill and wasteful.

Recommended filtering levels for grass:

| Technique | Samples | Visual Quality | Cost | Recommended? |
|-----------|---------|---------------|------|-------------|
| Hard shadow (1 sample) | 1 | Pixelated, harsh | Cheapest | Only for very distant grass |
| 2x2 PCF (hardware bilinear) | 1 (HW) | Slightly soft | Very cheap | Good minimum |
| 4-sample Poisson PCF | 4 | Decent softness | Cheap | Good default |
| 9-sample grid PCF | 9 | Soft | Moderate | Upper limit for grass |
| 16-sample PCSS | 32 | Full soft shadows | Expensive | No -- use for scene objects only |

**Recommendation:** Use a **4-sample rotated Poisson PCF** for grass shadow receiving. This provides visually acceptable soft shadow edges at a fraction of the PCSS cost. The rotated Poisson disk (using interleaved gradient noise to rotate per-fragment) breaks up banding artifacts just like the scene shader does, but with 4 samples instead of 32.

```glsl
// Simplified 4-sample PCF for grass shadows
float calcGrassShadow(vec3 worldPos, float viewDepth, vec3 lightDir)
{
    int cascade = getCascadeIndex(viewDepth);
    vec4 lightSpacePos = u_cascadeLightSpaceMatrices[cascade] * vec4(worldPos, 1.0);
    vec3 proj = lightSpacePos.xyz / lightSpacePos.w * 0.5 + 0.5;

    if (proj.z > 1.0) return 0.0;

    float bias = 0.003; // Larger constant bias for grass (see Section 4)
    vec2 texelSize = 1.0 / vec2(textureSize(u_cascadeShadowMap, 0).xy);

    // Rotate 4 Poisson samples per-fragment
    float angle = interleavedGradientNoise(gl_FragCoord.xy) * 6.28318;
    float cosA = cos(angle);
    float sinA = sin(angle);
    mat2 rot = mat2(cosA, sinA, -sinA, cosA);

    float shadow = 0.0;
    const vec2 samples[4] = vec2[4](
        vec2(-0.94201624, -0.39906216),
        vec2( 0.94558609, -0.76890725),
        vec2(-0.09418410, -0.92938870),
        vec2( 0.34495938,  0.29387760)
    );

    for (int i = 0; i < 4; i++)
    {
        vec2 offset = rot * samples[i] * 1.5 * texelSize;
        float d = texture(u_cascadeShadowMap,
            vec3(proj.xy + offset, float(cascade))).r;
        shadow += (proj.z - bias > d) ? 1.0 : 0.0;
    }
    return shadow * 0.25;
}
```

### 3.3 Shadow Map Resolution for Grass-Scale Detail

At 2048x2048 per cascade, with the first cascade covering ~20m from the camera, each texel represents approximately 1cm. Grass blades in the star mesh are typically 5-15cm wide, meaning grass occupies 5-15 texels in the shadow map. This is sufficient resolution for grass to receive shadows from buildings, trees, and terrain features. It is NOT sufficient for individual grass blades to cast distinct blade-shaped shadows (which would require sub-centimeter texel resolution).

This is actually desirable -- you want grass to receive soft, blob-like shadows from large casters, not per-blade shadow detail.

### 3.4 Impact of Alpha Testing on Shadow Pass Performance

If grass is rendered into the shadow map with alpha testing:

- **Without alpha test (opaque objects):** Shadow pass runs depth-only with no fragment shader. The GPU uses early-Z aggressively. Extremely fast.
- **With alpha test (`discard`):** Fragment shader must run for every rasterized fragment. Early-Z is disabled for the entire draw call. Tests in Godot showed shadow pass time tripling from ~20us to ~60us for a single material when alpha scissors were added. Source: Godot engine proposal #7366.
- **Scaling:** With thousands of grass instances across 4 cascades, this overhead multiplies. At 100k grass instances, each with ~12 vertices (6 triangles), the shadow pass processes 1.2M vertices + associated fragments per cascade. Across 4 cascades, that is 4.8M additional vertices in the shadow pass.

**Recommendation:** Do not render grass into shadow maps initially. Rely on contact shadows. If grass shadow casting is later desired, limit it to cascade 0 only.

---

## 4. Normal Bias and Self-Shadowing

### 4.1 The Thin Geometry Problem

Grass star meshes are extremely thin -- each quad has zero thickness. This makes them uniquely problematic for shadow mapping:

- **Standard front-face culling trick fails.** A common shadow map optimization is to render only back faces into the shadow map, which pushes shadow acne artifacts to the dark side of objects. But billboards and thin quads have no "inside" -- culling front faces eliminates them entirely from the shadow map.
- **Slope-scaled bias is unreliable.** The standard slope-scaled bias `max(c * (1.0 - NdotL), min_bias)` assumes a continuous surface. Grass quads seen edge-on produce extreme NdotL values near zero, causing the bias formula to apply maximum bias and potentially creating peter-panning.
- **Two-sided rendering compounds the issue.** Grass must be rendered with backface culling disabled (so both sides are visible). This means shadow map depth values come from both front and back faces, and the standard back-face-culling trick for shadow maps cannot be used.

### 4.2 Recommended Bias Strategy for Grass Receiving Shadows

For grass **receiving** shadows (sampling the shadow map to determine if the grass fragment is in shadow from other objects), the bias situation is simpler because the shadow map was rendered from scene objects (buildings, terrain, trees), not from the grass itself:

- **Use a constant bias, not slope-scaled.** Grass normals are unreliable for bias calculation -- the quad normals point perpendicular to the quad surface, which has no meaningful relationship to shadow acne. A constant bias of 0.002-0.005 in NDC works well.
- **Do not use normal offset for grass.** Normal offset bias moves the shadow lookup position along the surface normal. For vertical grass quads, this would shift the lookup sideways (perpendicular to the quad), which is wrong. Use depth-only bias.
- **A slightly larger bias than scene objects is acceptable.** Grass does not need pixel-perfect shadow boundaries. A bias of 0.003 versus the scene shader's `max(0.002 * (1.0 - NdotL), 0.0003)` eliminates most artifacts without visible peter-panning.

### 4.3 Self-Shadowing Prevention

If grass casts shadows (into the shadow map), self-shadowing is a major problem:

- **Problem:** A grass blade writes depth into the shadow map. Adjacent blades in the same star mesh (or nearby instances) sample that depth and incorrectly determine they are in shadow.
- **Solution 1: Don't cast shadows.** The simplest and most common solution.
- **Solution 2: Increased bias for grass shadow casting.** Use a significantly larger bias (0.005-0.01) when rendering grass into the shadow map. Accept some peter-panning.
- **Solution 3: Separate shadow pass flags.** Render grass with a flag in the shadow pass that allows the shadow receiver shader to identify "grass shadow" vs "object shadow" and apply different attenuation. This is over-engineered for most use cases.
- **Solution 4: Cull grass from its own shadow cascade.** Only let grass cast shadows into the shadow map but exclude grass from receiving its own cast shadows. This is complex to implement but eliminates self-shadowing entirely.

**Recommendation:** Do not have grass cast shadows into the shadow map. Use the existing contact shadow system, which naturally handles self-occlusion with its thickness threshold check. Vestige's contact shadow shader already has: `if (depthDiff > 0.01 && depthDiff < thicknessThreshold)` which rejects overly thick occluders.

---

## 5. Ambient and Diffuse Lighting for Grass

### 5.1 Applying Shadow Factor to Grass Color

Vestige's current grass fragment shader outputs `texel.rgb * v_colorTint` with no lighting whatsoever. The texture color IS the final color. To apply shadows, the shadow factor needs to modulate this color.

**Approach 1: Multiplicative shadow (simple)**
```glsl
float shadow = calcGrassShadow(v_fragPosition, v_viewDepth, u_dirLightDirection);
vec3 shadowedColor = texel.rgb * v_colorTint * (1.0 - shadow * u_shadowStrength);
fragColor = vec4(shadowedColor, texel.a * v_alpha);
```
`u_shadowStrength` (0.0-1.0) controls how dark shadows get. A value of 0.5-0.7 is typical for grass to avoid pitch-black shadows that look unnatural on vegetation.

**Approach 2: Shadow with ambient floor**
```glsl
float shadow = calcGrassShadow(...);
vec3 ambient = texel.rgb * v_colorTint * u_ambientStrength;  // e.g., 0.3
vec3 lit = texel.rgb * v_colorTint;
fragColor = vec4(mix(lit, ambient, shadow), texel.a * v_alpha);
```
This ensures shadowed grass never goes fully black -- there is always some ambient light. This is more physically plausible since grass in shadow is still illuminated by sky light and bounce light.

### 5.2 Subsurface Scattering / Translucency Approximation

Grass blades are thin and semi-translucent. When the sun is behind a grass blade (backlit), real grass glows with transmitted light. This is a strong visual cue that is cheap to approximate.

**The wrap-diffuse translucency formula:**

```glsl
// In the grass fragment shader:
vec3 viewDir = normalize(u_cameraPos - v_fragPosition);
vec3 lightDir = normalize(-u_dirLightDirection);

// Standard NdotL (clamped to 0)
float NdotL = max(dot(v_normal, lightDir), 0.0);

// Translucency: light passing through from behind
// Use -normal to detect backlit condition
float backlit = max(dot(-v_normal, lightDir), 0.0);
float translucency = backlit * pow(max(dot(viewDir, -lightDir), 0.0), 2.0);

// Combine
vec3 grassColor = texel.rgb * v_colorTint;
vec3 directLight = grassColor * NdotL * u_dirLightColor;
vec3 transLight  = grassColor * translucency * u_translucencyStrength * u_dirLightColor;
vec3 ambientLight = grassColor * u_ambientColor;

float shadow = calcGrassShadow(...);
// Shadow affects direct light and partially affects translucency
vec3 finalColor = ambientLight + (directLight + transLight * 0.5) * (1.0 - shadow);
```

The translucency term is `max(dot(-N, L), 0.0) * pow(max(dot(V, -L), 0.0), exponent)`. The first dot checks if light hits the back of the blade. The second dot (view-aligned with light) creates a bright halo when looking toward the sun through grass. The exponent (2-4) controls how tight the highlight is.

**Crysis vegetation shading** used a similar approach with an artist-made "subsurface texture map" to approximate varying leaf thickness, with the formula combining `-N dot L` with `E dot L` (eye-to-light alignment). Source: GPU Gems 3, Chapter 16.

**Important:** Shadow should only partially attenuate translucency. In reality, even when a grass blade is in shadow from the front (something is between the sun and the blade), some light still transmits through the blade itself. Using `transLight * 0.5` for the shadow-affected portion preserves some translucent glow even in shadow.

### 5.3 Fake Ambient Occlusion at Grass Base

This technique is almost universally used in grass rendering and provides an enormous visual improvement for minimal cost:

**Vertex-based AO gradient:**
```glsl
// In vertex shader:
float heightFactor = a_position.y * 2.5; // 0 at base, ~1 at tip
v_ao = pow(heightFactor, 1.5); // Smooth darkening at base

// In fragment shader:
vec3 grassColor = texel.rgb * v_colorTint * mix(u_baseDarkness, 1.0, v_ao);
```

**Why it works:** Real grass is darkest at the base where blades overlap and occlude each other, and brightest at the tips where they catch full sunlight. This gradient approximates complex inter-blade occlusion with a single multiply.

**What engines do:**
- AMD GPUOpen procedural grass: "We fake a self-shadow effect by darkening the grass near its roots" using `pow((worldPos.y - rootHeight) / height, 1.5)`. Source: gpuopen.com procedural grass rendering.
- Three.js fluffy grass tutorial: "Just adding a dark base color drastically changes the appearance of the grass." Uses two color uniforms (base and tip) mixed along UV.y. Source: Codrops, "How to Make the Fluffiest Grass with Three.js."
- Hexaquo Godot grass: Uses `AO = bottom_to_top; AO_LIGHT_AFFECT = 1.0;` to darken the base. Source: hexaquo.at grass rendering series.
- Unity grass shader by Halisavakis: Uses Half-Lambert (`NdotL * 0.5 + 0.5`) combined with fresnel-based translucency. Source: halisavakis.com grass shader tutorial.

### 5.4 Combined Grass Lighting Model

Putting it all together, a complete grass lighting pass would be:

```glsl
// Inputs: worldPos, normal, texCoord, colorTint, viewDepth, heightAO
// Uniforms: shadow map, light direction, light color, camera pos

vec4 texel = texture(u_texture, v_texCoord);
if (texel.a < 0.5) discard;

// Base color with AO gradient
vec3 baseColor = texel.rgb * v_colorTint * mix(u_grassBaseDarkness, 1.0, v_ao);

// Shadow from CSM
float shadow = calcGrassShadow(v_fragPosition, v_viewDepth, u_dirLightDirection);

// Wrap-diffuse lighting (softer than standard Lambertian)
vec3 lightDir = normalize(-u_dirLightDirection);
float NdotL = dot(v_normal, lightDir) * 0.5 + 0.5; // Half-Lambert

// Translucency (backlit glow)
vec3 viewDir = normalize(u_cameraPos - v_fragPosition);
float backFacing = max(dot(-v_normal, lightDir), 0.0);
float viewLight = pow(max(dot(viewDir, -lightDir), 0.0), 3.0);
float translucency = backFacing * viewLight * u_translucencyStrength;

// Combine
vec3 ambient = baseColor * u_ambientColor;
vec3 direct  = baseColor * NdotL * u_dirLightColor;
vec3 trans   = baseColor * translucency * u_dirLightColor;

vec3 finalColor = ambient + (direct + trans) * (1.0 - shadow * u_grassShadowStrength);
fragColor = vec4(finalColor, texel.a * v_alpha);
```

---

## 6. Open Source Implementations

### 6.1 Godot Engine

**Shadow casting for foliage:**
Godot's spatial shader system supports alpha scissor with shadow casting, but this was historically buggy. Issue #19444 and PRs #58954/#58959 fixed alpha scissor shadow casting in both GLES3 and Vulkan backends. The fix ensures that the shadow pass fragment shader properly samples the alpha texture and calls `discard` for transparent pixels.

**Performance note:** Godot's custom shaders use "the exact same fragment code as the color pipeline" for shadow passes, "wasting valuable time" on unnecessary calculations. A developer testing with RenderDoc found that "the depth pass for a basic noise-based material with alpha scissors can take up to 60 microseconds, but by manually trimming the shader to only what's necessary for depth rendering (the alpha scissors), execution time was cut to 20 microseconds." This is a 3x overhead. Source: Godot proposal #7366, #4443.

**Godot community grass implementations:**
- **BastiaanOlij/godot-grass-tutorial:** GPU particle-based grass with compute shaders. Shadow casting is controlled per-MultiMeshInstance3D. Source: GitHub.
- **2Retr0/GodotGrass:** Ghost of Tsushima-inspired per-blade grass. Uses vertex color red channel for "root darkening amount" (AO) and green channel for wind phase. Source: GitHub.
- **FaRu85/Godot-Foliage:** Foliage shader using UV2 for random face rotation, red channel for alpha leaf shape. Source: GitHub.

### 6.2 Unity Terrain Grass

Unity's built-in terrain grass system provides these shadow-related settings:

- **Cast Shadows:** Toggle on grass detail prototypes. Documented as "very high overhead."
- **Shadow LOD Distance:** Distance up to which grass casts shadows. Controls performance by limiting shadow casting range.
- **Shadow Thickness:** Makes cast shadows more distinctive; should be paired with shadow strength adjustment on the light.
- **Receive Shadows:** Grass receives shadows via standard shadow attenuation in the lighting pass.

Unity uses `UnityApplyLinearShadowBias` in the shadow caster pass to reduce artifacts on thin geometry. The shadow pass uses a dedicated `LightMode = ShadowCaster` tag that runs a simplified shader.

**Community implementations:**
- **Acrosicious/TerrainGrassShader:** Real-time terrain grass shader that generates grass on the GPU per ground texture. Uses the deferred rendering path for "cheap lighting and shadows." Source: GitHub.
- **Stylized Grass Shader (Staggart):** Commercial Unity package with configurable shadow LOD distance, per-instance shadow fading, and contact/raytraced shadow support (HDRP only). Source: staggart.xyz.

### 6.3 OpenGL-Specific Implementations

- **DeveloperDenis/RealTimeGrassRendering:** C/C++ OpenGL demo using geometry-based grass. Does not implement shadow casting for grass due to performance. Source: GitHub.
- **LesleyLai/GLGrassRenderer:** OpenGL grass renderer inspired by the GPU Gems tessellation approach. No shadow support. Source: GitHub.
- **Flix01/Tiny-OpenGL-Shadow-Mapping-Examples:** Compact single-file OpenGL shadow mapping examples covering basic, PCF, variance, and cascaded shadow maps. Good reference for shadow sampling code. Source: GitHub.
- **damdoy/opengl_examples:** OpenGL examples collection including shadow mapping and grass. Grass uses instanced billboards with alpha test; shadow mapping is implemented separately. Source: GitHub.

### 6.4 Skyrim Community Shaders

The Skyrim Community Shaders project implements a complete grass lighting system:
- Soft lighting (uniform illumination from all directions) combined with backlighting for subsurface scattering.
- Higher SSS values make grass "appear more translucent and luminous when backlit, with effects particularly noticeable during sunrise/sunset."
- Grass collision system uses a 512x512 scrolling clipmap texture.
- Grass vertex shader samples a collision displacement buffer.
- Source: GitHub doodlum/skyrim-community-shaders, documented on DeepWiki.

---

## 7. Recommended Approach for Vestige

### 7.1 Phase 1: Grass Receives Shadows (Recommended First Step)

**Difficulty:** Low-Medium
**Performance impact:** ~0.1-0.3ms at 1080p with 4-sample PCF

Changes required:

1. **Modify `foliage.vert.glsl`:**
   - Add `uniform mat4 u_view;` to compute view-space depth.
   - Output `v_fragPosition` (world-space position after wind animation).
   - Output `v_viewDepth` (view-space Z for cascade selection).
   - Output `v_heightAO` (height-based ambient occlusion factor).

2. **Modify `foliage.frag.glsl`:**
   - Add cascade shadow map uniforms (same as `scene.frag.glsl`).
   - Add a simplified `calcGrassShadow()` function with 4-sample PCF (not full PCSS).
   - Apply shadow factor with ambient floor.
   - Add basic height-based AO darkening.
   - Add directional light uniforms for basic Half-Lambert diffuse.

3. **Modify `FoliageRenderer::render()`:**
   - Bind the cascade shadow map texture array.
   - Upload cascade uniforms to the foliage shader.
   - Upload light direction and color.

### 7.2 Phase 2: Fake AO + Half-Lambert Lighting (With Phase 1)

**Difficulty:** Low
**Performance impact:** Negligible (a few extra ALU ops per fragment)

Implement the base-darkening AO and Half-Lambert wrap lighting from Section 5.3-5.4. This provides the biggest visual improvement per cost and should be done alongside Phase 1.

### 7.3 Phase 3: Translucency / Backlit Effect (Optional Enhancement)

**Difficulty:** Low
**Performance impact:** Negligible (a few extra ALU ops)

Add the subsurface scattering approximation from Section 5.2. This makes sunset/sunrise scenes dramatically more beautiful. Requires passing the camera position and light color to the grass shader.

### 7.4 Phase 4: Grass Shadow Casting (Optional, Low Priority)

**Difficulty:** Medium-High
**Performance impact:** Potentially 1-3ms depending on instance count and cascade count

Only pursue this if the contact shadow system does not provide sufficient grass shadowing. If implemented:

1. Create a `foliage_shadow.vert.glsl` / `foliage_shadow.frag.glsl` pair.
2. The vertex shader applies the same wind animation as the main foliage shader (shadows must match the rendered grass position).
3. The fragment shader only samples the grass texture alpha and calls `discard`.
4. Render grass into cascade 0 only (or cascade 0 + 1 at most).
5. Only render instances within a configurable shadow casting distance (e.g., 25m).
6. Use a larger shadow bias (0.005+) for grass in the shadow pass.
7. Disable backface culling during the grass shadow pass (two-sided shadows).

### 7.5 Summary of Priorities

| Priority | Feature | Visual Impact | Performance Cost | Difficulty |
|----------|---------|--------------|-----------------|------------|
| 1 | Shadow receiving (4-sample PCF) | High | Low (~0.2ms) | Low-Medium |
| 1 | Height-based AO gradient | Very High | Negligible | Low |
| 1 | Half-Lambert diffuse lighting | High | Negligible | Low |
| 2 | Translucency/backlit glow | Medium-High | Negligible | Low |
| 3 | Shadow casting (cascade 0 only) | Low-Medium | Medium (1-2ms) | Medium |
| 4 | PCSS soft shadows for grass | Low | High | Medium |
| 5 | Dithered translucent shadows | Low | Medium | Medium |

---

## 8. Sources

### Engine Documentation & Tech Blogs
- [Virtual Shadow Maps in Fortnite Battle Royale Chapter 4 - Unreal Engine Tech Blog](https://www.unrealengine.com/en-US/tech-blog/virtual-shadow-maps-in-fortnite-battle-royale-chapter-4)
- [Virtual Shadow Maps in Unreal Engine Documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine)
- [Contact Shadows in Unreal Engine Documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/contact-shadows-in-unreal-engine)
- [Unity Manual: Grass and Other Details](https://docs.unity3d.com/6000.3/Documentation/Manual/terrain-Grass.html)
- [Godot 3D Lights and Shadows Documentation](https://docs.godotengine.org/en/stable/tutorials/3d/lights_and_shadows.html)
- [Unity ShadowCastingMode.TwoSided API Reference](https://docs.unity3d.com/ScriptReference/Rendering.ShadowCastingMode.TwoSided.html)

### GDC Talks & Academic
- [GDC 2021: Procedural Grass in Ghost of Tsushima (GDC Vault)](https://gdcvault.com/play/1027033/Advanced-Graphics-Summit-Procedural-Grass)
- [GDC 2021: Procedural Grass in Ghost of Tsushima (PDF)](https://archive.thedatadungeon.com/ghost_of_tsushima_2020/documents/gdc_2021/gdc_2021_procedural_grass_in_got.pdf)
- [SIGGRAPH 2021: Real-Time Samurai Cinema (Patry)](https://advances.realtimerendering.com/s2021/jpatry_advances2021/index.html)
- [SIGGRAPH Advances in Real-Time Rendering 2019](https://advances.realtimerendering.com/s2019/index.htm)
- [Physically Based Real-Time Translucency for Leaves (Habel et al., 2007)](https://www.cg.tuwien.ac.at/research/publications/2007/Habel_2007_RTT/Habel_2007_RTT-Preprint.pdf)

### GPU Gems & NVIDIA
- [GPU Gems 3, Ch.16: Vegetation Procedural Animation and Shading in Crysis](https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-16-vegetation-procedural-animation-and-shading-crysis)
- [GPU Gems 1, Ch.7: Rendering Countless Blades of Waving Grass](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-7-rendering-countless-blades-waving-grass)
- [GPU Gems 1, Ch.11: Shadow Map Antialiasing](https://developer.nvidia.com/gpugems/gpugems/part-ii-lighting-and-shadows/chapter-11-shadow-map-antialiasing)
- [GPU Gems 1, Ch.16: Real-Time Approximations to Subsurface Scattering](https://developer.nvidia.com/gpugems/gpugems/part-iii-materials/chapter-16-real-time-approximations-subsurface-scattering)
- [GPU Gems 2, Ch.17: Efficient Soft-Edged Shadows Using Pixel Shader Branching](https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-17-efficient-soft-edged-shadows-using)

### Technical Articles & Blogs
- [A Sampling of Shadow Techniques (MJP / TheRealMJP)](https://therealmjp.github.io/posts/shadow-maps/)
- [To Early-Z, or Not To Early-Z (MJP / TheRealMJP)](https://therealmjp.github.io/posts/to-earlyz-or-not-to-earlyz/)
- [Shadow Mapping Summary Part 1 (The Witness / Jonathan Blow)](http://the-witness.net/news/2013/09/shadow-mapping-summary-part-1/)
- [Shadowmap Bias (Render Diagrams)](https://renderdiagrams.org/2024/12/18/shadowmap-bias/)
- [Dealing with Shadow Map Artifacts (WillP GFX)](https://willpgfx.com/2015/05/dealing-with-shadow-map-artifacts/)
- [AMD GPUOpen: Procedural Grass Rendering with Mesh Shaders](https://gpuopen.com/learn/mesh_shaders/mesh_shaders-procedural_grass_rendering/)
- [Grass Shader Tutorial (Halisavakis)](https://halisavakis.com/my-take-on-shaders-grass-shader-part-ii/)
- [How to Make the Fluffiest Grass with Three.js (Codrops)](https://tympanus.net/codrops/2025/02/04/how-to-make-the-fluffiest-grass-with-three-js/)
- [Grass Rendering Series Part 2: Full-Geometry Grass in Godot (hexaquo)](https://hexaquo.at/pages/grass-rendering-series-part-2-full-geometry-grass-in-godot/)
- [Creating a Foliage Shader in Unity URP (NedMakesGames)](https://nedmakesgames.medium.com/creating-a-foliage-shader-in-unity-urp-shader-graph-5854bf8dc4c2)
- [Translucent Foliage Shader Graph (NedMakesGames)](https://nedmakesgames.github.io/blog/foliage-shader-graph)
- [Screen-Door Transparency (DigitalRune)](https://digitalrune.github.io/DigitalRune-Documentation/html/fa431d48-b457-4c70-a590-d44b0840ab1e.htm)
- [Dithering Transparency in Unity URP (Daniel Ilett)](https://danielilett.com/2020-04-19-tut5-5-urp-dither-transparency/)
- [Shadow Acne (DigitalRune)](https://digitalrune.github.io/DigitalRune-Documentation/html/3f4d959e-9c98-4a97-8d85-7a73c26145d7.htm)
- [Sparse Virtual Shadow Maps (J Stephano)](https://ktstephano.github.io/rendering/stratusgfx/svsm)
- [Foliage Optimization in Unity (Eastshade Studios)](https://eastshade.com/foliage-optimization-in-unity/)
- [PCF Shadow Acne (Johannes Jendersie)](https://jojendersie.de/pcf-shadow-acne/)
- [Efficient 2D Dithering Shader Snippets (Meta)](https://developers.meta.com/horizon/blog/tech-note-shader-snippets-for-efficient-2d-dithering/)
- [Simple Subsurface Scattering Shader (Halisavakis)](https://halisavakis.com/my-take-on-shaders-simple-subsurface-scattering/)

### Tutorials & Learning Resources
- [LearnOpenGL: Shadow Mapping](https://learnopengl.com/Advanced-Lighting/Shadows/Shadow-Mapping)
- [LearnOpenGL: Cascaded Shadow Mapping](https://learnopengl.com/Guest-Articles/2021/CSM)
- [LearnOpenGL: Instancing](https://learnopengl.com/advanced-opengl/instancing)
- [OpenGL Tutorial: Shadow Mapping](http://www.opengl-tutorial.org/intermediate-tutorials/tutorial-16-shadow-mapping/)
- [OGLDev: Percentage Closer Filtering](https://www.ogldev.org/www/tutorial42/tutorial42.html)
- [OGLDev: Cascaded Shadow Mapping](https://ogldev.org/www/tutorial49/tutorial49.html)
- [GLSL Programming: Translucent Bodies (Wikibooks)](https://en.wikibooks.org/wiki/GLSL_Programming/Unity/Translucent_Bodies)
- [Microsoft: Common Techniques to Improve Shadow Depth Maps](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps)
- [Microsoft: Cascaded Shadow Maps](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps)
- [Cascaded Shadow Maps with Soft Shadows (Alex Tardif)](https://alextardif.com/shadowmapping.html)

### Open Source Repositories
- [Acrosicious/TerrainGrassShader (Unity)](https://github.com/Acrosicious/TerrainGrassShader)
- [BastiaanOlij/godot-grass-tutorial (Godot)](https://github.com/BastiaanOlij/godot-grass-tutorial)
- [2Retr0/GodotGrass (Godot, Ghost of Tsushima-inspired)](https://github.com/2Retr0/GodotGrass)
- [FaRu85/Godot-Foliage (Godot)](https://github.com/FaRu85/Godot-Foliage)
- [DeveloperDenis/RealTimeGrassRendering (C++/OpenGL)](https://github.com/DeveloperDenis/Real-Time-Grass)
- [LesleyLai/GLGrassRenderer (OpenGL)](https://github.com/LesleyLai/GLGrassRenderer)
- [Flix01/Tiny-OpenGL-Shadow-Mapping-Examples (OpenGL)](https://github.com/Flix01/Tiny-OpenGL-Shadow-Mapping-Examples)
- [damdoy/opengl_examples (OpenGL)](https://github.com/damdoy/opengl_examples)
- [keijiro/ContactShadows (Unity)](https://github.com/keijiro/ContactShadows)
- [aleksandrpp/InteractiveGrass (Unity, screen-space shadows)](https://github.com/aleksandrpp/InteractiveGrass)
- [gkjohnson/unity-dithered-transparency-shader (Unity)](https://github.com/gkjohnson/unity-dithered-transparency-shader)
- [teadrinker/foliage-shader (Unity, leaves with translucency)](https://github.com/teadrinker/foliage-shader)
- [doodlum/skyrim-community-shaders (Skyrim, grass lighting)](https://deepwiki.com/doodlum/skyrim-community-shaders/6.2-grass-and-vegetation-systems)
- [harlan0103/Grass-Rendering-in-Modern-Game-Engine (Unity, GoT-inspired)](https://github.com/harlan0103/Grass-Rendering-in-Modern-Game-Engine)
- [Godot Alpha Scissor Shadow Fix PR #58959](https://github.com/godotengine/godot/pull/58959)
- [Godot Dithered Shadows Proposal #3276](https://github.com/godotengine/godot-proposals/issues/3276)

### Forum Discussions
- [Unity Forum: Terrain Grass Shadow Question](https://forum.unity.com/threads/terrain-grass-shadow-question.802155/)
- [Unity Forum: Grass Shadow with Deferred Rendering](https://forum.unity.com/threads/no-terrain-grass-shadow-with-deferred-rendering.881482/)
- [Unity Forum: How to Get Grass to Produce Shadows](https://forum.unity.com/threads/how-can-i-get-grass-to-produce-shadows-on-terrains.487071/)
- [Unreal Forum: How Shadows Work for Foliage Tool](https://forums.unrealengine.com/t/help-me-understand-how-shadows-work-for-the-foliage-tool/138493)
- [Unreal Forum: CSM Performance](https://forums.unrealengine.com/t/directionallight-shadowdepths-performance-cascaded-shadow-maps/2531105)
- [GameDev.net: Grass Shadows](https://www.gamedev.net/forums/topic/423966-grass-shadows/3821626/)
- [GameDev.net: Why Discard Takes a Performance Hit](https://www.gamedev.net/forums/topic/655602-why-discard-pixel-take-a-noticeable-performance-hit/)
- [Khronos Forum: Ignoring Discard for Depth Buffer Performance](https://community.khronos.org/t/is-there-anything-to-ignore-discard-glsl-instructions-to-improve-depth-buffer-performance/108948)
- [Polycount: Shadow on Foliage Paint Too Dark](https://polycount.com/discussion/212149/shadow-on-foliage-paint-are-way-too-dark)
