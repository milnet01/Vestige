# CSM Foliage Shadow Frustum Clipping Research

**Date:** 2026-03-25
**Scope:** Why small objects (grass, foliage) lose their shadows in cascaded shadow maps when looking steeply downward, and how to fix it
**Engine:** Vestige (C++17, OpenGL 4.5, forward renderer)
**Current State:** CSM with 4 cascades at 2048x2048, SDSM (Sample Distribution Shadow Maps) depth-bounds optimization, 10% + 5m XY padding in `computeCascadeMatrix`, 50% Z-range extension

---

## Table of Contents

1. [The Problem](#1-the-problem)
2. [Root Cause Analysis](#2-root-cause-analysis)
3. [Technique 1: Bounding Sphere Cascade Fitting (Stable CSM)](#3-technique-1-bounding-sphere-cascade-fitting-stable-csm)
4. [Technique 2: Minimum Cascade World Size Floor](#4-technique-2-minimum-cascade-world-size-floor)
5. [Technique 3: Scene AABB Intersection for Z Bounds](#5-technique-3-scene-aabb-intersection-for-z-bounds)
6. [Technique 4: Shadow Pancaking / Depth Clamping](#6-technique-4-shadow-pancaking--depth-clamping)
7. [Technique 5: SDSM Conservative Margins](#7-technique-5-sdsm-conservative-margins)
8. [Technique 6: Extended Shadow Caster Frustum](#8-technique-6-extended-shadow-caster-frustum)
9. [How Production Engines Handle This](#9-how-production-engines-handle-this)
10. [Recommended Approach for Vestige](#10-recommended-approach-for-vestige)
11. [Sources](#11-sources)

---

## 1. The Problem

When the camera looks steeply downward (e.g., pitch near -90 degrees), grass and foliage objects that are clearly visible on screen fail to cast shadows, or their shadows are clipped at the edges of the shadow map. The problem is worst when SDSM is active because SDSM tightens cascade bounds to the range of actual scene depth, which, when looking straight down, produces very thin depth slices.

### What happens geometrically

When the camera looks down at the ground at a steep angle:

1. **The camera frustum becomes nearly flat in world space.** A sub-frustum covering e.g. 5m to 20m of depth, when looking straight down, is a flat slab about 15m thick vertically but the full screen width/height horizontally.

2. **SDSM further tightens this.** If the visible geometry only spans 2m to 8m of depth (because the ground is flat), SDSM compresses the cascade splits into that tiny range. Each cascade now covers an even thinner depth slice.

3. **In light space, a thin horizontal slab becomes a thin vertical slab.** When the light direction is not aligned with the camera's look direction (e.g., the sun at 45 degrees), the camera's flat sub-frustum, projected into light space, becomes a narrow strip. The resulting orthographic XY bounds can be very small in one dimension.

4. **Small XY bounds means shadow casters at the edge are clipped.** Grass blades or foliage that extend even slightly beyond the tight AABB in light space are not captured by the shadow map, producing missing or partially clipped shadows.

5. **The 5m minimum padding in Vestige's current code helps but is not sufficient.** When the cascade covers a thin enough depth slice (SDSM-compressed), the light-space footprint can be smaller than the grass shadow casting distance, and 5 world-units of padding is not enough to cover all visible shadow casters.

---

## 2. Root Cause Analysis

The root cause is the fundamental approach of computing the cascade orthographic projection solely from the 8 corners of the camera sub-frustum. This approach is correct for scene-scale objects (buildings, terrain) but breaks down for small, dense objects like grass because:

### 2.1 Tight AABB changes size with camera orientation

The AABB computed from frustum corners in light space changes both its size and aspect ratio as the camera rotates. When looking horizontally, the sub-frustum projected into light space is roughly square. When looking straight down, it degenerates into a thin slab. This is the classic "fit to cascade" vs "fit to scene" tradeoff documented extensively by Microsoft and NVIDIA:

> "The problem with fitting to cascade is that the orthographic projection grows and shrinks based on the orientation of the view frustum."
> -- Microsoft, "Cascaded Shadow Maps" documentation

### 2.2 SDSM compounds the issue

Without SDSM, cascade splits span from `cameraNear` to `shadowDistance` (e.g., 0.1 to 150m), producing reasonable sub-frustum volumes even when looking down. With SDSM, if visible geometry only spans 2m to 8m, all four cascades are packed into that 6m range. Each cascade covers roughly 1.5m of depth. Projected into light space at a 45-degree sun angle, this produces an orthographic frustum that is approximately `screenWidth * 1.5m` on one axis and only `~2m` on the other. Shadow casters beyond that 2m extent in light space X or Y are lost.

### 2.3 The Z-extension helps shadow casters behind the camera, not at the sides

Vestige currently extends `minZ` and `maxZ` by 50% of the Z range. This helps capture shadow casters behind the camera (which is the standard recommendation from GPU Gems, Microsoft, and NVIDIA). However, it does nothing for the XY clipping problem, which is about the width and height of the orthographic frustum, not its depth.

### 2.4 Foliage shadow distance is independent of cascade bounds

The foliage shadow rendering system uses `shadowMaxDistance` (a distance from camera) to select which grass instances to render. But the shadow map's orthographic frustum determines which of those rendered instances actually contribute to the shadow texture. If the ortho frustum is too narrow, instances that are rendered are still clipped by the shadow map's XY bounds.

---

## 3. Technique 1: Bounding Sphere Cascade Fitting (Stable CSM)

**Origin:** Michal Valient, "Rendering Cascaded Shadow Maps" (ShaderX6, 2008); NVIDIA Dimitrov paper (2007); widely adopted by Unreal Engine, Unity, The Witness, Filament, etc.

### Concept

Instead of computing a tight AABB around the sub-frustum corners in light space, compute a **bounding sphere** around the 8 corners, then use the sphere's diameter as both the width and height of the orthographic projection. Because a sphere is rotation-invariant, the projection never changes size as the camera rotates.

### Algorithm

```
1. Compute the 8 world-space corners of the cascade sub-frustum.
2. Compute the center of these 8 corners (average).
3. Compute the radius = max distance from center to any corner.
4. Ortho width = radius * 2, ortho height = radius * 2.
5. Use these as the orthographic projection bounds.
6. Snap to texel grid as before.
```

### Advantages

- **Eliminates shadow shimmer from rotation** — the projection is the same size regardless of camera angle.
- **Guarantees minimum coverage** — the sphere always encloses the entire sub-frustum, so no corner is ever clipped.
- **Prevents the degenerate thin-slab problem** — even when looking straight down, the sphere radius is determined by the diagonal of the sub-frustum, which is always larger than its thinnest dimension.

### Disadvantages

- **Wastes shadow map resolution.** A sphere always encloses more volume than a tight AABB. For a typical perspective sub-frustum, the sphere can be 30-60% larger than the tight AABB, meaning 30-60% of shadow map texels cover empty space.
- **Small objects may still be too small to resolve.** If the cascade covers a wide area (large sphere), individual grass blades may be sub-texel in the shadow map.

### Resolution

This is the industry-standard approach and is the single most impactful fix for the rotation-dependent shadow clipping problem. The resolution waste is an acceptable tradeoff in most production engines.

---

## 4. Technique 2: Minimum Cascade World Size Floor

**Origin:** Common in production engines; discussed in Microsoft CSM docs and gamedev.net forums.

### Concept

After computing the orthographic bounds (whether from tight AABB or bounding sphere), clamp the width and height to a **minimum world-space size**. This prevents cascades from becoming degenerate when the sub-frustum projects to a thin strip.

### Algorithm

```
float orthoWidth = maxX - minX;
float orthoHeight = maxY - minY;
float minCascadeSize = 10.0f; // Minimum 10 world-units per cascade

if (orthoWidth < minCascadeSize) {
    float center = (minX + maxX) * 0.5f;
    minX = center - minCascadeSize * 0.5f;
    maxX = center + minCascadeSize * 0.5f;
}
if (orthoHeight < minCascadeSize) {
    float center = (minY + maxY) * 0.5f;
    minY = center - minCascadeSize * 0.5f;
    maxY = center + minCascadeSize * 0.5f;
}
```

### Advantages

- **Simple to implement** — just a few lines of code.
- **Directly addresses the problem** — prevents the degenerate thin-slab case.
- **Tunable per cascade** — cascade 0 (nearest) might need a different minimum than cascade 3 (farthest).

### Disadvantages

- **Wastes resolution when it activates** — if the minimum is larger than the computed bounds, the shadow map covers area that has no visible geometry.
- **Does not solve shimmer** — the projection size still changes with camera rotation (it's just clamped at a floor).
- **Requires tuning** — the minimum size depends on the foliage shadow casting distance and the scale of the scene.

### Resolution

This is a pragmatic fix that directly addresses the symptom. Best combined with Technique 1 (bounding sphere) which inherently provides a minimum size based on the frustum diagonal.

---

## 5. Technique 3: Scene AABB Intersection for Z Bounds

**Origin:** Microsoft "Common Techniques to Improve Shadow Depth Maps"; GPU Gems 3, Chapter 10; NVIDIA Dimitrov CSM paper.

### Concept

Instead of computing the light-space Z bounds purely from the camera sub-frustum, intersect the scene's bounding volume with the light frustum's four XY planes. Use the resulting Z range (which includes all shadow casters that could cast into the visible area) as the near/far planes.

### Algorithm (from Microsoft docs)

```
1. Compute the four XY-aligned planes of the light frustum from the sub-frustum corners.
2. Clip the scene AABB against these four planes.
3. The clipped AABB's min/max Z values in light space become the near and far planes.
4. This captures shadow casters that are outside the camera frustum but inside the
   light's XY column.
```

### The GPU Gems 3 approach

> "The bounding box's minimum z-value is set to 0 because the near plane position should remain unchanged, as there might be shadow casters between the near plane of the split's bounding box and the light's near plane."
> -- GPU Gems 3, Chapter 10

### Advantages

- **Captures off-screen shadow casters** — objects behind the camera or to its sides that cast shadows into the visible area.
- **Optimal Z precision** — the near/far range is exactly what the scene geometry needs.

### Disadvantages

- **Requires a scene AABB** — you need to know the bounding box of all potentially shadow-casting geometry. For dynamic scenes, this may need to be conservative (e.g., terrain AABB + some margin).
- **Does not fix XY clipping** — this addresses Z bounds, not the XY shrinkage problem.
- **Can cause projection size changes** — varying the Z range from frame to frame can cause shadow detail to shift.

### Resolution

Useful for the Z dimension, but does not directly solve the XY foliage clipping problem. Vestige already extends Z by 50%, which is a simpler version of this idea.

---

## 6. Technique 4: Shadow Pancaking / Depth Clamping

**Origin:** Catlike Coding "Custom SRP Directional Shadows"; Bevy engine PR #8877; widely used in Unity, Unreal, Godot.

### Concept

When shadow casters are in front of the shadow camera's near plane (because the near plane was pushed forward for depth precision), they get clipped and their shadows disappear. **Shadow pancaking** solves this by clamping the projected Z to the near plane in the vertex shader, effectively "flattening" shadow casters onto the near plane.

### Implementation (Vertex Shader)

```glsl
// In shadow caster vertex shader, after projection:
gl_Position = u_lightSpaceMatrix * vec4(worldPos, 1.0);
gl_Position.z = max(gl_Position.z, -gl_Position.w); // Clamp to near plane (OpenGL NDC)
```

### Alternative: GL_DEPTH_CLAMP (Hardware)

```cpp
glEnable(GL_DEPTH_CLAMP);
// ... render shadow casters ...
glDisable(GL_DEPTH_CLAMP);
```

This is the hardware equivalent: the rasterizer clamps fragment depth to [0,1] instead of clipping it.

### Advantages

- **Zero shadow map resolution cost** — no change to the orthographic projection.
- **Simple to implement** — one line in the vertex shader, or one GL state change.
- **Handles shadow casters behind the camera** — objects that cross the near plane are flattened instead of clipped.

### Disadvantages

- **Only fixes Z clipping, not XY clipping.** If the shadow caster is outside the left/right/top/bottom bounds of the orthographic projection, pancaking does not help — the object is simply not rasterized.
- **Can cause depth precision issues** with overlapping casters near the near plane.
- **Bevy PR #8877 found a bug** with the original vertex shader approach: interpolating the clamped clip position gives incorrect depth values for objects that intersect the near plane at a steep angle. The fix is to write the unclamped depth in a separate varying and use that for the depth buffer, while only clamping the position for clipping.

### Resolution

Useful complement to other techniques (handles the Z axis), but does not solve the XY clipping problem that is the primary issue for foliage when looking down.

---

## 7. Technique 5: SDSM Conservative Margins

**Origin:** Intel SDSM paper (Lauritzen, SIGGRAPH 2010); practical considerations from MJP's "A Sampling of Shadow Techniques."

### Concept

The core insight is that SDSM can be *too* aggressive in tightening bounds. The depth buffer only captures what is already visible, but shadow casters outside the depth buffer may still need to cast shadows into the visible area. Several mitigation strategies:

### 7.1 Larger SDSM margins

Vestige currently applies:
```cpp
effectiveNear = std::max(cameraNear, m_depthBoundsNear * 0.9f);  // 10% margin
effectiveFar = std::min(shadowFar, m_depthBoundsFar * 1.1f);     // 10% margin
```

Increasing these margins (e.g., 0.7 near / 1.3 far) widens the cascade distribution, giving each cascade a larger sub-frustum and thus a larger light-space footprint.

### 7.2 Minimum effective range

Clamp the effective range to never be smaller than some minimum:
```cpp
float effectiveRange = effectiveFar - effectiveNear;
float minRange = 20.0f; // At least 20m of depth range
if (effectiveRange < minRange) {
    float center = (effectiveNear + effectiveFar) * 0.5f;
    effectiveNear = std::max(cameraNear, center - minRange * 0.5f);
    effectiveFar = std::min(shadowFar, center + minRange * 0.5f);
}
```

### 7.3 Disable SDSM when looking steeply down

SDSM provides the most benefit when there is large depth variation in the scene (e.g., looking horizontally across a landscape). When looking straight down at flat ground, there is minimal depth variation and SDSM adds little value while creating the tight-bounds problem. Detect steep downward look angles and reduce or disable SDSM:

```cpp
float cameraPitch = acos(dot(camera.getForward(), vec3(0, 1, 0)));
float pitchFactor = smoothstep(0.3, 0.1, abs(cameraPitch - PI/2)); // 0..1 based on how vertical
if (pitchFactor < 0.5f) {
    clearDepthBounds(); // Revert to fixed splits
}
```

### Advantages

- **Addresses the compound SDSM + looking down problem directly.**
- **Low implementation cost.**

### Disadvantages

- **Wider margins reduce SDSM's benefit** — the whole point of SDSM is tight bounds.
- **Disabling SDSM based on camera angle introduces a discontinuity** — shadows pop as the camera crosses the threshold.

### Resolution

A minimum effective range (7.2) is the least disruptive option. It preserves SDSM's benefit when depth variation is large and only kicks in when the range becomes degenerate.

---

## 8. Technique 6: Extended Shadow Caster Frustum

**Origin:** DigitalRune documentation "Shadow Caster Culling"; Babylon.js CSM docs; Unity directional light shadow implementation.

### Concept

Rather than trying to make the shadow map's orthographic projection cover all possible shadow casters, expand the **culling frustum** used to select which objects are rendered into the shadow map. Objects that lie outside the cascade's orthographic projection but whose shadows could fall within it (because the light direction angles them in) should still be rendered.

### The Shadow Volume Approach

For each shadow caster, extend its bounding volume along the light direction to form a "shadow volume." If this extended volume intersects the cascade's orthographic frustum, the caster should be rendered. This is how DigitalRune describes it:

> "Camera Frustum Culling estimates the extent of the shadow, comparing the resulting shadow volume with the viewing frustum, and marking the shadow caster as hidden if the shadow volume does not intersect with the viewing frustum."

### The Light Column Approach (simpler)

Instead of computing per-caster shadow volumes, expand the cascade's orthographic XY bounds in the direction opposite to the light to capture shadow casters that are "upstream" along the light direction:

```
1. Transform the cascade frustum corners to light space.
2. Compute the AABB (as currently done).
3. Extend the XY bounds in the direction that captures objects whose shadows
   would fall inside the original bounds.
4. This is the same as extending the AABB in all XY directions by the maximum
   possible shadow projection offset.
```

For grass (height ~0.4m) with a sun at 45 degrees, the maximum shadow length is approximately:
```
shadow_length = grass_height / tan(sun_elevation) = 0.4m / tan(45) = 0.4m
```

This means grass 0.4m outside the frustum edge could cast shadows into the frustum. The current 5m padding already covers this. But the problem is not individual grass blades being outside the frustum -- it is the frustum itself being too narrow to contain the visible grass area.

### Advantages

- **Correct culling** — only renders shadow casters that actually contribute.
- **No wasted shadow map resolution** — the projection does not need to be expanded.

### Disadvantages

- **Complex to implement correctly** — computing shadow volumes or extended culling regions requires careful geometry.
- **Does not fix the core XY bounds problem** — if the orthographic projection is too narrow, even perfectly culled shadow casters will not fit in the shadow map.

### Resolution

This technique is useful for culling optimization but does not solve the fundamental problem. The XY bounds of the orthographic projection itself must be expanded.

---

## 9. How Production Engines Handle This

### 9.1 Unreal Engine (UE4 CSM / UE5 VSM)

**Approach:** Bounding sphere per cascade. Each cascade's orthographic projection is sized from the bounding sphere of the sub-frustum, ensuring rotation-invariant coverage. Combined with texel snapping for stability.

For foliage specifically, UE5's Virtual Shadow Maps handle small geometry differently: the shadow map is a virtual texture that pages in high-resolution tiles where needed. For CSM (still used in UE4 and as fallback), grass typically does not cast shadows into CSM. Instead, screen-space contact shadows provide near-camera grass shadowing.

Source: Unreal Engine documentation; "Virtual Shadow Maps in Fortnite Battle Royale Chapter 4" tech blog.

### 9.2 Unity

**Approach:** Bounding sphere per cascade (in URP/HDRP), with configurable shadow distance and cascade split ratios. Shadow casters outside the camera frustum are included by extending the culling volume along the light direction.

For foliage, Unity uses `Shadow LOD Distance` to limit which grass instances cast shadows, and the shadow cascade's near plane is pushed back far enough to include off-screen casters. **Shadow pancaking** is applied in the vertex shader (Catlike Coding documents this in detail).

Source: Unity documentation "Directional light shadows"; Catlike Coding "Custom SRP: Directional Shadows."

### 9.3 Godot

**Approach:** Tight AABB fitting per cascade, with proposals (issue #3908) to switch to bounding sphere for stability. Godot extends the Z range considerably to catch shadow casters behind the camera. For foliage, shadow casting is typically disabled (too expensive), relying on baked or screen-space shadows.

Source: Godot documentation; godotengine/godot-proposals #3908 and #6948.

### 9.4 The Witness

**Approach:** Used Valient's bounding sphere method, then optimized by pushing spheres forward to minimize waste. Also explored bounding rectangles rotated at different angles (testing 100 orientations over 90 degrees) to find the tightest fit while maintaining stability.

Source: The Witness blog, "Graphics Tech: Shadow Maps (part 1)" and "(part 2)."

### 9.5 Bevy Engine

**Approach:** Bounding sphere per cascade. Documented issues with shadow caster culling (#10397) where objects are not aggressively enough culled because the sphere-based frustum is conservative. Also fixed shadow pancaking depth precision (PR #8877) where clamped clip positions produced incorrect depth interpolation.

Source: bevyengine/bevy issues #10397, #16076; PR #8877.

### 9.6 Filament (Google)

**Approach:** Multiple shadow cascade splitting strategies (`computePracticalSplits`, `computeLogSplits`, `computeUniformSplits`). Fixed a shadow clipping bug (PR #2259) caused by aggressive box-frustum intersection that over-clipped the shadow map.

Source: google/filament PR #2259; issue #6293.

### 9.7 Summary Table

| Engine    | Cascade Shape   | Z Extension       | Foliage Shadows     | Pancaking |
|-----------|-----------------|-------------------|---------------------|-----------|
| UE4/5     | Bounding sphere | Scene-dependent   | Contact shadows     | Yes       |
| Unity     | Bounding sphere | Caster extension  | LOD distance limit  | Yes       |
| Godot     | Tight AABB*     | Large Z extension | Usually disabled    | No        |
| Witness   | Bounding sphere | Optimized         | N/A (no grass)      | Yes       |
| Bevy      | Bounding sphere | Scene bounds      | Standard            | Yes (fixed)|
| Filament  | AABB + clipping | Scene intersection| Standard            | Yes       |
| **Vestige** | **Tight AABB** | **50% Z extend** | **Distance-limited** | **No**  |

*Godot has proposals to switch to bounding sphere.

---

## 10. Recommended Approach for Vestige

Based on this research, the following changes are recommended in priority order:

### 10.1 PRIMARY FIX: Switch to Bounding Sphere Cascade Fitting

This is the single most impactful change and what virtually every production engine does. Replace the tight AABB in `computeCascadeMatrix` with a bounding sphere:

```cpp
// After computing frustum corners in light space...

// Compute bounding sphere of the frustum corners (in world space)
glm::vec3 center(0.0f);
for (const auto& corner : frustumCorners) {
    center += corner;
}
center /= 8.0f;

float radius = 0.0f;
for (const auto& corner : frustumCorners) {
    radius = std::max(radius, glm::length(corner - center));
}

// Round up radius to texel grid for stability
float texelSize = (radius * 2.0f) / static_cast<float>(resolution);
radius = std::ceil(radius / texelSize) * texelSize;

// Light view from sphere center
glm::mat4 lightView = glm::lookAt(center - lightDir, center, up);

// Fixed-size orthographic projection from sphere
float minX = -radius, maxX = radius;
float minY = -radius, maxY = radius;
// Z still computed from scene/frustum bounds (sphere only fixes XY)
```

**Why this works for the looking-down case:** When looking straight down, the sub-frustum is a thin horizontal slab. Its tight AABB in light space might be 40m x 3m. But its bounding sphere radius is determined by the diagonal -- approximately 20m. So the orthographic projection becomes 40m x 40m (the sphere's diameter), which easily covers all visible grass.

**Cost:** Some wasted shadow map texels (estimated 30-50% in typical cases). This is the standard tradeoff accepted by every major engine.

### 10.2 SECONDARY FIX: Add GL_DEPTH_CLAMP for shadow passes

Enable hardware depth clamping during shadow rendering to prevent Z-axis clipping:

```cpp
// In renderShadowPass, before rendering:
glEnable(GL_DEPTH_CLAMP);

// ... render all shadow casters ...

// After all cascades:
glDisable(GL_DEPTH_CLAMP);
```

This is simpler than vertex shader pancaking and handles the Z-axis clipping for shadow casters behind the near plane. It is supported on all OpenGL 3.2+ hardware including AMD RDNA2.

### 10.3 TERTIARY FIX: Add minimum effective range to SDSM

Prevent SDSM from compressing the cascade range to a degenerate sliver:

```cpp
// In CascadedShadowMap::update(), after computing effectiveNear/effectiveFar:
float effectiveRange = effectiveFar - effectiveNear;
float minRange = 15.0f; // Never less than 15m of depth range for cascades
if (effectiveRange < minRange) {
    float center = (effectiveNear + effectiveFar) * 0.5f;
    effectiveNear = std::max(cameraNear, center - minRange * 0.5f);
    effectiveFar = std::min(shadowFar, center + minRange * 0.5f);
    // Re-check validity
    if (effectiveNear >= effectiveFar) {
        effectiveNear = cameraNear;
        effectiveFar = shadowFar;
    }
}
```

### 10.4 OPTIONAL: Remove the existing 10% + 5m XY padding

Once bounding sphere fitting is in place, the existing XY padding in `computeCascadeMatrix` (lines 153-166) becomes unnecessary. The bounding sphere inherently provides more than enough XY coverage. Removing the padding simplifies the code and avoids double-expansion. The Z extension (lines 149-151) should be kept.

### 10.5 Implementation Order

1. **Bounding sphere** (fixes the core problem)
2. **GL_DEPTH_CLAMP** (fixes Z-axis clipping, one line)
3. **SDSM minimum range** (prevents degenerate SDSM compression)
4. **Remove XY padding** (cleanup after bounding sphere is confirmed working)

### 10.6 Testing

- **Look straight down** at a grass-covered area. Grass shadows should be visible in all cascades.
- **Rotate camera 360 degrees** while looking at shadows. Shadows should not shimmer, swim, or change coverage.
- **Toggle SDSM on/off** while looking down. Shadows should be present in both modes.
- **Check shadow map resolution** with the frame diagnostics overlay. The bounding sphere will show larger ortho extents but consistent shadow coverage.
- **Performance:** The bounding sphere should have no measurable performance impact (it changes the projection matrix, not the number of draw calls).

---

## 11. Sources

### Primary References

- [Microsoft: Cascaded Shadow Maps](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps) -- Fit-to-scene vs fit-to-cascade, bounding sphere stabilization, texel snapping
- [Microsoft: Common Techniques to Improve Shadow Depth Maps](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps) -- Scene AABB intersection for Z bounds, near/far plane optimization
- [NVIDIA: Cascaded Shadow Maps (Dimitrov, 2007)](https://developer.download.nvidia.com/SDK/10.5/opengl/src/cascaded_shadow_maps/doc/cascaded_shadow_maps.pdf) -- Original CSM algorithm and frustum fitting
- [Alex Tardif: Cascaded Shadow Maps with Soft Shadows](https://alextardif.com/shadowmapping.html) -- Complete CSM implementation with frustum fitting and texel snapping
- [MJP: A Sampling of Shadow Techniques](https://therealmjp.github.io/posts/shadow-maps/) -- Comprehensive survey of shadow mapping techniques including SDSM and depth clamping
- [LearnOpenGL: Cascaded Shadow Maps](https://learnopengl.com/Guest-Articles/2021/CSM) -- Tutorial-level CSM implementation

### Engine References

- [Catlike Coding: Custom SRP Directional Shadows](https://catlikecoding.com/unity/tutorials/custom-srp/directional-shadows/) -- Unity shadow pancaking, cascade culling, near plane clamping
- [Bevy Engine: Improve culling for cascaded shadow maps (Issue #10397)](https://github.com/bevyengine/bevy/issues/10397) -- Bounding sphere culling discussion
- [Bevy Engine: Fix prepass ortho depth clamping (PR #8877)](https://github.com/bevyengine/bevy/pull/8877) -- Shadow pancaking depth precision fix
- [Bevy Engine: Maximize useful directional light cascade shadow map texels (Issue #16076)](https://github.com/bevyengine/bevy/issues/16076) -- Near plane extension discussion
- [Google Filament: Fix a shadow clipping bug (PR #2259)](https://github.com/google/filament/pull/2259) -- Aggressive frustum intersection bug
- [Filament: Shadows disappearing on view angle and distance (Issue #6293)](https://github.com/google/filament/issues/6293) -- View-angle dependent shadow disappearance
- [Unreal Engine: Virtual Shadow Maps Documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine) -- VSM approach for foliage
- [Epic Games: Virtual Shadow Maps in Fortnite Chapter 4](https://www.unrealengine.com/en-US/tech-blog/virtual-shadow-maps-in-fortnite-battle-royale-chapter-4) -- Foliage shadow optimization
- [The Witness: Graphics Tech Shadow Maps (part 1)](http://the-witness.net/news/2010/03/graphics-tech-shadow-maps-part-1/) -- Valient sphere method with optimized sphere pushing
- [Godot: Use bounding box/sphere-based directional shadow splits (Proposal #3908)](https://github.com/godotengine/godot-proposals/issues/3908) -- Proposal to switch from AABB to sphere

### Academic/Technical References

- [Intel: Sample Distribution Shadow Maps (Lauritzen, SIGGRAPH 2010)](https://advances.realtimerendering.com/s2010/Lauritzen-SDSM%28SIGGRAPH%202010%20Advanced%20RealTime%20Rendering%20Course%29.pdf) -- Original SDSM algorithm and limitations
- [GPU Gems 3, Chapter 10: Parallel-Split Shadow Maps on Programmable GPUs](https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-10-parallel-split-shadow-maps-programmable-gpus) -- PSSM near plane handling, scene-dependent bounds
- [NVIDIA GPU Gems 1, Chapter 14: Perspective Shadow Maps](https://developer.nvidia.com/gpugems/gpugems/part-ii-lighting-and-shadows/chapter-14-perspective-shadow-maps-care-and-feeding) -- Light frustum expansion for off-screen casters
- [DigitalRune: Shadow Caster Culling](https://digitalrune.github.io/DigitalRune-Documentation/html/4058fb6c-8794-46cb-9d22-fb8558857179.htm) -- Shadow volume culling approach
- [Diary of a Graphics Programmer: Stable Cascaded Shadow Maps](http://diaryofagraphicsprogrammer.blogspot.com/2008/06/stable-cascaded-shadow-maps.html) -- Original blog post on Valient's technique
- [A Long Forgotten Blog: Stable Cascaded Shadow Maps](http://longforgottenblog.blogspot.com/2014/12/rendering-post-stable-cascaded-shadow.html) -- Detailed sphere-based implementation
- [Eric's Blog: Calculate Minimal Bounding Sphere of Frustum](https://lxjk.github.io/2017/04/15/Calculate-Minimal-Bounding-Sphere-of-Frustum.html) -- Optimized bounding sphere calculation
- [Babylon.js: Cascaded Shadow Maps Documentation](https://doc.babylonjs.com/features/featuresDeepDive/lights/shadows_csm) -- lightMargin parameter for Z extension
- [Khronos: GL_DEPTH_CLAMP](https://community.khronos.org/t/gl-depth-clamp/59068) -- Hardware depth clamping for shadow pancaking
- [GameDev.net: Stable Cascaded Shadow Maps - Sphere Based Bounding Help](https://www.gamedev.net/forums/topic/691434-stable-cascaded-shadow-maps-sphere-based-bounding-help/) -- Practical implementation discussion
- [GameDev.net: Cascaded Shadow Mapping - Shadow Frustum Generation](https://gamedev.net/forums/topic/670683-cascaded-shadow-mapping-shadow-frustum-generation/5245256/) -- Sphere vs AABB tradeoff discussion
