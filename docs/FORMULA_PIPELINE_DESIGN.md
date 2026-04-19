# Phase 9: Formula Pipeline Design Document

**Status:** Implemented. All six sub-phases (FP-1 through FP-6) shipped. `engine/formula/` contains `expression.{h,cpp}`, `expression_eval.{h,cpp}`, `formula.{h,cpp}`, `formula_library.{h,cpp}`, `codegen_cpp.{h,cpp}`, `codegen_glsl.{h,cpp}`, `curve_fitter.{h,cpp}`, `formula_preset.{h,cpp}`, `quality_manager.{h,cpp}`, `lut_generator.{h,cpp}`, `lut_loader.{h,cpp}`, `formula_benchmark.{h,cpp}`, `formula_doc_generator.{h,cpp}`, `node_graph.{h,cpp}`, `sensitivity_analysis.{h,cpp}`, plus the `physics_templates.cpp` library. `EnvironmentForces` is shipped in `engine/environment/environment_forces.{h,cpp}`. This document is retained as the original design record.

## Overview

The Formula Pipeline is a cross-cutting infrastructure system that provides:
1. **Centralized environmental forces** (wind, weather, buoyancy) queried by all physics/rendering systems
2. **Unified formula storage** as expression trees in JSON
3. **Build-time code generation** from expression trees to native C++ and GLSL
4. **Interactive formula discovery** via a curve-fitting workbench tool
5. **Quality tiers** for runtime formula switching (full/approximate/LUT)

### Motivation

Every physics and rendering system currently implements its own environmental forces independently:
- **Cloth** has its own gust state machine, hash noise, and drag calculation (`cloth_simulator.cpp:1600-1800`)
- **Foliage** has simple sine-wave sway with separate wind parameters (`foliage_renderer.h:72-78`)
- **Particles** have a `BH_WIND` behavior that applies simple additive velocity (`particle_simulate.comp.glsl:315-320`)
- **Water** has wave parameters completely decoupled from wind (`water_surface.h:33-46`)

This causes wind to blow cloth north while foliage sways east. The Formula Pipeline unifies these behind a single source of truth.

### Research Sources
- Ghost of Tsushima wind system (GDC 2020, Sucker Punch) — central Perlin noise field sampled by all consumers
- God of War wind simulation (GDC 2019, Rupert Renard) — GPU 3D fluid simulation
- Unity WindZone documentation — Directional + Spherical modes with turbulence/pulse
- Unreal Engine WindDirectionalSource — separate consumption by cloth, particles, foliage
- GPU Gems 3 Ch.6 — GPU wind animations for trees
- TinyExpr++ — lightweight expression parser (MIT, ~800 LOC)
- least-squares-cpp — single-header Levenberg-Marquardt optimizer
- GPU Gems 2 Ch.24 — LUT-based acceleration of color transforms
- Wang et al. SIGGRAPH 2014 — automatic shader simplification
- He et al. SIGGRAPH 2015 — automatic shader LOD generation

---

## FP-1: EnvironmentForces System

### Architecture

Follows the Ghost of Tsushima model: a central controller with a noise-modulated wind field. All consumers (cloth, foliage, particles, water) query the same system instead of maintaining their own wind state.

The EnvironmentForces object lives in Engine alongside the other subsystems. It is updated once per frame in the main loop, then passed to each consumer during their update/render calls.

### Current State (What Exists)

| System | Wind Source | Parameters | Location |
|--------|-----------|------------|----------|
| Cloth | `setWind(dir, strength)` called per-instance | direction, strength, dragCoeff, gust SM | `cloth_simulator.h:180-196` |
| Foliage | Public members on FoliageRenderer | windDirection, windAmplitude, windFrequency | `foliage_renderer.h:72-78` |
| Particles | `BH_WIND` behavior entry | direction XYZ, strength | `gpu_particle_emitter.h:70` |
| Water | WaterSurfaceConfig::Wave array | amplitude, wavelength, speed, direction (decoupled from wind) | `water_surface.h:33-46` |
| Scene | Hardcoded `sceneWindDir` in engine.cpp | `normalize(0.15, 0, -1)` | `engine.cpp:2097` |

### New Files

