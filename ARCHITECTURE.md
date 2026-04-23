# Vestige Engine Architecture

> **How to read this document.** §1–9 describe the Phase-1 skeleton (core subsystems, loop, event bus, scene graph, rendering, folder structure) and remain authoritative. §10–19 document everything added since (animation, physics, environment, editor, GPU particles, profiling, cloth collision, scripting). Where a subsystem appears in both halves, the later section is the current reality.

---

## 1. High-Level Overview

**Subsystem + Event Bus.** Independent subsystems orchestrated by the central `Engine` class, communicating via a shared `EventBus`. Since Phase 9A the pattern is formalised via `ISystem` (`engine/core/i_system.h`, `system_registry.h`): every domain system (rendering, physics, animation, audio, navigation, UI, scripting, …) implements `ISystem` and registers with `SystemRegistry`, which drives `initialize → update → fixedUpdate → submitRenderData → shutdown` in order each frame.

```
┌─────────────────────────────────────────────────────┐
│                      Engine                         │
│  Timer · Logger · ResourceManager                   │
│  Window · Renderer · SceneManager                   │
│  InputManager · EventBus                            │
└─────────────────────────────────────────────────────┘
```

---

## 2. Engine Loop

```
while (running)
{
    float dt = timer.update();
    window.pollEvents();
    inputManager.update();
    eventBus.dispatchAll();
    sceneManager.update(dt);
    renderer.render(sceneManager.getActiveScene());
    window.swapBuffers();
}
```

---

## 3. Subsystems

Self-contained modules; they communicate through the Event Bus, not direct references.

| Subsystem | Path | Role / Events |
|-----------|------|---------------|
| Engine | `core/engine` | Owns + orchestrates all subsystems; runs main loop. |
| Window | `core/window` | OS window + GL context (GLFW). Emits `WindowResize`, `WindowClose`. |
| Timer | `core/timer` | Delta time, FPS, elapsed. Queried directly. |
| Logger | `core/logger` | Trace/Debug/Info/Warning/Error/Fatal → console (+file). |
| InputManager | `core/input_manager` | Keyboard/mouse/gamepad (Xbox, PS via GLFW). Emits `KeyPressed/Released`, `MouseMoved`, `MouseButtonPressed`, `GamepadConnected/Disconnected`. |
| EventBus | `core/event_bus` | Synchronous typed pub/sub, one dispatch phase per frame. |
| Renderer | `renderer/renderer` | All GL rendering. Listens `WindowResize`. |
| SceneManager | `scene/scene_manager` | Scenes + entity lifecycle. |
| ResourceManager | `resource/resource_manager` | Load + cache meshes / textures / shaders / fonts (OBJ, glTF, PNG/JPG, GLSL, TTF). |

---

## 4. Event Bus

Events are lightweight structs inheriting from a base `Event`:

```cpp
struct WindowResizeEvent : public Event { int width, height; };
struct KeyPressedEvent : public Event { int keyCode; bool isRepeat; };

eventBus.subscribe<WindowResizeEvent>([this](const auto& e) {
    updateViewport(e.width, e.height);
});
eventBus.publish(WindowResizeEvent{1920, 1080});
```

**Rules:** dispatched once per frame in `dispatchAll()`; listeners must not mutate the event; handlers must be fast.

---

## 5. Scene Graph

Scenes contain a hierarchy of Entities; Entities hold Components.

```
Scene
└── Root (Transform)
    ├── Camera (Transform, Camera)
    ├── Sun (Transform, DirectionalLight)
    ├── Tabernacle (Transform)
    │   ├── Outer Court (Transform, MeshRenderer)
    │   ├── Holy Place → Table of Showbread, Lampstand (+ PointLight), Altar of Incense
    │   └── Holy of Holies → Ark of the Covenant
    └── Ground Plane (Transform, MeshRenderer, Material)
```

**Components (core):** `Transform`, `MeshRenderer`, `Camera`, `DirectionalLight`, `PointLight`, `SpotLight`, `Material`.

---

## 6. Rendering Pipeline

