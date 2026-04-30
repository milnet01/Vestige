# Phase 12-26 Design-Doc Stubs

Phases 12 through 26 do not yet have full design docs. The ROADMAP-level scope per phase is the canonical reference until each phase's slot in the development queue arrives, at which point a full `docs/phases/phase_NN_design.md` (mirroring the Phase 10.7 / 11A / 11B template) will be authored and reviewed before any implementation slices begin (per CLAUDE.md Rule 1).

This file is the placeholder index. **Status: stub** for all entries unless a sibling `phase_NN_*_design.md` already exists (noted below).

---

## How to use this index

For each phase below:
- **ROADMAP**: jump to the section in `/ROADMAP.md` for the canonical scope and item list.
- **Status**: where the phase sits in the development queue. `planned-distant` = work not expected within the next two phases; `in-research` = research notes exist but no design doc; `partial-design` = a sibling design doc exists for a sub-area.
- **When a phase enters the active queue**: replace the row's stub with a full `phase_NN_<topic>_design.md` per the established template, and link it here.

---

## Phase 12 — Distribution

| Field | Value |
|-------|-------|
| ROADMAP | `## Phase 12: Distribution` (~line 1025) |
| Status | planned-distant |
| Scope summary | Steam packaging, installer / runtime, asset packaging, save-format compatibility checks, MIT-release-blocking work (per project memory `project_open_source_plan.md`). |
| Sibling design docs | None today. |
| Notes | Phase 11A's `zstd` integration is shared with Phase 12 asset packaging — single integration, not two; track as a §13-dep on the 12 doc when it lands. |

## Phase 13 — Advanced Rendering

| Field | Value |
|-------|-------|
| ROADMAP | `## Phase 13: Advanced Rendering` (~line 1094) |
| Status | partial-design (research-update sub-sections shipped 2026-04-28: Gaussian splatting, voxel techniques, dynamic-weather technique pinning) |
| Scope summary | Screen-space effects (SSR / SSGI / DoF / motion blur), advanced materials (SSS / anisotropy / hair), GI (probe / surfel / radiance-cascade / Brixelizer / ReSTIR), Vulkan + ray tracing, tessellation, shadow techniques (VSM / PCSS / MegaLights), upscaling (FSR), GPU-driven rendering, voxel techniques (VCT / SVO / SVDAG), 3DGS rendering, VR. |
| Sibling design docs | None — the per-feature ROADMAP rows point at primary references (Kerbl 2023, NVIDIA VXGI, Aokana 2025, etc.). Each feature gets a focused design doc when scheduled. |
| Notes | Phase 13 is the largest scope phase; it is implemented as a long sequence of independent feature slices spread across the post-MIT timeline rather than as a single phase block. |

## Phase 14 — Adaptive Geometry System

| Field | Value |
|-------|-------|
| ROADMAP | `## Phase 14: Adaptive Geometry System` (~line 1261) |
| Status | in-research |
| Scope summary | Automatic mesh simplification, cluster-based decomposition (meshlets), screen-space-error LOD, virtual geometry streaming, mesh-shader integration, micropolygon rendering. |
| Sibling design docs | None. |
| Notes | The "research-direction" phrasing in ROADMAP is deliberate — this phase will start with a research sweep + design doc rather than slice planning. |

## Phase 15 — Atmospheric Rendering

| Field | Value |
|-------|-------|
| ROADMAP | `## Phase 15: Atmospheric Rendering` (~line 1291) |
| Status | partial-design (2026-04-28 research-update sub-section pins technique choices: Worley+Perlin clouds, GPU particle precipitation, wet-surface stack, 30-90 s state-machine blend window) |
| Scope summary | Procedural sky (Rayleigh / Mie), volumetric clouds, weather state machine + rain / snow / hail / dust / lightning, fog modulation, god rays. |
| Sibling design docs | None. |
| Notes | Phase 9B Atmosphere & Weather domain system is the shipped wrapper; Phase 15 fills in the actual rendering. |

## Phase 16 — Scripting and Interactivity

| Field | Value |
|-------|-------|
| ROADMAP | `## Phase 16: Scripting and Interactivity` (~line 1391) |
| Status | partial (Phase 9E shipped the visual-scripting foundation; Phase 16 is the *advanced* features) |
| Scope summary | Behavior trees, AI perception, AI director, cutscene system, dialogue. |
| Sibling design docs | Phase 11A landed BT + AI perception runtime; Phase 16 will extend with director-level AI + cutscenes + dialogue. |
| Notes | When Phase 16 reaches the queue, its design doc cross-references Phase 11A for the BT runtime contract. |

## Phase 17 — Terrain and Landscape

| Field | Value |
|-------|-------|
| ROADMAP | `## Phase 17: Terrain and Landscape` (~line 1470) |
| Status | core-complete (Phase 5I shipped the foundation) |
| Scope summary | Future enhancements on top of the Phase 5I CDLOD terrain — e.g. terrain chunking for arbitrarily-large worlds (per memory `project_terrain_chunking.md`), advanced erosion / weathering simulation, deeper biome integration. |
| Sibling design docs | Multiple Phase 5I docs exist under `docs/phases/phase_05i_*.md`. |
| Notes | The Phase 17 doc when it lands is a delta against Phase 5I, not a fresh design. |

## Phase 18 — 2D Game and Scene Support