| File | Purpose |
|------|---------|
| `engine/environment/environment_forces.h` | EnvironmentForces class declaration, WeatherState struct |
| `engine/environment/environment_forces.cpp` | Wind field, gust state machine, weather queries |
| `tests/test_environment_forces.cpp` | Google Test suite |

### Modified Files

| File | Change |
|------|--------|
| `engine/core/engine.h` | Add `EnvironmentForces m_environmentForces;` member |
| `engine/core/engine.cpp` | Update EnvironmentForces each frame, pass to consumers |
| `engine/physics/cloth_simulator.h/.cpp` | Add `setWindFromEnvironment(const EnvironmentForces&)` overload |
| `engine/renderer/foliage_renderer.h/.cpp` | Read wind from EnvironmentForces instead of public members |
| `engine/scene/gpu_particle_emitter.h/.cpp` | Optional auto-sync with EnvironmentForces wind |
| `engine/scene/water_surface.h` | Add `windInfluence` parameter (0-1, modulates wave amplitude by wind strength) |

### API Design

```cpp
/// @brief Centralized environmental force queries.
///
/// All physics/rendering systems query this instead of maintaining their own
/// wind state. Updated once per frame in the main loop. Uses a noise-modulated
/// wind field inspired by Ghost of Tsushima (GDC 2020).
class EnvironmentForces
{
public:
    // --- Wind queries (position-dependent) ---

    /// @brief Wind velocity at a world position (includes gusts + spatial variation).
    /// @param worldPos Position to sample.
    /// @return Wind velocity in m/s.
    glm::vec3 getWindVelocity(const glm::vec3& worldPos) const;

    /// @brief Aerodynamic drag force on a surface panel.
    /// @param worldPos Panel center position.
    /// @param surfaceArea Panel area in m^2.
    /// @param surfaceNormal Panel outward normal (normalized).
    /// @return Force vector in Newtons.
    glm::vec3 getWindForce(const glm::vec3& worldPos, float surfaceArea,
                           const glm::vec3& surfaceNormal) const;

    /// @brief Scalar wind speed at a position (magnitude of getWindVelocity).
    float getWindSpeed(const glm::vec3& worldPos) const;

    /// @brief Global base wind direction (normalized, before noise modulation).
    glm::vec3 getBaseWindDirection() const;

    /// @brief Global base wind strength (m/s, before gusts).
    float getBaseWindStrength() const;

    /// @brief Current gust intensity [0, 1] from the gust state machine.
    float getGustIntensity() const;

    // --- Weather queries ---

    /// @brief Temperature at a position (Celsius). Defaults to 20C (room temp).
    float getTemperature(const glm::vec3& worldPos) const;

    /// @brief Relative humidity at a position [0, 1]. Defaults to 0.5.
    float getHumidity(const glm::vec3& worldPos) const;

    /// @brief Surface wetness [0, 1] (0=dry, 1=soaked). Defaults to 0.
    float getWetness(const glm::vec3& worldPos) const;

    /// @brief Current precipitation intensity [0, 1]. Defaults to 0.
    float getPrecipitationIntensity() const;

    /// @brief Air density at a position (kg/m^3). Defaults to 1.225 (sea level, 15C).
    float getAirDensity(const glm::vec3& worldPos) const;

    // --- Fluid queries ---

    /// @brief Buoyancy force for a submerged object.
    /// @param worldPos Object center.
    /// @param submergedVolume Volume below water surface (m^3).
    /// @param objectDensity Object density (kg/m^3).
    /// @return Upward buoyancy force vector.
    glm::vec3 getBuoyancy(const glm::vec3& worldPos, float submergedVolume,
                          float objectDensity) const;

    // --- Configuration ---

    /// @brief Sets the global wind direction (normalized).
    void setWindDirection(const glm::vec3& direction);

    /// @brief Sets the global wind strength (m/s).
    void setWindStrength(float strength);

    /// @brief Enables/disables gusts.
    void setGustsEnabled(bool enabled);

    /// @brief Sets turbulence scale (larger = slower spatial variation).
    void setTurbulenceScale(float scale);

    /// @brief Sets weather state.
    void setWeather(const WeatherState& weather);

    /// @brief Returns the current weather state.
    const WeatherState& getWeather() const;

    /// @brief Advances gust state machine and weather transitions. Call once per frame.
    void update(float deltaTime);

    /// @brief Resets all state to defaults.
    void reset();
};
```