**Phase 1 (basic):** clear → view+projection from active Camera → per-entity (model matrix, bind shader, set uniforms, bind VAO, draw) → swap.

**Current (as of 2026-04-19):**
- **Shadow pass** — directional CSM (4 cascades), point, spot (Phases 4–5).
- **Post-processing** — TAA, bloom, SSAO, tonemap (Reinhard/ACES), color-grading LUT, parallax occlusion.
- **SMAA** — `renderer/smaa.{h,cpp}`.
- **IBL** — prefilter + BRDF LUT via `environment_map.{h,cpp}`, `light_probe.{h,cpp}`; unified in `ibl_prefilter.h`.
- **GPU-driven culling / instancing** — frustum-culled MDI via `gpu_culler.{h,cpp}` + `indirect_buffer.{h,cpp}`.
- **GI** — SH probe grid + radiosity bake shipped (see `docs/GI_ROADMAP.md`). SSGI next; hybrid RT / cone tracing planned.
- **Not started:** deferred rendering (forward-plus + GPU culling has sufficed).
- **Planned (long-term):** Vulkan backend, ray tracing.

---

## 7. Folder Structure

```
vestige/
├── CMakeLists.txt · CLAUDE.md · ROADMAP.md · CHANGELOG.md · VERSION
├── CODING_STANDARDS.md · ARCHITECTURE.md · SECURITY.md · CONTRIBUTING.md
├── LICENSE · ASSET_LICENSES.md · THIRD_PARTY_NOTICES.md
├── .gitleaks.toml · .pre-commit-config.yaml · .clang-format
│
├── engine/                           # Engine static library
│   ├── core/       Engine, Window, Timer, Logger, InputManager, EventBus,
│   │              ISystem, SystemRegistry, SystemEvents, FirstPersonController
│   ├── renderer/   Renderer, Shader, Mesh, Texture, Camera, Material,
│   │              Framebuffer, ShadowMap (CSM + point), Skybox, TAA, SMAA,
│   │              Bloom, SSAO, TonemapLUT, EnvironmentMap, LightProbe,
│   │              SHProbeGrid, RadiosityBaker, InstanceBuffer, IndirectBuffer,
│   │              GpuCuller, ParticleRenderer, GpuParticleSystem, WaterRenderer,
│   │              WaterFbo, FoliageRenderer, TreeRenderer, TerrainRenderer,
│   │              TextRenderer, DebugDraw, MeshPool, IblPrefilter,
│   │              ScopedForwardZ, FrameDiagnostics
│   ├── scene/      Scene, SceneManager, Entity, Component, MeshRenderer,
│   │              CameraComponent, LightComponent, ParticleEmitter,
│   │              GpuParticleEmitter, ParticlePresets, WaterSurface,
│   │              InteractableComponent, PressurePlateComponent
│   ├── resource/   ResourceManager, Model, AsyncTextureLoader, FileWatcher
│   ├── utils/      ObjLoader, GltfLoader, ProceduralMesh, CatmullRomSpline,
│   │              CubeLoader, EntitySerializer, MaterialLibrary, JsonSizeCap,
│   │              DeterministicLcgRng
│   ├── physics/    PhysicsWorld, RigidBody, CharacterController,
│   │              PhysicsConstraint, ClothSimulator, ClothComponent,
│   │              ClothPresets, FabricMaterial, ClothMeshCollider, Bvh,
│   │              SpatialHash, Ragdoll (+Preset), GrabSystem, Fracture,
│   │              DeformableMesh, BreakableComponent, Dismemberment,
│   │              StasisSystem, PhysicsDebug
│   ├── animation/  Skeleton, AnimationClip, AnimationSampler,
│   │              AnimationStateMachine, SkeletonAnimator, Easing, Tween,
│   │              IkSolver, MorphTarget, FacialAnimation (+FacialPresets,
│   │              EyeController, VisemeMap, AudioAnalyzer, LipSync),
│   │              MotionMatcher (+FeatureVector, KdTree, MotionDatabase,
│   │              TrajectoryPredictor, Inertialization, MotionPreprocessor,
│   │              MirrorGenerator)
│   ├── environment/ Terrain, EnvironmentForces, DensityMap, FoliageChunk,
│   │              FoliageManager, SplinePath, BiomePreset
│   ├── formula/    Expression, ExpressionEval, Formula, FormulaLibrary,
│   │              PhysicsTemplates, CodegenCpp, CodegenGlsl, LutGenerator,
│   │              LutLoader, CurveFitter, FormulaPreset, QualityManager,
│   │              NodeGraph, SensitivityAnalysis, FormulaBenchmark,
│   │              FormulaDocGenerator
│   ├── audio/      AudioEngine (OpenAL Soft), AudioClip, AudioSourceComponent
│   ├── ui/         SpriteBatchRenderer, UIElement (UICanvas/UIImage/UILabel/
│   │              UIPanel), UiSignal
│   ├── navigation/ NavMeshBuilder (Recast), NavMeshQuery (Detour),
│   │              NavMeshConfig, NavAgentComponent
│   ├── scripting/  ScriptValue, PinId, Blackboard, NodeTypeRegistry,
│   │              ScriptGraph, ScriptInstance, ScriptContext,
│   │              ScriptComponent; core/event/action/pure/flow/latent nodes
│   ├── systems/    Domain ISystem impls: Atmosphere, Particle, Water, Vegetation,
│   │              Terrain, Cloth, Destruction, Character, Lighting, Audio, UI,
│   │              Navigation, Scripting
│   ├── editor/     ImGui editor (dockable)
│   │   ├── editor.cpp, editor_camera, selection, entity_factory,
│   │   │   entity_actions, command_history, file_menu, recent_files,
│   │   │   scene_serializer, material_preview, prefab_system
│   │   ├── panels/   Hierarchy, Inspector, AssetBrowser, History,
│   │   │             ImportDialog, Environment, Terrain, Performance,
│   │   │             Validation, ScriptEditor, TextureViewer, HdriViewer,
│   │   │             ModelViewer, TemplateDialog, Welcome
│   │   ├── widgets/  AnimationCurve, ColorGradient, CurveEditor, GradientEditor,
│   │   │             NodeEditor
│   │   ├── tools/    Brush/TerrainBrush, Ruler, Wall, Room, Cutout, Roof, Stair,
│   │   │             Path (+BrushPreview)
│   │   └── commands/ TerrainSculptCommand + scripting commands
│   ├── profiler/   GpuTimer, CpuProfiler, MemoryTracker, PerformanceProfiler
│   └── testing/    VisualTestRunner
│
├── app/                              # main.cpp executable
├── assets/                           # runtime assets (copied to build)
│   ├── shaders/   ~60 GLSL programs
│   ├── textures/  CC0 Poly Haven 2K PBR
│   ├── models/    glTF samples (CesiumMan, Fox, …)
│   └── fonts/     Arimo (OFL 1.1)
├── external/                         # third-party (FetchContent)
│   ├── GLFW, GLM, ImGui, ImGuizmo, imgui-node-editor, Jolt, OpenAL Soft,
│   │   Recast/Detour, FreeType, nlohmann/json, tinyexr, tinygltf
│   ├── glad/      Vendored GL loader
│   ├── stb/       Single-header libs
│   └── dr_libs/   Audio decoders
├── tools/
│   ├── audit/              Python audit tool (tier 1–6 static analysis)
│   └── formula_workbench/  Interactive formula fitter + codegen
├── tests/                            # Google Test (~1880 tests)
├── docs/                             # PHASE*_DESIGN, *_RESEARCH, GI_ROADMAP,
│                                     #   PRE_OPEN_SOURCE_AUDIT, TABERNACLE_SPECS
├── scripts/                          # pre-commit + launch
├── packaging/                        # vestige-editor wrapper + .desktop
└── .github/
    ├── workflows/ci.yml              # Debug+Release, audit tier 1, gitleaks
    ├── ISSUE_TEMPLATE/ · PULL_REQUEST_TEMPLATE.md
    └── dependabot.yml                # Weekly github-actions + pip
```