| Field | Value |
|-------|-------|
| ROADMAP | `## Phase 18: 2D Game and Scene Support` (~line 1495) |
| Status | partial (sprite + tilemap shipped via earlier phases; 2D-specific advanced features like 2D physics polish, 2D-only render path, etc. land in 18) |
| Scope summary | 2D-specific game support beyond what already ships: better 2D physics, 2D-only render path optimisation, 2D scene templates beyond the existing side-scroller / shmup helpers. |
| Sibling design docs | Sprite + tilemap pieces are inside Phase 5/Phase 9 docs already. |
| Notes | Engine already ships `engine/scene/sprite_animation`, `tilemap_*`, `Camera2DComponent`, `game_templates_2d.h` — Phase 18 builds on those rather than introducing them. |

## Phase 19 — Procedural Generation

| Field | Value |
|-------|-------|
| ROADMAP | `## Phase 19: Procedural Generation` (~line 1543) |
| Status | planned-distant |
| Scope summary | Procedural splatmap generation, biome auto-painting, procedural foliage placement (basic shipped — advanced authoring tools land here), wave-function-collapse for tile arrangement, terrain erosion sim. |
| Sibling design docs | None. |
| Notes | Density-map and biome-preset primitives shipped via Phase 5I. Phase 19 is the authoring-tool layer. |

## Phase 20 — Networking and Multiplayer

| Field | Value |
|-------|-------|
| ROADMAP | `## Phase 20: Networking and Multiplayer` (~line 1583) |
| Status | planned-distant |
| Scope summary | Networking foundation, lockstep + state-replication, server / client topology, latency-compensation, co-op horror (cross-references Phase 11B horror archetype). |
| Sibling design docs | None. |
| Notes | Phase 11B horror's co-op story is explicitly deferred to Phase 20; the 11B doc has the dependency call-out. |

## Phase 21 — Build Wizard

| Field | Value |
|-------|-------|
| ROADMAP | `## Phase 21: Build Wizard — Project Creation and Export` (~line 1619) |
| Status | planned-distant |
| Scope summary | Project-creation wizard, per-platform export, asset bundling, settings-default templates per archetype. |
| Sibling design docs | None. |
| Notes | Pairs with Phase 12 distribution work; the Build Wizard is the user-facing front-end to the distribution pipeline. |

## Phase 22 — Collaborative Editing

| Field | Value |
|-------|-------|
| ROADMAP | `## Phase 22: Collaborative Editing — Real-Time Multi-User Projects` (~line 1677) |
| Status | planned-distant |
| Scope summary | Real-time multi-user editing of scenes, CRDT or operational-transform-based merge, presence indicators, conflict UI. |
| Sibling design docs | None. |
| Notes | Phase 22 depends on Phase 20 networking primitives. |

## Phase 23 — AI Assistance

| Field | Value |
|-------|-------|
| ROADMAP | `## Phase 23: AI Assistance — Prompt-Driven Engine Integration` (~line 1756) |
| Status | planned-distant |
| Scope summary | LLM-driven scene authoring, asset suggestion, gameplay-script generation. |
| Sibling design docs | None. |
| Notes | Scope and ethics call-outs (provenance, copyright, hallucination-prevention) will be the bulk of the Phase 23 doc when it lands. |

## Phase 24 — Structural / Architectural Physics

| Field | Value |
|-------|-------|
| ROADMAP | `## Phase 24: Structural / Architectural Physics` (~line 1854) |
| Status | partial-design — full design doc shipped at `phase_24_structural_physics_design.md` |
| Scope summary | Load-bearing-aware destruction, mortar / stone / wood material differentiation, real-world architectural failure modes for biblical-structure scenes. |
| Sibling design docs | `docs/phases/phase_24_structural_physics_design.md` (existing). |
| Notes | The existing design doc is the canonical reference; this stubs entry just confirms it's the active design. |

## Phase 25 — Open-World Game Systems

| Field | Value |
|-------|-------|
| ROADMAP | `## Phase 25: Open-World Game Systems` (~line 1877) |
| Status | planned-distant |
| Scope summary | Time-of-day cycle propagation, regional weather zones, NPC schedules, traffic, dynamic events, faction systems. |
| Sibling design docs | None. |
| Notes | Phase 25 builds on Phase 9B (Atmosphere & Weather domain system), Phase 15 (atmospheric rendering), Phase 11A (BT + AI perception), Phase 11B (vehicle physics for traffic). When it reaches the queue, the design doc cross-references each. |

## Phase 26 — Racing Game Systems

| Field | Value |
|-------|-------|
| ROADMAP | `## Phase 26: Racing Game Systems` (~line 1926) |
| Status | partial — Phase 11B vehicle physics + replay infrastructure ships first |
| Scope summary | Race-mode-specific systems beyond the Phase 11B vehicle archetype: track / lap / sector logic, AI competitors, time-trial / ghost-replay rules, driver-license / progression. |
| Sibling design docs | None. |
| Notes | Phase 11B builds the vehicle physics + replay foundation; Phase 26 builds the race-rules layer on top. |

---

## Cross-cutting items not in any single phase

A handful of ROADMAP items don't belong to one phase but to multiple:

- **Open-Source Release** (`## Open-Source Release` ~line 1978) — pre-release checklist tracked in `docs/PRE_OPEN_SOURCE_AUDIT.md`. Touches Phase 12 (distribution) and Phase 21 (build wizard).
- **Target Projects** (`## Target Projects` ~line 2050) — biblical-walkthrough projects (Tabernacle, Solomon's Temple) that consume the engine. These are not engine phases; they're separate downstream projects.

---

## Change log

| Date | Author | Change |
|------|--------|--------|
| 2026-04-30 | milnet01 | Initial stubs index for Phases 12-26. Each entry will be replaced with a full `phase_NN_<topic>_design.md` when its phase enters the active queue. |