### WeatherState

```cpp
/// @brief Global weather parameters.
struct WeatherState
{
    float temperature = 20.0f;        ///< Celsius
    float humidity = 0.5f;            ///< [0, 1]
    float precipitation = 0.0f;       ///< [0, 1] (0=none, 1=heavy rain)
    float wetness = 0.0f;             ///< [0, 1] surface wetness accumulation
    float cloudCover = 0.3f;          ///< [0, 1] (affects ambient light)
    float airDensity = 1.225f;        ///< kg/m^3 (sea level default)
};
```

### Wind Field Model

The wind field combines three components:
1. **Base wind:** Direction + strength set globally (replaces `sceneWindDir`)
2. **Gust state machine:** Migrated from `ClothSimulator`, now runs once globally instead of per cloth instance
3. **Spatial noise:** 2-octave hash noise varying wind by world position (migrated from `ClothSimulator::applyWind`)

```
windVelocity(pos) = baseDirection * baseStrength * gustIntensity * spatialNoise(pos)
```

The gust state machine cycles between:
- **Blow phase** (1.5-4 sec): intensity ramps up to target
- **Calm phase** (3-7 sec): intensity ramps down
- **Direction variation**: 15% chance of side gusts per cycle

This is the same algorithm currently in `cloth_simulator.cpp:1600-1650`, extracted to run once per frame instead of per cloth instance.

### Spatial Noise

Uses the same hash noise function currently in `cloth_simulator.cpp:1582`:
```cpp
float hashNoise(float x, float y)
{
    float h = std::sin(x * 127.1f + y * 311.7f) * 43758.5453f;
    return h - std::floor(h);
}
```

Two octaves sampled at `(pos.x * freq, pos.z * freq)` with different frequencies produce spatial wind variation. This creates areas of stronger/weaker wind without requiring a full 3D noise field.

### Integration Plan

**Cloth:** Add `setWindFromEnvironment(const EnvironmentForces& env, const glm::vec3& clothCenter)` that calls `env.getWindVelocity(clothCenter)` for the base wind, then applies per-particle spatial noise (already exists in cloth). The cloth's own gust SM becomes optional — if EnvironmentForces is used, the global gust drives all cloth.

**Foliage:** `FoliageRenderer::render()` receives EnvironmentForces and extracts `getBaseWindDirection()`, `getWindSpeed(chunkCenter)` to set shader uniforms. The existing `windAmplitude` member becomes a scaling factor on the environment wind strength.

**Particles:** `GPUParticleEmitter` gains a `syncWithEnvironment(const EnvironmentForces& env)` method that auto-updates its `BH_WIND` behavior params from the environment. Emitters that want custom wind can opt out.

**Water:** `WaterSurfaceConfig` gains a `windInfluence` float [0, 1]. When > 0, the water renderer modulates wave amplitude by `env.getWindSpeed(waterCenter) * windInfluence`. Waves still use their configured directions — wind only affects intensity.

### Backward Compatibility

All existing per-component wind APIs remain functional. EnvironmentForces is additive — systems that don't opt in continue to work as before. The integration in `engine.cpp` calls `setWindFromEnvironment()` only when EnvironmentForces is active.

---

## FP-2: FormulaLibrary + Expression System

### Architecture

Formulas are stored as expression trees in JSON during development. The expression tree is a simple AST with node types: `Literal`, `Variable`, `BinaryOp`, `UnaryOp`, `Conditional`.

At tool time (workbench, editor preview), formulas are evaluated via a tree-walking interpreter. At build time, they are compiled to native C++ or GLSL (see FP-3).

### External Dependency

**TinyExpr++** (MIT license, ~800 LOC, header-only):
- Parses mathematical expressions from strings
- Supports custom variables and functions
- Used for tool-time evaluation only (not runtime)
- Chosen over ExprTk (40K LOC) for simplicity per CLAUDE.md Rule #6

### New Files