**Build output:** `build/lib/libvestige_engine.a`, `build/bin/{vestige,vestige-editor,vestige_tests,formula_workbench}`, `build/_deps/`.

---

## 8. Dependency Flow

Downward only — no circular refs.

```
app → engine → { core, renderer, scene, resource, utils }
```

**Internal:** `core` depends on nothing (except its own EventBus); `renderer` and `scene` depend on `core`; `scene` also depends on `renderer` (for components); `resource` depends on `core`; `utils` depends on nothing.

**Third-party licenses:** GLFW (Zlib), GLM/glad/stb_image (MIT / Public Domain), Google Test/Assimp (BSD-3). All permissive / commercial-OK.

---

## 9. Platform Abstraction

Targets Linux + Windows. Platform-specific code isolated:
- **GLFW** — window/input (cross-platform).
- **glad** — GL function loading (cross-platform).
- **CMake** — build differences.
- Remaining platform code in `engine/utils/` guarded with `VESTIGE_PLATFORM_{LINUX,WINDOWS}`.

---

## 10. Animation (`engine/animation/`)

Skeletal animation, tweening, IK, morph targets.

**Data flow:** glTF → `GltfLoader` → `Skeleton` (joint hierarchy + inverse binds), `AnimationClip` (keyframe channels), `MorphTargetData` (per-vertex deltas). Per frame: `AnimationSampler` (interp) → `SkeletonAnimator` (playback, crossfade, root motion) → bone matrices (GPU skinning). IK runs post-process. `TweenManager` animates entity properties.

