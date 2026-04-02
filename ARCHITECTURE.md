# Vestige Engine Architecture

This document describes the overall architecture of the Vestige 3D Engine.

---

## 1. High-Level Overview

Vestige uses a **Subsystem + Event Bus** architecture. The engine is composed of independent subsystems that are orchestrated by a central Engine class. Subsystems communicate through a shared Event Bus, keeping them decoupled from each other.

```
┌─────────────────────────────────────────────────────┐
│                      Engine                         │
│                                                     │
│  ┌─────────┐  ┌──────────┐  ┌───────────────────┐  │
│  │  Timer   │  │  Logger  │  │   ResourceManager │  │
│  └─────────┘  └──────────┘  └───────────────────┘  │
│                                                     │
│  ┌─────────┐  ┌──────────┐  ┌───────────────────┐  │
│  │ Window  │  │ Renderer │  │   SceneManager    │  │
│  └─────────┘  └──────────┘  └───────────────────┘  │
│                                                     │
│  ┌──────────────┐  ┌────────────────────────────┐   │
│  │ InputManager │  │         EventBus           │   │
│  └──────────────┘  └────────────────────────────┘   │
│                                                     │
└─────────────────────────────────────────────────────┘
```

---

## 2. Engine Loop

The engine runs a fixed main loop that updates each subsystem in a specific order every frame:

```
1. Timer          → Calculate delta time
2. Window         → Poll OS events (GLFW)
3. InputManager   → Process input state (keyboard, mouse, gamepad)
4. EventBus       → Dispatch queued events to listeners
5. SceneManager   → Update active scene (entities, components, logic)
6. Renderer       → Render the active scene to the screen
7. Window         → Swap front/back buffers
```

### Frame Timing
```
while (engine is running)
{
    float deltaTime = timer.update();

    window.pollEvents();
    inputManager.update();
    eventBus.dispatchAll();
    sceneManager.update(deltaTime);
    renderer.render(sceneManager.getActiveScene());
    window.swapBuffers();
}
```

---

## 3. Subsystems

Each subsystem is a self-contained module with a clear responsibility. Subsystems do not directly reference each other — they communicate through the Event Bus.

### 3.1 Engine (`core/engine`)
- **Role:** Owns and orchestrates all subsystems
- **Responsibilities:** Initialize/shutdown subsystems in order, run the main loop
- **Owns:** All other subsystem instances

### 3.2 Window (`core/window`)
- **Role:** Manage the OS window and OpenGL context
- **Responsibilities:** Create/destroy window, handle resize, poll OS events, swap buffers
- **Library:** GLFW
- **Events emitted:** `WindowResize`, `WindowClose`

### 3.3 Timer (`core/timer`)
- **Role:** Track frame timing
- **Responsibilities:** Calculate delta time, track FPS, provide elapsed time
- **Events emitted:** None (queried directly by Engine)

### 3.4 Logger (`core/logger`)
- **Role:** Centralized logging
- **Responsibilities:** Log messages at different severity levels (Trace, Debug, Info, Warning, Error, Fatal)
- **Output:** Console (and optionally file)

### 3.5 InputManager (`core/input_manager`)
- **Role:** Abstract raw input into a queryable state
- **Responsibilities:** Track key/button states, mouse position/delta, gamepad axes/buttons
- **Events emitted:** `KeyPressed`, `KeyReleased`, `MouseMoved`, `MouseButtonPressed`, `GamepadConnected`, `GamepadDisconnected`
- **Supports:** Keyboard, mouse, Xbox controllers, PlayStation controllers (via GLFW gamepad API)

### 3.6 EventBus (`core/event_bus`)
- **Role:** Decoupled communication between subsystems
- **Responsibilities:** Register listeners for event types, queue events, dispatch events to listeners
- **Design:** Synchronous dispatch — events are processed in order during the dispatch phase
- **Pattern:** Publish/Subscribe with typed events