| File | Purpose |
|------|---------|
| `engine/formula/expression.h` | Expression tree node types and AST definition |
| `engine/formula/formula.h` | FormulaDefinition with metadata, inputs, expression, quality tier |
| `engine/formula/formula_library.h/.cpp` | Registry: load/save JSON, category browser, lookup by name |
| `engine/formula/physics_templates.h/.cpp` | Pre-built formula templates (drag, Fresnel, Beer-Lambert, etc.) |
| `engine/formula/expression_eval.h/.cpp` | Tree-walking interpreter for tool-time evaluation |

### Expression Tree

```cpp
/// @brief Node types in the expression tree.
enum class ExprNodeType
{
    LITERAL,      ///< Constant float value
    VARIABLE,     ///< Named input variable
    BINARY_OP,    ///< +, -, *, /, pow
    UNARY_OP,     ///< sin, cos, sqrt, abs, exp, log, floor, clamp, normalize
    CONDITIONAL   ///< condition ? trueExpr : falseExpr
};

/// @brief A single node in the expression tree.
struct ExprNode
{
    ExprNodeType type;
    float value;                              ///< For LITERAL
    std::string name;                         ///< For VARIABLE
    std::string op;                           ///< For BINARY_OP/UNARY_OP
    std::vector<std::unique_ptr<ExprNode>> children;
};
```

### Formula Definition

```cpp
/// @brief A named formula with metadata and multiple quality tiers.
struct FormulaDefinition
{
    std::string name;                 ///< Unique identifier (e.g., "aerodynamic_drag")
    std::string category;            ///< Category (wind, water, lighting, collision, material)
    std::string description;         ///< Human-readable description
    std::vector<FormulaInput> inputs; ///< Named typed inputs with units and defaults
    FormulaOutput output;            ///< Output type and unit

    /// @brief Expression tree for each quality tier.
    std::map<QualityTier, std::unique_ptr<ExprNode>> expressions;

    /// @brief Fitted coefficients (name -> value).
    std::map<std::string, float> coefficients;

    std::string source;              ///< Provenance (e.g., "fitted from simulation data, 2026-04")
    float accuracy = 1.0f;          ///< R^2 accuracy vs. reference data
};
```

### Built-in Physics Templates

These ship with the engine and represent formulas already scattered across the codebase:

| Category | Formula | Current Location | Expression |
|----------|---------|-----------------|------------|
| Wind | Aerodynamic drag | `cloth_simulator.cpp:1784` | `F = 0.5 * Cd * rho * A * (v . n) * n` |
| Wind | Foliage sway | `foliage.vert.glsl:25-32` | `offset = sin(t * freq + pos.x * 0.5) * amp * height` |
| Water | Fresnel-Schlick | `water.frag.glsl:206-207` | `R = F0 + (1-F0) * (1-cosTheta)^5` |
| Water | Beer-Lambert | `water.frag.glsl:190-193` | `I = I0 * exp(-alpha * depth)` |
| Water | Gerstner wave | `water_surface.h:33-46` | `y = A * sin(k*x - omega*t + phi)` |
| Light | Inverse-square | `scene.frag.glsl:612-613` | `atten = 1 / (c + l*d + q*d^2)` |
| Light | GGX NDF | `scene.frag.glsl:459-471` | `D = a^2 / (PI * (NdotH^2 * (a^2-1) + 1)^2)` |
| Physics | Hooke spring | `cloth_simulator.cpp` (XPBD) | `F = -k * (x - x0)` |
| Physics | Coulomb friction | `cloth_simulator.h:265-267` | `Ft = min(Ft, mu * Fn)` |
| Material | Wet darkening | (not yet implemented) | `albedo *= (1 - wetness * darkFactor)` |

### JSON Storage Format

