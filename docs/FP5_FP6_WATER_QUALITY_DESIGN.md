# FP-5 + FP-6: Water Formula Optimization & Quality Tier Integration

## Overview

FP-5 applies the Formula Pipeline to the water rendering system, creating quality-tiered
formula variants for caustics, absorption, and Fresnel. FP-6 adds the engine-wide quality
tier manager that lets users select Full/Approximate/LUT tiers from settings.

Combined because the quality tier infrastructure (FP-6) is what makes the water
optimization tiers (FP-5) usable at runtime.

## Current Water Rendering Analysis

### Per-Fragment Costs (water.frag.glsl)

| Operation | Calls/Fragment | Cost |
|-----------|---------------|------|
| Gradient noise (`noised()`) | 9 (3 FBM calls x 3 octaves) | HIGH — 36 hash lookups + quintic interp |
| Fresnel (Schlick) | 1 | LOW — 1 pow + 2 mul |
| Beer-Lambert absorption | 1 | LOW — 1 exp + 3 mul |
| Specular (Blinn-Phong) | 1 | LOW — 1 pow + dot |

### Per-Fragment Costs (caustics in scene.frag.glsl / terrain.frag.glsl)

| Operation | Calls/Fragment | Cost |
|-----------|---------------|------|
| Texture samples (chromatic aberration) | 6 (3 channels x 2 patterns) | MEDIUM |
| min-blend + depth fade + tint | 1 | LOW |

### Optimization Targets

1. **Caustics**: 6 texture reads -> 2 (drop chromatic aberration) or 1 (single sample)
2. **Water FBM noise**: 9 octave evals -> 4 (reduce octaves + calls)
3. **Fresnel/Absorption**: Already cheap — wire through formula pipeline for consistency

## Architecture

### FP-6: FormulaQualityManager

Central system for managing active quality tiers. Stored in engine settings,
accessible to all renderers.

```cpp
class FormulaQualityManager
{
public:
    /// Set the global quality tier (applied to all categories by default)
    void setGlobalTier(QualityTier tier);
    QualityTier getGlobalTier() const;

    /// Override tier for a specific category ("water", "lighting", etc.)
    void setCategoryTier(const std::string& category, QualityTier tier);
    QualityTier getCategoryTier(const std::string& category) const;
    bool hasCategoryOverride(const std::string& category) const;
    void clearCategoryOverride(const std::string& category);

    /// Convenience: get effective tier for a category (override or global)
    QualityTier getEffectiveTier(const std::string& category) const;

    /// JSON persistence (saves to/loads from engine settings)
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);
};
```

**Location**: `engine/formula/quality_manager.h/.cpp`

### FP-5: Water Quality Tiers

Three quality levels for water rendering, driven by `FormulaQualityManager`:

#### Caustics Tiers

| Tier | Texture Reads | Features | Visual Quality |
|------|--------------|----------|---------------|
| FULL | 6 | Dual scroll + min-blend + chromatic aberration + depth fade | Reference |
| APPROXIMATE | 2 | Dual scroll + min-blend + depth fade (no chromatic aberration) | ~95% |
| LUT | 1 | Single scroll + depth fade | ~80% |

#### Water Surface Noise Tiers

| Tier | FBM Calls | Octaves/Call | Total Noise Evals | Visual Quality |
|------|-----------|-------------|-------------------|---------------|
| FULL | 3 (normal1 + normal2 + distortion) | 3 | 9 octaves | Reference |
| APPROXIMATE | 2 (normal1 + distortion) | 2 | 4 octaves | ~90% |
| LUT | 0 (use scrolling normal map texture) | 0 | 0 | ~75% |

#### Shader Implementation

Quality tiers are selected via a `u_waterQualityTier` uniform (int: 0/1/2).
The shader branches on this uniform. Since water surfaces are typically a small
fraction of screen pixels, the branch cost is negligible.

```glsl
// Caustics example (in scene.frag.glsl)
uniform int u_causticsQuality;  // 0=Full, 1=Approximate, 2=Simple

vec3 computeCaustics(vec3 worldPos, float causticsScale, float causticsTime, float intensity)
{
    if (u_causticsQuality == 0)  // FULL: 6 samples + chromatic aberration
    {
        // ... current 6-sample implementation ...
    }
    else if (u_causticsQuality == 1)  // APPROXIMATE: 2 samples, no chromatic aberration
    {
        vec2 uv1 = worldPos.xz * causticsScale + causticsTime * vec2(0.03, 0.02);
        vec2 uv2 = worldPos.xz * causticsScale * 1.4 + causticsTime * vec2(-0.02, 0.03);
        float c1 = texture(u_causticsTex, uv1).r;
        float c2 = texture(u_causticsTex, uv2).r;
        return vec3(min(c1, c2)) * intensity;
    }
    else  // LUT: 1 sample
    {
        vec2 uv = worldPos.xz * causticsScale + causticsTime * vec2(0.03, 0.02);
        return vec3(texture(u_causticsTex, uv).r) * intensity * 0.7;
    }
}
```