### 3.7 Renderer (`renderer/renderer`)
- **Role:** All OpenGL rendering
- **Responsibilities:** Initialize OpenGL state, manage render pipeline, draw scenes
- **Owns:** Shader management, render state
- **Events listened:** `WindowResize` (to update viewport)

### 3.8 SceneManager (`scene/scene_manager`)
- **Role:** Manage scenes and the entity hierarchy
- **Responsibilities:** Load/unload scenes, switch active scene, update entities
- **Owns:** Scene instances, entity lifecycle

### 3.9 ResourceManager (`resource/resource_manager`)
- **Role:** Load and cache assets
- **Responsibilities:** Load meshes, textures, shaders, fonts from disk; cache to avoid reloading; reference counting for cleanup
- **Supported formats (planned):** OBJ, glTF (models), PNG/JPG (textures), GLSL (shaders), TTF (fonts)

---

## 4. Event Bus Design

### Event Structure
Events are lightweight structs that inherit from a base `Event` type:

```cpp
// Base event
struct Event
{
    virtual ~Event() = default;
};

// Specific events
struct WindowResizeEvent : public Event
{
    int width;
    int height;
};

struct KeyPressedEvent : public Event
{
    int keyCode;
    bool isRepeat;
};
```

### Registration and Dispatch
```cpp
// Subsystem registers interest in an event type
eventBus.subscribe<WindowResizeEvent>([this](const WindowResizeEvent& event)
{
    updateViewport(event.width, event.height);
});

// Another subsystem fires an event
eventBus.publish(WindowResizeEvent{1920, 1080});
```

### Rules
- Events are dispatched once per frame during the `eventBus.dispatchAll()` phase
- Listeners must not modify the event
- Event handlers should be fast — no heavy work in callbacks

---

## 5. Scene Graph

Scenes contain a hierarchy of Entities. Each Entity can have child Entities and Components.

```
Scene
└── Root Entity (Transform)
    ├── Camera Entity (Transform, Camera)
    ├── Sun Light (Transform, DirectionalLight)
    ├── Tabernacle (Transform)
    │   ├── Outer Court (Transform, MeshRenderer)
    │   ├── Holy Place (Transform)
    │   │   ├── Table of Showbread (Transform, MeshRenderer, Material)
    │   │   ├── Golden Lampstand (Transform, MeshRenderer, Material, PointLight)
    │   │   └── Altar of Incense (Transform, MeshRenderer, Material)
    │   └── Holy of Holies (Transform)
    │       └── Ark of the Covenant (Transform, MeshRenderer, Material)
    └── Ground Plane (Transform, MeshRenderer, Material)
```

### Components
Entities are containers that hold Components. Components provide behavior and data:

| Component | Purpose |
|-----------|---------|
| `Transform` | Position, rotation, scale in 3D space |
| `MeshRenderer` | References a mesh + material for rendering |
| `Camera` | View and projection parameters |
| `DirectionalLight` | Sun-like light with direction |
| `PointLight` | Positional light with falloff |
| `SpotLight` | Cone-shaped light (torches, etc.) |
| `Material` | Surface properties (color, textures, shininess) |

---

## 6. Rendering Pipeline

### Phase 1 (Basic)
```
1. Clear color and depth buffers
2. Calculate view matrix from active Camera
3. Calculate projection matrix (perspective)
4. For each renderable entity:
   a. Calculate model matrix from Transform hierarchy
   b. Bind shader program
   c. Set uniforms (MVP matrices, light data, material properties)
   d. Bind vertex array (VAO)
   e. Issue draw call
5. Swap buffers
```

### Future Phases
- **Shadow pass:** Render depth from light's perspective before main pass
- **Post-processing:** Render to framebuffer, apply screen-space effects
- **Deferred rendering:** Geometry pass → Lighting pass (for many lights)
- **Ray tracing:** Hybrid RT for reflections, ambient occlusion, global illumination

---

## 7. Folder Structure