```json
{
    "name": "aerodynamic_drag",
    "category": "wind",
    "description": "Aerodynamic drag force on a surface panel from relative wind",
    "inputs": [
        {"name": "windVelocity", "type": "vec3", "unit": "m/s"},
        {"name": "surfaceArea", "type": "float", "unit": "m2"},
        {"name": "surfaceNormal", "type": "vec3", "unit": "normalized"},
        {"name": "airDensity", "type": "float", "unit": "kg/m3", "default": 1.225}
    ],
    "output": {"type": "vec3", "unit": "N"},
    "expression": {
        "op": "*",
        "left": {"op": "*", "left": 0.5, "right": {"var": "airDensity"}},
        "right": {"op": "*",
            "left": {"var": "surfaceArea"},
            "right": {"op": "*",
                "left": {"fn": "dot", "args": [{"var": "windVelocity"}, {"var": "surfaceNormal"}]},
                "right": {"var": "surfaceNormal"}
            }
        }
    },
    "coefficients": {"dragCoeff": 0.47},
    "source": "classical aerodynamics, matches cloth_simulator.cpp implementation"
}
```

---

## FP-3: FormulaCompiler (Build-Time Code Generation)

### Architecture

The FormulaCompiler is a build-time tool that walks expression trees and emits native C++ or GLSL code. This eliminates all runtime parsing and interpretation — formulas become compiled instructions.

### New Files

| File | Purpose |
|------|---------|
| `engine/formula/codegen_cpp.h/.cpp` | Emits C++ inline functions from expression trees |
| `engine/formula/codegen_glsl.h/.cpp` | Emits GLSL function snippets from expression trees |
| `engine/formula/lut_generator.h/.cpp` | Samples formula over input ranges, writes binary LUT |
| `engine/formula/lut_loader.h/.cpp` | Memory-maps binary LUT files, provides O(1) lookup with interpolation |
| `tools/formula_compile/main.cpp` | Build-time executable: reads formula library, generates code |

### Code Generation Approach

A simple recursive visitor (~200-300 lines per target language). Each node type emits the corresponding syntax:

| Node Type | C++ Output | GLSL Output |
|-----------|-----------|-------------|
| `Literal(3.14)` | `3.14f` | `3.14` |
| `Variable("x")` | `x` (function param) | `x` (function param) |
| `BinaryOp(+, a, b)` | `(visit(a) + visit(b))` | `(visit(a) + visit(b))` |
| `UnaryOp(sin, x)` | `std::sin(visit(x))` | `sin(visit(x))` |
| `UnaryOp(pow, x, y)` | `std::pow(visit(x), visit(y))` | `pow(visit(x), visit(y))` |

C++ and GLSL differ only in:
- Function prefixes: `std::sin` vs `sin`, `std::pow` vs `pow`
- Type declarations: `float` vs `float`, `glm::vec3` vs `vec3`
- Include headers vs nothing

### Generated Code Example

Input: `aerodynamic_drag` formula from library.

Output (`generated/formulas.h`):
```cpp
// Generated by FormulaCompiler — DO NOT EDIT
// Source: formula_library/wind/aerodynamic_drag.json
#pragma once
#include <glm/glm.hpp>
#include <cmath>

namespace Vestige::Formulas
{
    inline glm::vec3 aerodynamicDrag(const glm::vec3& windVelocity,
                                      float surfaceArea,
                                      const glm::vec3& surfaceNormal,
                                      float airDensity)
    {
        float vDotN = glm::dot(windVelocity, surfaceNormal);
        return surfaceNormal * (0.5f * 0.47f * airDensity * surfaceArea * vDotN);
    }
}
```

Output (`generated/formulas.glsl`):
```glsl
// Generated by FormulaCompiler — DO NOT EDIT
vec3 aerodynamicDrag(vec3 windVelocity, float surfaceArea,
                     vec3 surfaceNormal, float airDensity)
{
    float vDotN = dot(windVelocity, surfaceNormal);
    return surfaceNormal * (0.5 * 0.47 * airDensity * surfaceArea * vDotN);
}
```

### Binary LUT Format

For complex multi-variable formulas where a lookup table is faster than evaluation.

```
Header (32 bytes):
  magic: "VLUT"           (4 bytes)
  version: 1              (4 bytes)
  dimensions: N           (4 bytes)  — 1D, 2D, or 3D
  axisSizes[3]: [W,H,D]   (12 bytes)
  valueType: FLOAT32      (4 bytes)
  flags: INTERPOLATE      (4 bytes)

Axis definitions (N x 16 bytes):
  nameHash: uint32        — FNV-1a hash of input variable name
  minValue: float
  maxValue: float
  padding: uint32

Data (W x H x D x sizeof(float)):
  Row-major float array, memory-mappable
```