| Class | File | Purpose |
|-------|------|---------|
| `Skeleton` | `skeleton.h/cpp` | Joint hierarchy, inverse bind matrices, bind pose |
| `AnimationClip` | `animation_clip.h/cpp` | Named TRS + weight channels |
| `AnimationSampler` | `animation_sampler.h/cpp` | STEP / LINEAR / CUBICSPLINE interp |
| `SkeletonAnimator` | `skeleton_animator.h/cpp` | Clip playback, crossfade, root motion |
| `AnimationStateMachine` | `animation_state_machine.h/cpp` | Parameter-driven state graph |
| `Tween` / `TweenManager` | `tween.h/cpp` | Property animation |
| IK solvers | `ik_solver.h/cpp` | Two-bone, look-at, foot IK |
| `MorphTargetData` | `morph_target.h/cpp` | Blend shapes |
| `Easing` | `easing.h/cpp` | 32 Penner functions + cubic bezier |

**Skeletal pipeline.** Sampler binary-searches keyframes; animator crossfades via per-bone lerp/slerp; hierarchy walk (`T * R * S` parent → child); final = `globalTransform * inverseBindMatrix`; root motion extracts horizontal delta.

**State machine.** Named states map to clip indices; transitions fire on parameter conditions (float compare, bool/trigger); exit-time prevents mid-animation switch; triggers auto-reset.

**IK.** Two-bone (analytic, law of cosines + pole vector), look-at (single-joint, angle-clamped), foot IK (two-bone leg + ankle ground align + pelvis offset). All support NLerp weight blend.

---

## 11. Physics (`engine/physics/`)

Rigid-body via Jolt, character controller, XPBD cloth, constraints.

| Class | Purpose |
|-------|---------|
| `PhysicsWorld` | Jolt wrapper. Fixed 60 Hz accumulator. Bodies, constraints, raycasts. One per scene. |
| `RigidBody` | STATIC / DYNAMIC / KINEMATIC motion; BOX/SPHERE/CAPSULE shapes. Syncs Jolt → entity each frame. |
| `PhysicsCharacterController` | Jolt `CharacterVirtual` wrapper — walk/fly, stair step, slope limit, ground detect. |
| `ClothSimulator` | CPU XPBD: rectangular grid, structural/shear/bending constraints, pins, sphere/plane/cylinder/box colliders, wind with gust state machine, sleep, LRA tethers. |
| `ClothComponent` | Links simulator to `DynamicMesh`. Fixed 60 Hz. Fabric presets (linen, tent, banner, drape, fence). |
| `PhysicsConstraint` | Hinge, fixed, slider, spring, distance; breakable force thresholds. |