```
vestige/
├── CMakeLists.txt                  # Root CMake build file
├── CLAUDE.md                       # Project context for Claude Code
├── CODING_STANDARDS.md             # Coding standards document
├── ARCHITECTURE.md                 # This file
├── ROADMAP.md                      # Feature roadmap
├── .gitignore                      # Git ignore rules
│
├── engine/                         # Engine library (builds as static lib)
│   ├── CMakeLists.txt
│   ├── core/
│   │   ├── engine.h
│   │   ├── engine.cpp
│   │   ├── window.h
│   │   ├── window.cpp
│   │   ├── event.h                 # Event base class and common events
│   │   ├── event_bus.h
│   │   ├── event_bus.cpp
│   │   ├── input_manager.h
│   │   ├── input_manager.cpp
│   │   ├── timer.h
│   │   ├── timer.cpp
│   │   ├── logger.h
│   │   └── logger.cpp
│   ├── renderer/
│   │   ├── renderer.h
│   │   ├── renderer.cpp
│   │   ├── shader.h
│   │   ├── shader.cpp
│   │   ├── mesh.h
│   │   ├── mesh.cpp
│   │   ├── texture.h
│   │   ├── texture.cpp
│   │   ├── camera.h
│   │   ├── camera.cpp
│   │   ├── material.h
│   │   ├── material.cpp
│   │   ├── light.h
│   │   └── light.cpp
│   ├── scene/
│   │   ├── scene.h
│   │   ├── scene.cpp
│   │   ├── scene_manager.h
│   │   ├── scene_manager.cpp
│   │   ├── entity.h
│   │   ├── entity.cpp
│   │   ├── component.h
│   │   └── component.cpp
│   ├── resource/
│   │   ├── resource_manager.h
│   │   └── resource_manager.cpp
│   └── utils/
│       ├── file_utils.h
│       └── file_utils.cpp
│
├── app/                            # Application executable
│   ├── CMakeLists.txt
│   └── main.cpp                    # Entry point
│
├── assets/                         # Runtime assets (copied to build output)
│   ├── shaders/                    # GLSL shader files
│   ├── textures/                   # Image files (PNG, JPG)
│   ├── models/                     # 3D model files (OBJ, glTF)
│   └── fonts/                      # Font files (TTF)
│
├── external/                       # Third-party dependencies
│   └── CMakeLists.txt              # Fetches GLFW, GLM, etc.
│
└── tests/                          # Unit tests (Google Test)
    ├── CMakeLists.txt
    ├── test_event_bus.cpp
    ├── test_timer.cpp
    └── ...
```

### Build Output
```
build/                              # Out-of-source build (not in repo)
├── engine/
│   └── libvestige_engine.a         # Static library
├── app/
│   └── vestige                     # Executable
├── tests/
│   └── vestige_tests               # Test executable
└── assets/                         # Copied assets
```

---

## 8. Dependency Flow

Dependencies flow downward only — no circular references:

```
    app (executable)
     │
     ▼
    engine (static library)
     │
     ├── core/        ← depends on nothing (except EventBus for events)
     ├── renderer/    ← depends on core/
     ├── scene/       ← depends on core/, renderer/ (for components)
     ├── resource/    ← depends on core/
     └── utils/       ← depends on nothing
```

### Third-Party Dependencies
| Library | Purpose | License |
|---------|---------|---------|
| GLFW | Window, input, OpenGL context | Zlib (permissive, commercial OK) |
| GLM | Vector/matrix math | MIT (permissive, commercial OK) |
| glad | OpenGL function loader | MIT / Public Domain |
| stb_image | Image loading (PNG, JPG) | MIT / Public Domain |
| Google Test | Unit testing framework | BSD-3 (permissive, commercial OK) |
| Assimp | 3D model loading (Phase 2+) | BSD-3 (permissive, commercial OK) |

All dependencies are compatible with proprietary/commercial use.

---

## 9. Platform Abstraction