Runtime lookup uses bilinear interpolation for 2D LUTs (matching OpenGL texture hardware behavior). For GPU-side LUTs, the binary data is uploaded as a GL texture with `GL_LINEAR` filtering for free hardware interpolation.

### Build Integration

A CMake custom target `formula_compile` runs the compiler before the main build:
```cmake
add_custom_target(formula_compile
    COMMAND formula_compiler
        --library ${CMAKE_SOURCE_DIR}/assets/formulas/
        --output-cpp ${CMAKE_BINARY_DIR}/generated/formulas.h
        --output-glsl ${CMAKE_BINARY_DIR}/generated/formulas.glsl
        --output-lut ${CMAKE_BINARY_DIR}/generated/luts/
    COMMENT "Compiling formula library..."
)
add_dependencies(vestige formula_compile)
```

---

## FP-4: FormulaWorkbench (Interactive Discovery Tool)

### Architecture

A standalone ImGui application for discovering and fitting mathematical formulas. Separate executable, shares the formula library code with the engine.

### External Dependencies

- **TinyExpr++** (already added in FP-2) for expression evaluation
- **least-squares-cpp** (single-header, MIT) for Levenberg-Marquardt curve fitting
  - Depends on Eigen3 for matrix operations (tool-only dependency, not in engine runtime)
- **ImPlot** (MIT, header-only ImGui extension) for 2D plotting

### New Files

| File | Purpose |
|------|---------|
| `tools/formula_workbench/main.cpp` | Standalone ImGui application entry point |
| `tools/formula_workbench/data_editor.h/.cpp` | Manual data entry, CSV import |
| `tools/formula_workbench/template_browser.h/.cpp` | Browse/search formula templates by category |
| `tools/formula_workbench/visualizer.h/.cpp` | Plot fitted curve vs data, residuals, error histogram |
| `tools/formula_workbench/validation.h/.cpp` | Train/test split, R^2, RMSE, max error |
| `tools/formula_workbench/export_panel.h/.cpp` | Save fitted formula to library JSON |
| `engine/formula/curve_fitter.h/.cpp` | Levenberg-Marquardt wrapper using least-squares-cpp |

### Workflow

1. **Define problem:** "I need a formula for caustic light intensity"
2. **Set variables:** depth (0-10m), lightAngle (0-90 degrees)
3. **Enter data:** Manual entry or CSV import (or capture from expensive simulation)
4. **Browse templates:** Select candidate formula family (e.g., exponential decay, Beer-Lambert)
5. **Fit coefficients:** LM optimizer finds best parameters minimizing squared error
6. **Validate:** 20% holdout test shows R^2, RMSE, max error
7. **Compare tiers:** Full vs approximate vs LUT accuracy/cost tradeoff
8. **Export:** Save to FormulaLibrary JSON with provenance metadata

### Curve Fitter

```cpp
/// @brief Fits formula coefficients to data points using Levenberg-Marquardt.
struct FitResult
{
    std::map<std::string, float> coefficients;  ///< Fitted coefficient values
    float rSquared = 0.0f;                       ///< Coefficient of determination
    float rmse = 0.0f;                           ///< Root mean squared error
    float maxError = 0.0f;                       ///< Maximum absolute error
    int iterations = 0;                          ///< Optimizer iterations used
    bool converged = false;                      ///< Whether optimizer converged
};

/// @brief Fits coefficients of a formula template to reference data points.
FitResult fitFormula(
    const ExprNode& formulaTemplate,
    const std::vector<std::string>& coefficientNames,
    const std::vector<DataPoint>& trainingData,
    const std::vector<DataPoint>& validationData,
    int maxIterations = 100);
```

---

## FP-5: Water Formula Optimization

### Motivation

Water rendering is the most expensive per-fragment operation in the engine. The workbench can find cheaper approximations for the most expensive components.

### Optimization Targets