**Rigid-body pipeline.** Accumulate dt → step Jolt @ 60 Hz → `RigidBody::syncToEntity()` writes pose to entity.

**Cloth pipeline.** Accumulate dt → 60 Hz step: external forces (gravity, wind) → distance constraints → LRA tethers → collisions → pin constraints → copy positions + recomputed normals to GPU via `glBufferSubData`.

**Ragdoll (Phase 8).** `Ragdoll` wraps Jolt native ragdoll + skeleton conversion; `RagdollPreset` defines shapes, masses, `SwingTwistConstraint` limits. Supports full (limp), powered (motors to animation pose), partial (some joints kinematic). Uses `Stabilize()` + `DisableParentChildCollisions()`.

**Grab/carry/throw (Phase 8).** `GrabSystem` + `InteractableComponent` — raycast look-at, invisible kinematic holder, spring distance constraint; configurable mass/throw/hold.

**Destruction (Phase 8).** `Fracture` — Voronoi mesh fracture, seeds biased 60% Gaussian near impact / 40% uniform. `BreakableComponent` supports pre-fracture. `DeformableMesh` — soft impact deformation with spatial falloff.

**Dismemberment (Phase 8).** `DismembermentZones` — per-bone health + cascade. `Dismemberment` — runtime mesh split via bone-weight dominance, boundary triangle split, cap generation.

---

## 12. Animation Extensions (Phases 6–7)

**Facial.** `FacialAnimator` drives blend shapes via emotion presets (happy, sad, angry, surprised, pain) with crossfade. `EyeController` — look-at tracking, procedural blink, pupil dilation.

**Lip sync.** `LipSync` supports pre-processed phoneme tracks (JSON) and real-time amplitude fallback. `VisemeMap` phoneme→viseme. `AudioAnalyzer` — FFT frequency + RMS volume.

**Motion matching (Phase 7).** `MotionDatabase` stores sampled poses with feature vectors (joint pos, vel, trajectory). `KDTree` O(log n) NN search. `MotionMatcher` searches at configurable intervals; `Inertialization` smooths transitions. `TrajectoryPredictor` extrapolates input.

---

## 13. Environment (Phase 5)

`FoliageManager` — instanced foliage, LOD, wind animation. `DensityMap` — paintable grayscale density. `Terrain` — heightmap + LOD + splatmap + `TerrainBrush` sculpting.

---

## 14. Editor (Phase 5)

ImGui WYSIWYG, dockable panels.

- **HierarchyPanel** — scene tree, multi-select, drag-reparent, lock/visibility.
- **InspectorPanel** — component editing + undo/redo.
- **AssetBrowserPanel** — thumbnails.
- **EnvironmentPanel** — foliage paint, terrain sculpt, density edit.
- **ValidationPanel** — scene warnings.
- **ImportDialog** — model import with preview + scale validation.

**Supporting:** `CommandHistory` (Command pattern undo/redo), `EntityActions` (clipboard: copy/paste/duplicate), `RulerTool`, `BrushTool`.

---

## 15. GPU Compute Particles (Phase 6)

`GPUParticleSystem` — compute pipeline Emit → Simulate → Compact → Sort → IndirectDraw. SSBOs for data, atomic counters for alloc, bitonic sort for back-to-front transparency. `GPUParticleEmitter` — composable behaviors (gravity, drag, noise, orbit, vortex, collision). Auto-selects GPU path > 500 particles.

---

## 16. Profiler

`CpuProfiler` — scoped per-frame CPU markers. `GpuTimer` — GL timer queries per pass. `MemoryTracker` — allocation counts. `PerformanceProfiler` — aggregates metrics for the editor panel.

---

## 17. Resource Extensions (Phase 5)

`FileWatcher` — asset-dir change detection + reload callbacks. `AsyncTextureLoader` — background texture load, main-thread GPU upload.