The engine targets Linux and Windows. Platform-specific code is isolated:

- **GLFW** handles window/input abstraction (already cross-platform)
- **glad** handles OpenGL function loading (already cross-platform)
- **CMake** handles build system differences
- Any remaining platform-specific code goes in `engine/utils/` with `#ifdef` guards:
  - `VESTIGE_PLATFORM_LINUX`
  - `VESTIGE_PLATFORM_WINDOWS`

---

## 10. Animation Subsystem

The animation system lives in `engine/animation/` and provides skeletal animation, property tweening, inverse kinematics, and morph targets.

### Data Flow

```
glTF file
  → GltfLoader (extracts skeleton, clips, morph targets)
    → Skeleton (joint hierarchy + inverse bind matrices)
    → AnimationClip (channels of keyframes)
    → MorphTargetData (per-vertex displacement deltas)

Per frame:
  AnimationSampler (interpolates keyframes)
    → SkeletonAnimator (drives playback, crossfade, root motion)
      → bone matrices (uploaded to GPU for skinning)
  IK solvers (post-process corrections)
  TweenManager (property animation on entity components)
```

### Key Classes

| Class | File | Purpose |
|-------|------|---------|
| `Skeleton` | `skeleton.h/cpp` | Joint hierarchy, inverse bind matrices, bind pose |
| `AnimationClip` | `animation_clip.h/cpp` | Named collection of animation channels (TRS + weights) |
| `AnimationSampler` | `animation_sampler.h/cpp` | Keyframe interpolation: STEP, LINEAR, CUBICSPLINE |
| `SkeletonAnimator` | `skeleton_animator.h/cpp` | Component that plays clips, handles crossfade blending and root motion |
| `AnimationStateMachine` | `animation_state_machine.h/cpp` | Parameter-driven state graph with transitions |
| `Tween` | `tween.h/cpp` | Property animation with easing, events, and playback modes |
| `TweenManager` | `tween.h/cpp` | Component managing multiple tweens per entity |
| `IK Solvers` | `ik_solver.h/cpp` | Two-bone IK, look-at IK, foot IK |
| `MorphTargetData` | `morph_target.h/cpp` | Blend shape data and CPU blending |
| `Easing` | `easing.h/cpp` | 32 Penner easing functions + cubic bezier curves |

### Skeletal Animation Pipeline

1. **Sampling** — `AnimationSampler` interpolates keyframes per channel using binary search (`findKeyframe`) and the channel's interpolation mode
2. **Blending** — `SkeletonAnimator` supports crossfade between two clips via per-bone lerp/slerp with a time-based blend factor
3. **Hierarchy Walk** — Local transforms (T * R * S) are combined parent-to-child to produce global transforms
4. **Bone Matrices** — Final output = `globalTransform * inverseBindMatrix`, uploaded as a uniform array for GPU skinning
5. **Root Motion** — Optionally extracts the root bone's horizontal delta and applies it to the entity's transform

### State Machine

The `AnimationStateMachine` associates named states with clip indices. Transitions fire when parameter-based conditions (float comparisons, bool/trigger checks) are satisfied. Exit time constraints prevent transitions mid-animation. Trigger parameters auto-reset after consumption.

### Inverse Kinematics

Three solvers run as post-processes after the animation pipeline:
- **Two-Bone IK** — Analytic solution using law of cosines with pole vector alignment
- **Look-At IK** — Single-joint aim constraint with angle clamping
- **Foot IK** — Composes two-bone leg IK with ankle-to-ground alignment and pelvis offset

All solvers support weight blending via NLerp between identity and correction quaternions.

---

## 11. Physics Subsystem

The physics system lives in `engine/physics/` and provides rigid-body dynamics via Jolt Physics, a character controller, cloth simulation, and constraint joints.

### Data Flow

```
Entity
  → RigidBody.syncToEntity() copies Jolt transforms → Entity.transform
  → ClothComponent.update() runs ClothSimulator
    → copies positions/normals → uploads to DynamicMesh GPU buffer
```