| Component | Current Cost | Approach | Expected Benefit |
|-----------|-------------|----------|-----------------|
| Caustics | Procedural noise per fragment | Fit `intensity = f(depth, angle)` -> 2D LUT texture | ~40% fragment cost reduction |
| Water normal FBM | 2-octave gradient noise with quintic Hermite | Fit with fewer octaves, adjusted coefficients | Reduce from 2 octaves to 1 + offset |
| Fresnel | Schlick approximation (already cheap) | Validate coefficients for water (n=1.33, F0=0.02) | Correctness check only |
| Beer-Lambert | `exp(-alpha * depth)` per channel (already cheap) | 1D LUT for absorption curve | Marginal, mostly code clarity |

### Process

1. Run expensive water shaders on reference viewpoints, capture per-pixel output
2. Import captured data into workbench
3. Fit cheaper approximations
4. Compare visually: original vs approximation side-by-side
5. Export as GLSL via FormulaCompiler

---

## FP-6: Integration + Quality Tiers

### Quality Tier System

```cpp
/// @brief Formula evaluation quality levels.
enum class QualityTier
{
    FULL,         ///< Original formula, highest accuracy
    APPROXIMATE,  ///< Simplified expression, ~95% accuracy, ~50% cost
    LUT           ///< Precomputed lookup table, ~90% accuracy, ~10% cost
};
```

Each formula in the library can have up to 3 tier expressions. The engine's graphics quality setting selects the active tier. No runtime branching — tier selection happens at material bind time, choosing the appropriate pre-compiled shader variant or LUT texture.

### Settings Integration

```cpp
// In engine quality settings:
enum class GraphicsQuality { LOW, MEDIUM, HIGH, ULTRA };

// Maps to formula tiers:
// LOW    -> QualityTier::LUT
// MEDIUM -> QualityTier::APPROXIMATE
// HIGH   -> QualityTier::FULL
// ULTRA  -> QualityTier::FULL
```

### Profiler Integration

Each formula tier reports its evaluation cost through the existing PerformanceProfiler. The profiler UI shows per-formula cost, enabling data-driven quality decisions.

---

## Implementation Sequence

```
FP-1 (EnvironmentForces)  ──>  FP-2 (FormulaLibrary)
                                 ├──>  FP-3 (FormulaCompiler)  ──>  FP-5 (Water Optimization)
                                 │                              ──>  FP-6 (Integration + Tiers)
                                 └──>  FP-4 (FormulaWorkbench)  ──>  FP-5
```

### Batch Schedule

| Batch | Estimated Duration | Dependencies |
|-------|-------------------|-------------|
| FP-1: EnvironmentForces | ~1.5 weeks | standalone (start here) |
| FP-2: FormulaLibrary + Expressions | ~2 weeks | FP-1 |
| FP-3: FormulaCompiler | ~2.5 weeks | FP-2 |
| FP-4: FormulaWorkbench | ~3 weeks | FP-2 |
| FP-5: Water Optimization | ~2 weeks | FP-3, FP-4 |
| FP-6: Quality Tiers | ~1.5 weeks | FP-3 |

### Starting Point

FP-1 (EnvironmentForces) is standalone and delivers immediate value: unified wind across all systems. It has no external dependencies and modifies only existing integration points.

---

## Performance Considerations

- **EnvironmentForces::update()** runs once per frame (not per consumer). Cost: ~1us (noise evaluation + gust SM)
- **getWindVelocity()** per query: ~50ns (hash noise at position). Cloth queries per-particle; foliage per-chunk
- **Generated C++ formulas**: zero overhead vs hand-written code (identical after compilation)
- **Generated GLSL formulas**: zero overhead vs current shader code
- **LUT lookups**: one texture fetch (hardware interpolated) vs. multi-instruction formula evaluation
- **TinyExpr++ evaluation**: ~120ns per expression (tool-time only, never in hot path)

## Testing Strategy

- **EnvironmentForces**: Unit tests for wind queries, gust SM state transitions, weather state, spatial noise consistency
- **FormulaLibrary**: Serialization round-trip, template loading, lookup by name/category
- **FormulaCompiler**: Generated code compiles and matches interpreter output within epsilon
- **Curve fitter**: Known functions with added noise recover correct coefficients
- **Integration**: Visual test that cloth/foliage/water all respond to the same wind change
