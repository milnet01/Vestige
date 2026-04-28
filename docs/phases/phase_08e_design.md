# Phase 8E Design: Cloth Polish and Scene Integration

**Date:** 2026-03-31
**Phase:** 8E — Cloth Polish and Scene Integration
**Status:** Implemented. `engine/physics/cloth_presets.{h,cpp}` + `engine/physics/fabric_material.{h,cpp}` ship the preset library and inspector integration described here. This document is retained as the original design record.
**Depends on:** Phase 8D (Cloth Simulation — complete)

---

## Table of Contents

1. [Overview](#1-overview)
2. [Research Summary](#2-research-summary)
3. [Cloth Presets](#3-cloth-presets)
4. [Editor UI — Inspector Panel](#4-editor-ui--inspector-panel)
5. [Scene Integration — Tabernacle Refinements](#5-scene-integration--tabernacle-refinements)
6. [Simulation Reset](#6-simulation-reset)
7. [File Structure](#7-file-structure)
8. [Implementation Steps](#8-implementation-steps)
9. [Performance Considerations](#9-performance-considerations)
10. [Testing Strategy](#10-testing-strategy)

---

## 1. Overview

Phase 8E completes the cloth simulation subsystem by adding:
- **Named presets** — hardcoded parameter sets for common fabric types (linen, goat hair, leather, banner, heavy drape)
- **Editor UI** — inspector panel section for ClothComponent (preset selector, parameter sliders, wind controls, reset button)
- **Scene refinement** — use presets in the Tabernacle scene, add the inner partition veil as cloth

This is the final sub-phase of Phase 8 (Physics). After completion, the mandatory post-phase audit runs.

---

## 2. Research Summary

### 2.1 How Other Engines Handle Cloth Presets

**Unity Cloth Component** ([docs](https://docs.unity3d.com/Manual/class-Cloth.html)) provides per-vertex constraint painting, global stiffness sliders, and external acceleration (wind). Core parameters are all normalized to [0, 1] ranges: Stretching Stiffness (0 < value <= 1), Bending Stiffness (0-1), and Damping (0-1). Unity also provides `ClearTransformMotion()` for runtime position resets, which moves cloth particles along with the transform so that transform movement has no effect on the simulation. No named fabric presets ship built-in — parameters are set manually per instance. Third-party solutions like MagicaCloth2 and Obi Physics add preset support.

**Unreal Chaos Cloth** ([reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/clothing-tool-in-unreal-engine---properties-reference)) uses "Cloth Config" objects with named profiles. Parameters include Anim Drive Stiffness (retains intended style), Anim Drive Damping (reduces shaking), Gravity Scale, Drag/Lift (aerodynamics), Density (mass from gsm weight), Collision Thickness, and Bend/Stretch Stiffness (per weft/warp axis). Per-vertex weight painting provides stiffness gradients. In UE 5.4+, Panel Cloth uses a Dataflow graph for advanced simulation configuration. Properties from Marvelous Designer can be imported directly: weight (gsm) maps to density (kg conversion), bending stiffness converts with unit scaling (g*mm^2/s^2 to kg*cm^2/s^2), and collision thickness converts from mm to cm. No built-in fabric presets — studios create their own profiles.

**Godot Cloth** ([docs](https://docs.godotengine.org/en/stable/tutorials/physics/soft_body.html)) uses SoftBody3D with Simulation Precision (keep above 5 to prevent collapse), Linear Stiffness, Pressure Coefficient, and Damping. No preset system — parameters edited directly in the inspector. Jolt Physics (default since Godot 4.6) provides more robust soft body support than GodotPhysics3D.

**Havok Cloth** ([preview tool](https://www.havok.com/blog/introduction-havok-cloth-preview-tool/)) provides a Cloth Tweaker panel with five constraint stiffness types that can be individually zeroed for debugging. Changes in the preview tool are temporary until applied in the Property Editor and saved. Automatic LOD transitions optimize performance across platforms.

**Blender** provides the only built-in named presets among the tools surveyed. Its five cloth presets define mass, structural stiffness, bending stiffness, spring damping, and air damping ([source](https://github.com/walac/blender-wayland/tree/master/release/scripts/presets/cloth)):

| Preset | Mass | Structural Stiffness | Bending Stiffness | Spring Damping | Air Damping | Quality |
|--------|------|---------------------|-------------------|----------------|-------------|---------|
| Silk | 0.15 | 5 | 0.05 | 0 | 1 | 5 |
| Cotton | 0.30 | 15 | 0.50 | 5 | 1 | 5 |
| Denim | 1.00 | 40 | 10.0 | 25 | 1 | 12 |
| Leather | 0.40 | 80 | 150.0 | 25 | 1 | 15 |
| Rubber | 3.00 | 15 | 25.0 | 25 | 1 | 7 |

**Key pattern:** Mass increases from silk to denim (light to heavy), bending stiffness varies by orders of magnitude (0.05 for silk vs. 150 for leather), and stiffer materials need more solver iterations (quality). This mirrors our XPBD approach where compliance replaces stiffness (lower compliance = stiffer).

**Common pattern across all engines:** All expose compliance/stiffness, damping, gravity scale, and wind response as primary controls. None except Blender ship built-in fabric presets. We will provide built-in defaults since this is an exploration engine with specific fabric types.

### 2.2 Realistic Fabric Parameters

Physical properties for XPBD mapping (from textile engineering and archaeological references):

| Fabric | Areal Density (g/m^2) | Bend Rigidity | Stretch | Character |
|--------|----------------------|---------------|---------|-----------|
| Fine linen | 100-200 | Very low | Low | Light, flowing, gentle drape |
| Heavy linen (ancient, hand-woven) | 200-350 | Low-medium | Very low | Stiffer than modern, holds shape |
| Goat hair (cilice/tent cloth) | 300-600+ | Medium-high | Very low | Dense, heavy, minimal drape |
| Tanned leather (sheep/ram) | 333-1200 | High | Very low | Stiff, heavy, holds shape |
| Banner/flag (cotton/nylon) | 100-180 | Low | Low | Light, responsive to wind |
| Heavy drapery (velvet/brocade) | 300-500 | Medium | Low | Rich drape, moderate movement |

**Detailed fabric property notes:**

**Linen** ([source](https://thelinenshack.com/blogs/blog/linen-weight-explained), [source](https://szoneierfabrics.com/cotton-linen-fabric-gsm-chart-weight-classifications-for-different-end-uses/)): Modern linen ranges from very fine (<120 gsm, batiste) through medium (150-200 gsm, shirts/dresses) to heavy (200-280 gsm, blazers/curtains) and extra-heavy (280+ gsm, awnings/bags). Ancient hand-woven linen was coarser — archaeological finds show grades from sailcloth to 200-thread-per-inch fine linen rivaling silk. For tabernacle curtains, a medium-heavy weight (200-300 gsm) is appropriate, representing finely woven but substantial ritual fabric. The Torah distinguishes quality grades, with "choshev-quality" work reserved for the holiest fabrics.

**Goat hair fabric** ([source](https://www.tandfonline.com/doi/full/10.1080/15440478.2025.2515465), [source](https://www.thetabernacleman.com/post/coverings-of-the-tabernacle-of-moses-part-2-goat-hair-covering-exodus-26-7-by-dr-terry-harman)): Research on Iranian nomadic black tent cloth measured goat hair fabric at 1,588 g/m^2 — classified as "very heavy textile." The low thread density (500 warp, 230 woof per cm) results from thick yarns made from fibers with 78-80 um diameter. The fiber swells when wet, providing natural water resistance. Biblically, the second tabernacle covering was goat hair cloth — dark, thick, coarse, similar to felt. For simulation, this should behave like a very stiff, heavy material with minimal drape and high resistance to wind.

**Leather/animal skins** ([source](https://journals.sagepub.com/doi/full/10.1177/1558925020968825), [source](https://www.leather-dictionary.com/index.php/Measures_and_weights), [source](https://hoplokleather.com/leather-weight-and-thickness-guide/)): Sheepskin averages ~333 gsm (3 m^2 per kg of finished product). Leather thickness ranges from 0.6mm (thin lambskin) to 2.5mm (box, suede, nubuck). The drape is determined mainly by thickness and rigidity/stiffness — lower shearing stiffness means softer drape. Ram skins dyed red formed the third tabernacle covering over the goat hair. Cowhide is significantly heavier. For simulation, leather should be the stiffest material with the lowest bend compliance and highest resistance to deformation.

**Banner/flag cloth** ([source](https://designershighway.com/blender-cloth-simulation-flags-and-banners/)): Lightweight material (100-180 gsm) that responds strongly to wind. Low stiffness and low damping allow natural flutter. The key characteristic is high aerodynamic responsiveness — a flag should immediately react to wind changes with sustained fluttering motion. Simulation should use lower damping than curtains to maintain constant movement.

**Heavy drapery** ([source](https://sewingtrip.com/fabric-weight-for-curtains/), [source](https://vestadraperyhardware.com/understanding-drapery-weight-a-guide-to-choosing-the-right-hardware/)): Heavyweight curtain fabrics range from 200-400+ gsm. Velvet absorbs shadows and creates rich texture. The partition veil of the tabernacle is described as elaborately woven ("choshev" quality) with blue, purple, and scarlet — suggesting thick, luxurious fabric. The Temple veil was described as approximately 4 inches (10cm) thick when layered. Heavy drapery should have moderate stiffness with deep, slow-moving folds.

### 2.3 Editor UI Best Practices

Key findings from engine editor patterns:

**Parameter organization** ([Havok](https://www.havok.com/blog/introduction-havok-cloth-preview-tool/), [Unity](https://docs.unity3d.com/Manual/class-Cloth.html), [Unreal](https://dev.epicgames.com/community/learning/tutorials/jOrv/unreal-engine-chaos-cloth-tool-properties-reference)):
- **Group by category**: Solver, Material, Wind, Collision — collapsible sections
- **Preset selector at the top** — "Custom" mode when any parameter is manually changed
- **Immediate feedback**: parameter changes take effect next frame (our XPBD loop already does this)
- **Reset button**: critical for cloth — restores particles to initial grid positions
- **Ranges with sensible defaults**: compliance sliders should use logarithmic scale since the useful range spans several orders of magnitude

**Havok's approach** is notable: the Cloth Tweaker provides temporary changes for experimentation, requiring explicit "Apply" to make permanent. Five constraint types (stretch, compression, bend, shear, area) can each be zeroed individually for debugging. This separation of preview vs. committed state prevents accidental parameter changes.

**Marvelous Designer** ([docs](https://support.marvelousdesigner.com/hc/en-us/articles/47358125463321-Simulation-Properties)) uses Open/Save buttons in the Property Editor for saving and reusing parameter sets. Presets are starting points that can be tweaked in real-time with immediate visual feedback.

**For Vestige:** We adopt a simpler model — changes are live and immediate (matching our WYSIWYG philosophy from Phase 5). The preset combo tracks whether parameters have been manually modified, switching to "Custom" on any change. No separate apply step needed.

### 2.4 Simulation Reset

Approaches studied across engines:

**Unity** ([source](https://discussions.unity.com/t/how-to-reset-the-cloth-during-runtime/926913)): No built-in "Reset Cloth" method. Workarounds include disabling/re-enabling the Cloth component or using `ClearTransformMotion()`. The recommended approach is toggling the component off and on, which forces re-initialization from rest positions.

**Unreal** ([source](https://forums.unrealengine.com/t/how-to-reset-cloth-sim/2331825)): No simple Blueprint node for "Reset Cloth Simulation." Developers typically teleport the actor or toggle simulation enabled state to force a reset.

**Maya nCloth** ([source](https://download.autodesk.com/us/maya/maya_2014_gettingstarted/files/Creating_nCloth_Clothing_Setting_the_initial_state.htm)): Provides explicit "Start Shape" as the Rest Shape — the cloth always attempts to return to this configuration through stretch, compression, and bend resistance. Initial state can be set/captured at any simulation frame.

**Source Engine** ([Valve wiki](https://developer.valvesoftware.com/wiki/Cloth_Simulation)): Cloth resets to initial state when animation stops playing. An animation must be playing at all times for cloth not to "sleep and reset."

**PBD/XPBD approach** ([Muller 2006](https://matthias-research.github.io/pages/publications/posBasedDyn.pdf)): In position-based systems, reset is straightforward — copy stored initial positions back to current positions, zero all velocities, and reset Lagrange multipliers. Since PBD operates directly on positions rather than forces, there is no accumulated force state to clear.

**Our approach (Option 1):** Store initial state at `initialize()` — deep copy positions + velocities, restore on reset. This is better than re-initializing from config because it preserves pin positions set after initialization. We store `m_initialPositions` in the simulator and add a `reset()` method. For XPBD specifically, we also need to zero velocities and reset the gust state machine for deterministic behavior after reset.

---

## 3. Cloth Presets

### 3.1 ClothPresets Class

Follow the `ParticlePresets` factory method pattern (proven in codebase). Each preset returns a fully configured `ClothConfig` plus wind parameters.

```cpp
/// @brief Extended config that includes wind + drag alongside solver parameters.
struct ClothPresetConfig
{
    ClothConfig solver;           // Grid, mass, compliance, substeps, damping
    float windStrength = 0.0f;    // Default wind strength
    float dragCoefficient = 1.0f; // Aerodynamic drag
};

struct ClothPresets
{
    static ClothPresetConfig linenCurtain();     // Tabernacle entrance screens
    static ClothPresetConfig tentFabric();       // Goat hair / stiff tent material
    static ClothPresetConfig banner();           // Flags, banners
    static ClothPresetConfig heavyDrape();       // Thick partition veils
    static ClothPresetConfig stiffFence();       // Taut courtyard linen panels
};
```

### 3.2 Preset Parameter Values

Derived from Phase 8D tuning experience + fabric research:

| Preset | Mass | Substeps | Stretch | Shear | Bend | Damping | Wind | Drag |
|--------|------|----------|---------|-------|------|---------|------|------|
| Linen Curtain | 0.02 | 10 | 0.0001 | 0.001 | 0.5 | 0.05 | 8.0 | 2.0 |
| Tent Fabric | 0.08 | 6 | 0.00005 | 0.0005 | 0.05 | 0.04 | 3.0 | 1.0 |
| Banner | 0.03 | 8 | 0.0002 | 0.002 | 0.3 | 0.03 | 10.0 | 2.5 |
| Heavy Drape | 0.10 | 8 | 0.00001 | 0.0002 | 0.02 | 0.06 | 2.0 | 0.8 |
| Stiff Fence | 0.04 | 4 | 0.00002 | 0.0003 | 0.2 | 0.06 | 5.0 | 1.5 |

**Notes:**
- Values derived from working Phase 8D configs (entrance curtains, fence panels, demo banner)
- Linen Curtain = the current entrance curtain params (already tuned)
- Stiff Fence = the current fence panel params (already tuned)
- Tent Fabric, Banner, Heavy Drape = new, based on fabric density research

### 3.3 Preset Enum for Serialization

```cpp
enum class ClothPresetType : uint8_t
{
    CUSTOM = 0,        // User-modified parameters
    LINEN_CURTAIN,
    TENT_FABRIC,
    BANNER,
    HEAVY_DRAPE,
    STIFF_FENCE,
    COUNT
};
```

---

## 4. Editor UI — Inspector Panel

### 4.1 Layout

When an entity has a `ClothComponent`, the inspector shows:

```
[ Cloth Simulation ]  (collapsible header)
├─ Preset: [Linen Curtain  ▾]     (combo box)
├─ [Reset Simulation]              (button)
│
├─ ── Solver ──────────────────
│  Grid:       12 x 24            (read-only display)
│  Spacing:    [0.185  ]  m       (read-only — set at init)
│  Mass:       [0.020  ]  kg      (drag float)
│  Substeps:   [10     ]          (slider int, 1-20)
│  Damping:    [0.050  ]          (drag float, 0-0.5)
│
├─ ── Compliance ──────────────
│  Stretch:    [0.0001 ]          (drag float, 0-0.01, log)
│  Shear:      [0.001  ]          (drag float, 0-0.1, log)
│  Bend:       [0.500  ]          (drag float, 0-2.0)
│
├─ ── Wind ────────────────────
│  Direction:  [X][Y][Z]          (drag float3)
│  Strength:   [8.0    ]          (drag float, 0-30)
│  Drag:       [2.0    ]          (drag float, 0-10)
│
├─ ── Info ────────────────────
│  Particles:  288
│  Constraints: 1,632
│  Pinned:     12
```

### 4.2 Preset Interaction

- Selecting a preset from the combo box applies all parameters immediately
- If the user manually changes any parameter, the combo switches to "Custom"
- The combo tracks `ClothPresetType` per-component (not global state)

### 4.3 Live Parameter Updates

Most parameters can be updated live without reinitializing:
- **Mass, substeps, damping**: direct setter on ClothSimulator
- **Compliance values**: update on existing constraints (need new setters)
- **Wind direction, strength, drag**: existing setters

Parameters that require reinitialization (shown read-only):
- **Grid width/height, spacing**: changes particle count, need full reinit

### 4.4 New ClothSimulator Setters Required

```cpp
void setDamping(float damping);
void setParticleMass(float mass);
void setStretchCompliance(float compliance);
void setShearCompliance(float compliance);
void setBendCompliance(float compliance);
```

These modify the config and update existing constraints in-place.

---

## 5. Scene Integration — Tabernacle Refinements

### 5.1 Use Presets in Existing Cloth

Replace hardcoded `ClothConfig` blocks in `engine.cpp` with preset calls:
- Entrance curtains: `ClothPresets::linenCurtain()`
- Fence panels: `ClothPresets::stiffFence()`
- Demo banner: `ClothPresets::banner()`

### 5.2 Add Inner Partition Veil

The veil separating the Holy Place from the Holy of Holies (Exodus 26:31-33) is currently a static box. Replace with animated cloth using `ClothPresets::heavyDrape()`:
- Wider and shorter than entrance curtains (~4.45m wide x ~4.45m tall = 10x10 cubits)
- Pinned along the full top edge
- Minimal wind (interior — sheltered from gusts)
- Use existing `veilMat` (blue/purple/scarlet)

### 5.3 Courtyard Gate Curtain

The courtyard gate is currently described as "open (curtain pulled aside, not rendered)" in the code. Add an optional cloth gate curtain using `ClothPresets::linenCurtain()`:
- 20 cubits wide (~8.9m), 5 cubits tall (~2.225m)
- Pinned at top, moderate wind
- Use `veilMat` (matching biblical description)

---

## 6. Simulation Reset

### 6.1 ClothSimulator::reset()

New method that restores simulation to post-initialize state:

```cpp
void ClothSimulator::reset()
{
    // Restore initial particle state
    m_positions = m_initialPositions;
    m_velocities.assign(m_velocities.size(), glm::vec3(0.0f));
    m_prevPositions = m_positions;

    // Restore pin positions
    for (auto& pin : m_pinConstraints)
    {
        m_positions[pin.index] = pin.position;
    }

    // Reset gust state machine
    m_gustCurrent = 0.0f;
    m_gustTarget = 0.0f;
    m_gustTimer = 0.0f;
    m_windDirOffset = glm::vec3(0.0f);
    m_dirTimer = 0.0f;
    m_elapsed = 0.0f;
}
```

### 6.2 ClothComponent::reset()

```cpp
void ClothComponent::reset()
{
    m_simulator.reset();
    syncMesh();  // Upload reset positions to GPU
}
```

### 6.3 Storage of Initial State

In `ClothSimulator::initialize()`, after all setup:
```cpp
m_initialPositions = m_positions;  // Deep copy
```

This captures positions BEFORE any simulation runs but AFTER pins are applied by `ClothComponent::initialize()`. Note: the caller may further modify pins via `pinParticle()`/`setPinPosition()` after init — `syncMesh()` handles that. The reset restores to post-`initialize()` state, and the caller is responsible for re-applying any post-init pin changes.

---

## 7. File Structure

```
engine/
  physics/
    cloth_presets.h          // NEW: ClothPresetConfig, ClothPresetType, ClothPresets factory
    cloth_presets.cpp         // NEW: Preset parameter values
    cloth_simulator.h         // MODIFIED: add reset(), compliance/mass setters, m_initialPositions
    cloth_simulator.cpp       // MODIFIED: implement new methods
    cloth_component.h         // MODIFIED: add reset(), m_presetType tracking
    cloth_component.cpp       // MODIFIED: implement reset()
  editor/panels/
    inspector_panel.h         // MODIFIED: add drawClothComponent()
    inspector_panel.cpp       // MODIFIED: implement cloth inspector UI
  core/
    engine.cpp                // MODIFIED: use presets, add partition veil cloth, add gate curtain
tests/
  test_cloth_presets.cpp      // NEW: preset value validation tests
```

**New files:** 3 (cloth_presets.h, cloth_presets.cpp, test_cloth_presets.cpp)
**Modified files:** 6 (cloth_simulator.h/cpp, cloth_component.h/cpp, inspector_panel.h/cpp, engine.cpp)

---

## 8. Implementation Steps

### Step 1: ClothPresets (cloth_presets.h/cpp)
- Define `ClothPresetConfig` struct
- Define `ClothPresetType` enum
- Implement 5 factory methods with tuned parameters
- Add `getPresetName(ClothPresetType)` helper for UI display

### Step 2: ClothSimulator Extensions (cloth_simulator.h/cpp)
- Add `m_initialPositions` storage
- Save initial state at end of `initialize()`
- Implement `reset()` method
- Add setters: `setDamping()`, `setParticleMass()`, `setStretchCompliance()`, `setShearCompliance()`, `setBendCompliance()`
- Compliance setters iterate existing constraints and update values

### Step 3: ClothComponent Extensions (cloth_component.h/cpp)
- Add `m_presetType` member (defaults to CUSTOM)
- Add `reset()` method (delegates to simulator + syncMesh)
- Add `getPresetType()` / `setPresetType()` accessors
- Add `applyPreset(ClothPresetType)` that applies solver + wind params without reinit

### Step 4: Inspector Panel — Cloth UI (inspector_panel.h/cpp)
- Add `drawClothComponent(Entity&)` declaration to header
- Add `hasComponent<ClothComponent>()` check in `draw()`
- Implement preset combo, solver sliders, compliance sliders, wind controls
- Implement Reset button
- Show read-only info (particle count, constraint count, pin count)

### Step 5: Tabernacle Scene Updates (engine.cpp)
- Replace hardcoded `ClothConfig` with `ClothPresets::` calls
- Add inner partition veil as animated cloth (heavyDrape preset)
- Add courtyard gate curtain as animated cloth (linenCurtain preset)

### Step 6: Tests (test_cloth_presets.cpp)
- Validate all presets return sane values (mass > 0, substeps > 0, compliance >= 0)
- Test that preset configs can be used with ClothSimulator::initialize()
- Test reset() restores positions correctly
- Test live compliance setters update constraint values

---

## 9. Performance Considerations

### Current Budget
Phase 8D established ~200 FPS with 56 fence panels + 2 entrance curtains + 1 demo banner (59 total cloth objects). Adding 1-2 more cloths (veil, gate curtain) is negligible.

### Research: Multi-Cloth Performance in Other Engines

**NVIDIA APEX Clothing** ([docs](https://archive.docs.nvidia.com/gameworks/content/gameworkslibrary/physx/apexsdk/APEX_Clothing/Clothing_Module_Doc.html)) uses a sophisticated LOD budget system for managing many cloth actors:
- Each scene has a configurable **resource budget** (total vertex count x iteration count)
- Each cloth actor gets a **benefit** value (typically distance-based)
- Resources distribute to actors with higher benefit; low-benefit actors reduce quality or disable entirely
- **Physical LOD** reduces Max Distance first (constraining motion range) before disabling vertices
- Transitions between LOD levels use smooth blending with configurable blend time
- "A Clothing Actor that is not simulated comes at almost zero overhead"
- The cost for simulating a cloth scales linearly with number of simulated vertices

**Unreal Engine** ([Intel optimization guide](https://www.intel.com/content/www/us/en/developer/articles/technical/unreal-engine-4-blueprint-cpu-optimizations-for-cloth-simulations.html)) cloth simulations are CPU-taxing and run whether visible or not. Key optimization techniques:
- Boolean LOD switch: simulate at LOD 0, skip simulation at LOD 1 (triggered by area entry/exit)
- Occlusion culling: call "Was Recently Rendered" to stop simulation when actor is not visible
- Distance-based toggling: begin simulation when entering proximity, stop when leaving

**GPU cloth research** ([CMU](https://www.ri.cmu.edu/event/high-resolution-cloth-simulation-in-milliseconds-efficient-gpu-cloth-simulation-with-non-distance-barriers-and-subspace-reuse-interactions/)): Modern GPU approaches achieve millisecond-range simulation for meshes with one million DOF. GPU methods are approximately 8-10x faster than parallel CPU methods for high-resolution cloth.

### Implications for Vestige

Our current approach (CPU XPBD) is well-suited for the scene scale:
- 59 cloth objects with small grids (4x6 fence panels, 12x24 curtains) = low vertex count per cloth
- At ~200 FPS we have significant headroom
- Adding 2 more cloths (veil + gate) adds ~600 particles total — negligible

**Future optimization hooks if needed** (not implementing now, but the architecture supports them):
1. **Distance-based simulation skip**: disable `ClothComponent::update()` for cloths beyond a camera distance threshold
2. **Substep reduction**: reduce substeps for distant cloths (already exposed via `setSubsteps()`)
3. **Sleep detection**: if max particle velocity < threshold for N frames, skip simulation until a wind gust or interaction wakes it
4. **Frustum culling**: skip simulation for cloths outside the view frustum (aggressive but effective)

These match the NVIDIA APEX pattern of graduated quality reduction rather than binary on/off.

### Editor UI Overhead
ImGui widgets are trivially cheap (<0.1ms). The inspector only draws when the editor is visible and an entity is selected. No performance impact during gameplay.

### Live Parameter Changes
Compliance setters iterate constraint vectors (O(n) where n = constraints per cloth). For a typical 12x24 grid, there are ~1,500 constraints. Iterating and updating float values takes <0.01ms. Safe for per-frame updates from slider drags.

### Reset
`reset()` copies `m_initialPositions` (288 vec3s for entrance curtain = ~3.5KB). Trivially fast, safe to call from UI button.

---

## 10. Testing Strategy

### Unit Tests (test_cloth_presets.cpp)

| Test | What it verifies |
|------|-----------------|
| AllPresetsHavePositiveMass | Each preset returns mass > 0 |
| AllPresetsHavePositiveSubsteps | Each preset returns substeps >= 1 |
| AllPresetsHaveNonNegativeCompliance | Compliance values >= 0 for all presets |
| PresetsCanInitializeSimulator | Each preset's config works with ClothSimulator::initialize() |
| PresetNamesAreNonEmpty | getPresetName() returns valid strings for all types |
| ResetRestoresPositions | After simulate(), reset() puts particles back to initial positions |
| LiveComplianceUpdate | setStretchCompliance() updates all stretch constraints |
| LiveMassUpdate | setParticleMass() updates all inverse masses |
| LiveDampingUpdate | setDamping() changes config value correctly |

### Visual Tests
1. Open editor, select a cloth entity → cloth properties appear in inspector
2. Change preset from combo → cloth behavior changes immediately
3. Drag compliance sliders → cloth gets stiffer/softer in real-time
4. Click Reset → cloth snaps back to initial hanging position
5. Inner partition veil hangs straight with minimal movement
6. Courtyard gate curtain sways gently with wind

---

## Appendix A: Preset Design Rationale

**Linen Curtain** — Tuned in Phase 8D for the entrance screens. Light fabric (0.02 kg/particle), high bend compliance (0.5) for flowing folds, moderate damping (0.05) so it settles between gusts. Wind-responsive at strength 8.0 with drag 2.0. Based on medium linen at ~200 gsm — the typical weight for curtain-grade linen fabric. The flowing behavior matches archaeological descriptions of finely woven tabernacle linen.

**Tent Fabric** — Heavier goat hair cloth (0.08 kg/particle). Low bend compliance (0.05) resists folding. Fewer substeps (6) since the fabric is naturally stiff. Lower wind strength (3.0) because the weight resists displacement. Research shows goat hair tent cloth reaches 1,588 g/m^2 in traditional nomadic tents — extremely heavy. Our particle mass reflects this density scaled down for the grid resolution. The fiber swells when wet, making it naturally water-resistant, which translates to simulation as low wind permeability.

**Banner** — Light fabric (0.03 kg/particle) designed to flutter prominently. Highest wind strength (10.0) and drag (2.5) for dramatic movement. Moderate bend compliance (0.3) — not as flowing as linen but more responsive than tent fabric. Based on Blender's cotton preset pattern (low mass, low-medium bending stiffness). The low damping (0.03) allows sustained fluttering instead of settling to rest.

**Heavy Drape** — Dense, luxurious fabric (0.10 kg/particle). Very low bend compliance (0.02) — holds shape, minimal folding. Low wind response (2.0) — only shifts slightly in strong gusts. Suitable for the partition veil and thick curtains. The Temple veil was described as elaborately woven, possibly layered, and very thick — our parameters reflect this weight and stiffness. Follows the Blender pattern where leather/denim have the highest bending stiffness values.

**Stiff Fence** — Taut linen panels (0.04 kg/particle). Very low stretch compliance (0.00002) keeps panels tight between posts. Only 4 substeps for performance (56 panels in the scene). Moderate wind (5.0) for subtle rippling. These represent the fine linen panels of the tabernacle courtyard enclosure — stretched taut between posts, not free-hanging like curtains.

---

## Appendix B: Cross-Engine Parameter Mapping Reference

For future reference, this table shows how our XPBD parameters relate to parameters in other engines:

| Vestige (XPBD) | Unity Cloth | Unreal Chaos Cloth | Blender | Notes |
|----------------|-------------|-------------------|---------|-------|
| particleMass | (implicit from mesh) | Density (gsm) | Mass | Mass per particle vs. per area |
| stretchCompliance | stretchingStiffness (0-1) | Stretch Stiffness | Structural Stiffness | Compliance is inverse stiffness |
| shearCompliance | (combined with stretch) | Shear Stiffness | (implicit) | Not all engines separate shear |
| bendCompliance | bendingStiffness (0-1) | Bend Stiffness | Bending Stiffness | Higher compliance = softer bends |
| damping | damping (0-1) | Damping | Spring Damping | Velocity damping per step |
| windStrength | externalAcceleration | Drag/Lift + Wind | (Force Field) | Wind as external force |
| dragCoefficient | (none) | Drag Coefficient | Air Damping | Aerodynamic drag on triangles |
| substeps | (solver iterations) | Solver Frequency | Quality | More iterations = stiffer result |

---

## Appendix C: Sources

### Engine Documentation
- [Unity Cloth Manual](https://docs.unity3d.com/Manual/class-Cloth.html)
- [Unity Cloth.bendingStiffness API](https://docs.unity3d.com/ScriptReference/Cloth-bendingStiffness.html)
- [Unity Cloth.stretchingStiffness API](https://docs.unity3d.com/ScriptReference/Cloth-stretchingStiffness.html)
- [Unity Cloth Reset Discussion](https://discussions.unity.com/t/how-to-reset-the-cloth-during-runtime/926913)
- [Unreal Clothing Tool Properties Reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/clothing-tool-in-unreal-engine---properties-reference)
- [Unreal Chaos Cloth Properties Tutorial](https://dev.epicgames.com/community/learning/tutorials/jOrv/unreal-engine-chaos-cloth-tool-properties-reference)
- [Unreal Chaos Cloth Overview](https://dev.epicgames.com/community/learning/tutorials/OPM3/unreal-engine-chaos-cloth-tool-overview)
- [Unreal Cloth LOD Tutorial](https://dev.epicgames.com/community/learning/tutorials/VLb5/unreal-engine-cloth-lod-tutorial)
- [Unreal Panel Cloth Editor Walkthrough (5.4)](https://dev.epicgames.com/community/learning/tutorials/Mpze/unreal-engine-panel-cloth-editor-walkthrough-updates-5-4)
- [Unreal Cloth Reset Discussion](https://forums.unrealengine.com/t/how-to-reset-cloth-sim/2331825)
- [Godot SoftBody3D Documentation](https://docs.godotengine.org/en/stable/tutorials/physics/soft_body.html)
- [Havok Cloth Preview Tool Introduction](https://www.havok.com/blog/introduction-havok-cloth-preview-tool/)
- [Havok Cloth for Unreal](https://www.havok.com/havok-cloth-for-unreal/)
- [Blender Cloth Presets (source code)](https://github.com/walac/blender-wayland/tree/master/release/scripts/presets/cloth)
- [Blender Cloth Settings Manual](https://docs.blender.org/manual/en/latest/physics/cloth/settings/index.html)
- [NVIDIA APEX Clothing Module Documentation](https://archive.docs.nvidia.com/gameworks/content/gameworkslibrary/physx/apexsdk/APEX_Clothing/Clothing_Module_Doc.html)
- [Intel UE4 Cloth CPU Optimizations](https://www.intel.com/content/www/us/en/developer/articles/technical/unreal-engine-4-blueprint-cpu-optimizations-for-cloth-simulations.html)

### Fabric Properties
- [The Linen Shack -- Linen Weights Explained](https://thelinenshack.com/blogs/blog/linen-weight-explained)
- [Cotton Linen GSM Chart](https://szoneierfabrics.com/cotton-linen-fabric-gsm-chart-weight-classifications-for-different-end-uses/)
- [Goat Hair Fabric Properties for Iranian Nomadic Tent (2025)](https://www.tandfonline.com/doi/full/10.1080/15440478.2025.2515465)
- [Tabernacle Goat Hair Covering](https://www.thetabernacleman.com/post/coverings-of-the-tabernacle-of-moses-part-2-goat-hair-covering-exodus-26-7-by-dr-terry-harman)
- [Leather Properties and Drapability Study](https://journals.sagepub.com/doi/full/10.1177/1558925020968825)
- [Leather Measures and Weights Dictionary](https://www.leather-dictionary.com/index.php/Measures_and_weights)
- [Leather Weight and Thickness Guide](https://hoplokleather.com/leather-weight-and-thickness-guide/)
- [Fabric Weight for Curtains Guide](https://sewingtrip.com/fabric-weight-for-curtains/)
- [Understanding Drapery Weight](https://vestadraperyhardware.com/understanding-drapery-weight-a-guide-to-choosing-the-right-hardware/)
- [Tabernacle Artistry: Text and Textile](https://www.thetorah.com/article/the-tabernacles-artistry-text-and-textile)
- [Temple Veil Thickness](https://cbumgardner.wordpress.com/2010/04/06/the-thickness-of-the-temple-veil/)

### Simulation Techniques
- [Muller et al. -- Position Based Dynamics (2006)](https://matthias-research.github.io/pages/publications/posBasedDyn.pdf)
- [Macklin et al. -- XPBD Position-Based Simulation Methods (EG 2015)](https://mmacklin.com/EG2015PBD.pdf)
- [Fabric Physics: MD to UE Conversion](https://subobject.co/cloth-simulation-unreal-engine/)
- [Cloth in the Wind: Physical Measurement through Simulation (CVPR 2020)](https://openaccess.thecvf.com/content_CVPR_2020/papers/Runia_Cloth_in_the_Wind_A_Case_Study_of_Physical_Measurement_CVPR_2020_paper.pdf)
- [GPU Cloth with OpenGL Compute Shaders](https://github.com/likangning93/GPU_cloth)
- [Real-Time Cloth Simulation in XR: Unity vs PBD (MDPI 2025)](https://www.mdpi.com/2076-3417/15/12/6611)
- [Maya nCloth Initial State Setting](https://download.autodesk.com/us/maya/maya_2014_gettingstarted/files/Creating_nCloth_Clothing_Setting_the_initial_state.htm)
- [Valve Source Engine Cloth Simulation](https://developer.valvesoftware.com/wiki/Cloth_Simulation)