### Key Classes

| Class | File | Purpose |
|-------|------|---------|
| `PhysicsWorld` | `physics_world.h/cpp` | Jolt Physics integration wrapper. Fixed timestep accumulator (60 Hz). Creates/destroys bodies, manages constraints, performs raycasts. Singleton per scene. |
| `RigidBody` | `rigid_body.h/cpp` | Component attaching physics to entities. Supports STATIC, DYNAMIC, KINEMATIC motion types with BOX, SPHERE, CAPSULE collision shapes. Syncs Jolt body transforms back to entity transforms each frame. |
| `PhysicsCharacterController` | `physics_character_controller.h/cpp` | Jolt CharacterVirtual wrapper for player movement. Supports walk/fly modes, stair stepping, slope limits, ground detection. |
| `ClothSimulator` | `cloth_simulator.h/cpp` | Pure CPU XPBD (Extended Position-Based Dynamics) cloth solver. Generates rectangular particle grid with structural, shear, and bending distance constraints. Supports pin constraints, sphere/plane/cylinder/box colliders, wind with gust state machine, sleep detection, LRA (Long Range Attachment) tethers. |
| `ClothComponent` | `cloth_component.h/cpp` | Links ClothSimulator to the entity/rendering system via DynamicMesh. Fixed 60 Hz timestep accumulator. Fabric presets (linen, tent, banner, drape, fence). |
| `ClothPresets` | `cloth_presets.h/cpp` | Factory for preset cloth configurations (5 fabric types with calibrated physics parameters). |
| `PhysicsConstraint` | `physics_constraint.h/cpp` | Jolt constraint wrapper supporting hinge, fixed, slider, spring, and distance joints with breakable force thresholds. |

### Rigid-Body Pipeline

1. **Accumulation** -- `PhysicsWorld` accumulates frame delta time and steps the Jolt simulation at a fixed 60 Hz rate
2. **Body Sync** -- After each step, `RigidBody.syncToEntity()` reads position and rotation from the Jolt body and writes them to the entity's `Transform` component
3. **Raycasts** -- `PhysicsWorld` exposes raycast queries against the Jolt broadphase for picking, ground detection, and gameplay logic

### Cloth Pipeline

1. **Timestep** -- `ClothComponent` accumulates delta time and steps `ClothSimulator` at a fixed 60 Hz rate
2. **XPBD Solve** -- Each step runs substeps of: apply external forces (gravity, wind), solve distance constraints (structural/shear/bending), apply LRA tethers, resolve collisions (sphere/plane/cylinder/box), enforce pin constraints
3. **Upload** -- After simulation, particle positions and recomputed normals are copied into the `DynamicMesh` vertex buffer via `glBufferSubData`

### Character Controller

`PhysicsCharacterController` wraps Jolt's `CharacterVirtual` to provide player movement with:
- Walk and fly movement modes
- Automatic stair stepping
- Configurable slope angle limits
- Ground contact detection for gravity and jumping

### Ragdoll System (Phase 8)

`Ragdoll` wraps Jolt's native ragdoll with Vestige skeleton conversion. `RagdollPreset` defines per-joint shapes, masses, and `SwingTwistConstraint` limits. Supports full ragdoll (limp), powered ragdoll (motors drive toward animation pose), and partial ragdoll (some joints kinematic, others dynamic). Uses `Stabilize()` and `DisableParentChildCollisions()` for stability.

### Object Interaction (Phase 8)

`GrabSystem` provides first-person grab/carry/throw. Uses raycast look-at detection, an invisible kinematic holder body, and a spring-based distance constraint for smooth carrying. `InteractableComponent` marks entities as grabbable with configurable mass limits, throw force, and hold distance.

### Dynamic Destruction (Phase 8)