---

## 18. Cloth Collision & Solver (Phase 8)

`ClothMeshCollider` — triangle mesh collision with `BVH`. `ColliderGenerator` — auto-create simplified collision from scene meshes. `SpatialHash` — efficient self-collision. `FabricMaterial` + `FabricDatabase` — physically-based presets (silk, cotton, leather, …) with KES-inspired parameters.

---

## 19. Scripting (Phase 9E — `engine/scripting/`)

Visual graph-based gameplay logic. Domain `ISystem`. See `docs/PHASE9E_DESIGN.md` for full rationale + anti-patterns.

**Data model.**
- `ScriptGraph` — serialized `.vscript` asset (nodes, connections, variables). Immutable at runtime post-load. Validated on load (dangling refs rejected) and size-capped (`MAX_NODES`, `MAX_CONNECTIONS`, `MAX_VARIABLES`, `MAX_STRING_BYTES`).
- `ScriptValue` — type-erased variant: bool, int32, float, string, vec2/3/4, quat, entity-ID. Common currency for data pins.
- `Blackboard` — string-keyed variable store, per-scope cap (`MAX_KEYS`). Six-scope model (Flow/Graph/Entity/Scene/Application/Saved) per Unity's design; currently Flow + Graph fully wired.
- `NodeTypeDescriptor` + `NodeTypeRegistry` — descriptor-driven registration. Each descriptor: input/output pin defs, optional `eventTypeName` for EventBus binding, `execute` lambda. Populated at `initialize()` from six category files (`{core,event,action,pure,flow,latent}_nodes.cpp`).

**Runtime.**
- `ScriptInstance` — per-entity runtime state bound to one graph. Holds per-node `ScriptNodeInstance`, graph-scope blackboard, pending latents, event-subscription IDs. Hot-path caches (`updateNodes`, `outputByNode`, `inputByNode`) built once at init — per-frame lookups O(pins/node).
- `ScriptContext` — short-lived per-impulse record. Pin-pull / pin-trigger API + call-depth + node-count guards against runaway scripts.

**EventBus bridge.** `ScriptingSystem::subscribeEventNodes` walks a graph's event nodes and subscribes each via a templated helper. Captures `ScriptingSystem* + ScriptInstance*`; every callback guarded with `isInstanceActive()` (liveness lookup against `m_activeInstances`) — cannot UAF even if instance destroyed without matching `unregisterInstance()`.

**Per-frame.** `update()` runs two passes: `tickUpdateNodes` (cached `OnUpdate` IDs per active instance) and `tickLatentActions` (single-pass partition into completed + pending, fires completed continuations which may schedule more; O(N)).

**Integration.** Depends only on `core/event_bus`, `core/i_system`, scene/entity lookups. Does not own/mutate engine state directly — publishes back via `EventBus` (`PublishEvent` node) or entity API via `ScriptContext::resolveEntity`. Node categories are extensible without core changes.

**Editor (Phase 9E-3).** `engine/editor/widgets/node_editor_widget.{h,cpp}` (thin wrapper over `imgui-node-editor` by thedmd, pulled via `external/CMakeLists.txt`) + `engine/editor/panels/script_editor_panel.{h,cpp}` (dockable host, New/Open/Save menu). Widget-vs-panel split per §14 convention. Steps 1–3 shipped (lib integration, audit-debt items M9/M10/M11 + L6); Step 4 WIP. Remaining: palette, property editors, type-aware pin-drag popup, variables panel, breakpoint UI, flow animation, close-out audit. See `docs/PHASE9E3_DESIGN.md`.

**Undo/redo.** Editor edits flow through `CommandHistory` (§14) via `Script{AddNode,RemoveNode,AddConnection,RemoveConnection,SetProperty}Command` — consistent with every other editor operation, and automatically undo-complete for future AI-authored changes (Phase 23).

**Hot reload.** Saving `.vscript` while scene is live re-parses graph, rebinds runtime; variable state persists; pending latents are dropped with a log note. No rebuild.