### New Formula Definitions

Add water-optimization-specific formulas to the library:

1. **`caustic_depth_fade`** — `1.0 - smoothstep(0.0, maxDepth, depth)`
   - Inputs: `depth` (float, m), `maxDepth` (float, m, default 5.0)
   - Coefficients: none (pure function)
   - All tiers use the same expression (already cheap)

2. **`water_absorption`** — `exp(-absorptionCoeff * thickness)`
   - Inputs: `thickness` (float, m)
   - Coefficients: `alpha` (per-channel in the formula library, scalar here)
   - APPROXIMATE: `max(1.0 - alpha * thickness, 0.0)` (linear approx, avoids exp)

3. **`caustic_chromatic`** — models chromatic aberration offset
   - FULL: 3-channel with UV offsets
   - APPROXIMATE: single channel (no aberration)

### Generated GLSL

`CodegenGlsl::generateFile()` creates `water_formulas.glsl` containing the formula
functions. These serve as a **reference** — the actual shader code is hand-written
with quality branches, but validated against the generated code in tests.

### Integration Points

1. **WaterRenderer** — reads quality tier from FormulaQualityManager, sets `u_waterQualityTier` uniform
2. **Renderer (scene/terrain)** — reads quality tier, sets `u_causticsQuality` uniform
3. **InspectorPanel** — quality tier dropdown in water component and global settings
4. **Engine** — owns FormulaQualityManager, initializes from settings

## Implementation Plan

### Step 1: FormulaQualityManager (FP-6 core)
- New: `engine/formula/quality_manager.h/.cpp`
- JSON persistence, global + per-category tiers
- Tests: `tests/test_quality_manager.cpp`

### Step 2: New Water Formulas
- Add `caustic_depth_fade`, `water_absorption` to PhysicsTemplates
- Add APPROXIMATE tier expressions for `water_absorption` and `fresnel_schlick`
- Tests: verify formula evaluation matches hand-calculated values

### Step 3: Shader Quality Tiers
- Modify `scene.frag.glsl`: extract caustics into quality-branching function
- Modify `terrain.frag.glsl`: same caustics refactor
- Modify `water.frag.glsl`: add quality-branching for FBM octave count
- New uniforms: `u_causticsQuality`, `u_waterQualityTier`

### Step 4: Engine Integration
- Add FormulaQualityManager to Engine
- WaterRenderer reads tier, sets uniforms
- Renderer sets caustics quality uniform
- Persist quality settings in scene/engine config

### Step 5: Inspector UI
- Add quality tier dropdown to water component inspector
- Add global quality tier to a settings section

### Step 6: Tests & Validation
- Unit tests for FormulaQualityManager
- GLSL codegen tests for new formulas
- Formula accuracy tests (APPROXIMATE vs FULL)
- Visual test: compare tiers side-by-side

## File Changes

| File | Change |
|------|--------|
| `engine/formula/quality_manager.h` | NEW — quality tier manager |
| `engine/formula/quality_manager.cpp` | NEW — implementation |
| `engine/formula/physics_templates.h` | MOD — add new water formula factories |
| `engine/formula/physics_templates.cpp` | MOD — implement new formulas + APPROXIMATE tiers |
| `assets/shaders/scene.frag.glsl` | MOD — extract caustics function with quality branches |
| `assets/shaders/terrain.frag.glsl` | MOD — same caustics refactor |
| `assets/shaders/water.frag.glsl` | MOD — quality-tier FBM octaves |
| `engine/renderer/water_renderer.cpp` | MOD — set quality uniforms |
| `engine/renderer/renderer.h` | MOD — add quality uniform setting for caustics |
| `engine/renderer/renderer.cpp` | MOD — set caustics quality from manager |
| `engine/core/engine.h` | MOD — own FormulaQualityManager |
| `engine/core/engine.cpp` | MOD — init quality manager, pass to renderers |
| `engine/editor/panels/inspector_panel.cpp` | MOD — quality tier dropdown |
| `engine/CMakeLists.txt` | MOD — add quality_manager.cpp |
| `tests/test_quality_manager.cpp` | NEW — quality manager tests |
| `tests/test_formula_library.cpp` | MOD — add tests for new formulas + APPROXIMATE tiers |
| `tests/CMakeLists.txt` | MOD — add test_quality_manager.cpp |

## Performance Impact

| Tier | Caustics Savings | Water Surface Savings | Visual Loss |
|------|-----------------|----------------------|-------------|
| FULL | 0% (reference) | 0% (reference) | 0% |
| APPROXIMATE | ~55% (6 reads -> 2) | ~55% (9 octaves -> 4) | ~5-10% |
| LUT | ~80% (6 reads -> 1) | ~95% (noise -> texture) | ~20-25% |