`Fracture` implements Voronoi-based mesh fracture. Seed points are biased toward the impact location (60% Gaussian near impact, 40% uniform). `BreakableComponent` supports pre-fracture (compute fragments offline, activate on impact). `DeformableMesh` provides soft impact deformation with smooth spatial falloff.

### Dismemberment (Phase 8)

`DismembermentZones` manages per-bone health zones with damage-driven severing and child cascade. `Dismemberment` performs runtime mesh splitting by classifying vertices via bone weight dominance, splitting boundary triangles, and generating cap geometry.

---

## 12. Animation Subsystem (Phases 6-7)

### Facial Animation

`FacialAnimator` drives blend shape weights via emotion presets (happy, sad, angry, surprised, pain) with crossfade transitions. Uses the existing morph target pipeline (`SkeletonAnimator::setMorphWeight()`). `EyeController` handles look-at tracking, procedural blinking, and pupil dilation.

### Audio-Driven Lip Sync

`LipSync` supports two modes: pre-processed phoneme tracks (JSON) and real-time amplitude fallback. `VisemeMap` maps phonemes to viseme blend shapes. `AudioAnalyzer` provides FFT-based frequency analysis and RMS volume detection.

### Motion Matching (Phase 7)

`MotionDatabase` stores sampled animation poses with feature vectors (joint positions, velocities, trajectory). `KDTree` provides O(log n) nearest-neighbor search. `MotionMatcher` performs per-frame search at configurable intervals with `Inertialization` for smooth transitions. `TrajectoryPredictor` extrapolates player intent from input.

---

## 13. Environment Subsystem (Phase 5)

`FoliageManager` handles instanced foliage rendering with LOD and wind animation. `DensityMap` provides a paintable grayscale texture controlling foliage placement density. `Terrain` supports heightmap-based terrain with LOD, splatmap texturing, and sculpting via `TerrainBrush`.

---

## 14. Editor Subsystem (Phase 5)

The editor is an ImGui-based WYSIWYG interface with dockable panels:

- **HierarchyPanel** — scene tree with multi-select, drag reparenting, lock/visibility toggles
- **InspectorPanel** — component property editing with undo/redo
- **AssetBrowserPanel** — filesystem browser with thumbnail previews
- **EnvironmentPanel** — foliage painting, terrain sculpting, density map editing
- **ValidationPanel** — scene validation warnings
- **ImportDialog** — model import with preview and scale validation

`CommandHistory` provides undo/redo via the Command pattern. `EntityActions` provides clipboard operations (copy/paste/duplicate). `RulerTool` provides measurement overlay. `BrushTool` handles environment painting with configurable radius and fallback.

---

## 15. GPU Compute Particles (Phase 6)

`GPUParticleSystem` manages a compute shader pipeline: Emit → Simulate → Compact → Sort → IndirectDraw. Uses SSBOs for particle data, atomic counters for allocation, and bitonic sort for back-to-front transparency. `GPUParticleEmitter` provides a composable behavior system (gravity, drag, noise, orbit, vortex, collision). Auto-selects GPU path for emitters with >500 particles.

---

## 16. Profiler Subsystem

`CpuProfiler` tracks per-frame CPU timing with scoped markers. `GpuTimer` uses OpenGL timer queries for per-pass GPU timing. `MemoryTracker` monitors allocation counts. `PerformanceProfiler` aggregates all metrics for the editor performance panel.

---

## 17. Resource Subsystem Extensions (Phase 5)

`FileWatcher` monitors asset directories for changes and triggers reload callbacks. `AsyncTextureLoader` handles background texture loading with GPU upload on the main thread.

---

## 18. Cloth Collision & Solver (Phase 8)

`ClothMeshCollider` provides triangle mesh collision with a `BVH` acceleration structure. `ColliderGenerator` automatically creates simplified collision geometry from scene meshes. `SpatialHash` enables efficient self-collision detection. `FabricMaterial` and `FabricDatabase` provide physically-based material presets (silk, cotton, leather, etc.) with KES-inspired parameters.
