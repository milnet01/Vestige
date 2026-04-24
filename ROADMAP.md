# Vestige Engine Roadmap

This document outlines the phased development plan for the Vestige 3D Engine.

---

## Phase 1: Foundation (COMPLETE)
**Goal:** Open a window, render a 3D shape, move a camera around it.

This phase establishes the project skeleton, build system, and core engine loop.

### Features
- [x] Project setup (CMake, dependency fetching, .gitignore)
- [x] GLFW window creation and OpenGL 4.5 context
- [x] glad OpenGL function loader
- [x] Basic engine loop (init → loop → shutdown)
- [x] Timer (delta time, FPS tracking)
- [x] Logger (console output with severity levels)
- [x] Shader loading and compilation (vertex + fragment)
- [x] Render a colored triangle (hello world of 3D graphics)
- [x] Render a 3D cube with perspective projection
- [x] Basic camera (position, look direction, perspective)
- [x] Keyboard input (WASD movement, mouse look)
- [x] Event Bus (basic publish/subscribe)
- [x] Window resize handling
- [x] Basic unit test infrastructure (Google Test)

### Milestone
~~A window showing a colored 3D cube that the user can fly around using keyboard and mouse.~~ DONE

---

## Phase 2: 3D World (COMPLETE)
**Goal:** Load 3D models, apply textures, and light the scene.

### Features
- [x] Mesh class (vertex buffer, index buffer, vertex array)
- [x] ~~OBJ model loading~~ Superseded by glTF loading (Phase 4) — glTF is the modern standard
- [x] Texture loading (PNG/JPG via stb_image)
- [x] UV mapping and texture coordinates
- [x] Basic materials (diffuse color, specular, shininess)
- [x] Blinn-Phong lighting model
- [x] Directional light (sun)
- [x] Point lights (lamps, candles)
- [x] Spot lights (focused beams)
- [x] Multiple objects in a scene
- [x] Depth testing and face culling
- [x] Wireframe debug rendering mode

### Milestone
~~A lit scene with multiple textured 3D models (e.g., a simple room with objects).~~ DONE

---

## Phase 3: Scene Management (COMPLETE)
**Goal:** Organize the world into a scene graph with entities and components.

### Features
- [x] Entity class with component system
- [x] Transform component (position, rotation, scale, parent-child hierarchy)
- [x] MeshRenderer component
- [x] Camera component (CameraComponent — entity-based, supports perspective/orthographic, scene tracks active camera)
- [x] Light components (Directional, Point, Spot)
- [x] Scene class (holds entity hierarchy)
- [x] SceneManager (load, unload, switch scenes)
- [x] Resource manager (caching loaded assets)
- [x] Gamepad/controller input (Xbox, PlayStation via GLFW)
- [x] First-person character controller (walking, looking, collision with ground)
- [x] Basic bounding-box collision detection (prevent walking through walls)

### Milestone
~~Walk through a multi-room environment in first person with controller support.~~ DONE

---

## Phase 4: Visual Quality (COMPLETE)
**Goal:** Make it look good — shadows, better materials, post-processing.

### Features
- [x] Shadow mapping (directional light shadows)
- [x] Point light shadows (omnidirectional shadow maps)
- [x] Normal mapping (surface detail without extra geometry)
- [x] Parallax occlusion mapping (depth illusion via height map ray-marching)
- [x] PBR materials (Physically Based Rendering — metallic/roughness workflow)
- [x] Environment mapping / skybox (procedural gradient)
- [x] Image-Based Lighting / IBL (HDRI environment maps for realistic ambient lighting — diffuse irradiance convolution, prefiltered specular mip chain, BRDF integration LUT; completes PBR pipeline with environment-driven reflections and ambient light)
- [x] HDR rendering and tone mapping (Reinhard + ACES Filmic)
- [x] 3D text rendering (FreeType glyph atlas, text-on-surface decals, embossed/engraved inscriptions)
- [x] Emissive lighting (materials that emit light into the scene — fire glow, neon signs, lava, glowing runes; emissive surfaces contribute to scene illumination via bloom and indirect light propagation; scalable from simple bloom-coupled emissives to many-light systems for scenes with hundreds of emissive sources)
- [x] Bloom post-processing effect
- [x] MSAA anti-aliasing (4x)
- [x] TAA (Temporal Anti-Aliasing — motion vectors, history buffer, jittered projection)
- [x] glTF model loading (modern, full-featured format)
- [x] Ambient occlusion (SSAO)
- [x] Transparency / alpha blending (glass, coloured glass, frosted glass, linen curtains)
- [x] Transparent object sorting (back-to-front draw order)
- [x] Asynchronous texture loading (background thread loading with GPU upload on main thread)
- [x] Instanced rendering (draw thousands of identical objects — pillars, columns, courtyard stones — in a single draw call)
- [x] Cascaded shadow maps (multi-split directional shadows for large outdoor scenes like Temple courtyards)
- [x] Color grading / LUT (look-up table color transformation for artistic control — warm golden interiors, cool blue night exteriors)

### Milestone
A visually polished scene with realistic lighting, shadows, materials, and transparency.

---

## Phase 5: Scene Editor — The Creator's Toolkit (COMPLETE)
**Goal:** Build a full graphical editor so scenes, materials, effects, and gameplay can be created entirely without writing code. This is the primary tool the user will use day-to-day.

The editor is not just a debug tool — it IS the way scenes get built. Every engine feature must ultimately be accessible through the editor. This phase is broken into sub-phases that can be developed incrementally.

---

### Phase 5A: Editor Foundation (COMPLETE)
**Goal:** Get the basic editor shell running — panels, viewport, and the ability to interact with the scene visually.

#### GUI Framework
- [x] Dear ImGui integration (immediate-mode GUI, renders on top of the 3D viewport)
- [x] ImGui GLFW + OpenGL backend setup
- [x] Docking system (drag panels to arrange your workspace — imgui docking branch)
- [x] Editor theme (dark theme, readable fonts, consistent styling)
- [x] Editor/Play mode toggle (Escape or a toolbar button switches between editing and first-person walkthrough)

#### Metric Scale System
- [x] Define 1 engine unit = 1 meter (enforce everywhere)
- [x] 3D grid overlay on the ground plane (1m lines, 10m bold lines)
- [x] Grid snapping — objects snap to 0.25m / 0.5m / 1m increments (configurable)
- [x] Ruler/measurement tool — click two points to see distance in meters
- [x] Dimension display — selected objects show their width/height/depth in meters
- [x] Room dimension input — type "10m x 20m x 2.5m" to create a room of that size

#### Editor Camera
- [x] Orbit camera (Alt+drag to orbit around a point, scroll to zoom)
- [x] Pan camera (middle-mouse drag to pan)
- [x] Focus on selection (F key snaps camera to look at the selected entity)
- [x] Top/Front/Side orthographic views (numpad shortcuts)
- [x] Smooth camera transitions between views

#### Selection System
- [x] Mouse picking — click on an object in the viewport to select it
- [x] Selection highlighting (outline or tint on selected object)
- [x] Multi-selection (Shift+click to add, Ctrl+click to toggle)
- [x] Selection rectangle (drag to box-select multiple objects)

#### Transform Gizmos
- [x] Translate gizmo (RGB arrows for X/Y/Z, click-drag to move)
- [x] Rotate gizmo (rings around the object, drag to rotate)
- [x] Scale gizmo (cubes on axes, drag to scale)
- [x] W/E/R hotkeys to switch between translate/rotate/scale
- [x] Local vs World space toggle for gizmos
- [x] Snap-to-grid for gizmos (hold Ctrl to snap while dragging)

---

### Phase 5B: Scene Construction (COMPLETE)
**Goal:** Place and arrange objects to build rooms, walls, floors, and architectural structures.

#### Primitive Placement
- [x] Toolbar palette for placing basic shapes: cube, plane, cylinder, sphere, wedge/ramp
- [x] Click-to-place — click in the viewport or grid to drop a shape at that position
- [x] Shapes spawn at metric sizes (e.g., cube = 1m x 1m x 1m by default)
- [x] Dimension handles — drag edges/faces of a placed shape to resize it

#### Room/Wall Builder
- [x] Wall tool — click two points on the grid to draw a wall segment (specify height + thickness)
- [x] Room tool — click four corners (or specify dimensions) to generate a room with walls, floor, and ceiling
- [x] Wall thickness setting (default 0.2m for interior, 0.3m for exterior)
- [x] Door cutout tool — click on a wall to cut a door-sized opening
- [x] Window cutout tool — click on a wall to cut a window opening
- [x] Roof tool — select walls and choose roof type (flat, gabled, shed)
- [x] Stair/ramp tool — specify start height, end height, and width

#### Model Import
- [x] glTF / OBJ import dialog — browse and import 3D models into the asset library
- [x] Import preview — see the model before committing to import
- [x] Automatic material/texture extraction from imported models
- [x] Place imported models into the scene from the asset browser
- [x] Scale-on-import — ensure imported models match metric scale (1 unit = 1 meter)

#### Object Management
- [x] Duplicate objects (Ctrl+D)
- [x] Delete objects (Delete key)
- [x] Group objects (select multiple, group into a named parent entity)
- [x] Lock objects (prevent accidental selection/modification)
- [x] Hide/show objects (eye icon in hierarchy — hidden objects don't render)
- [x] Copy/Paste transforms between objects
- [x] Align tools (align selected objects by edge, center, or distribute evenly)

#### Prefab System
- [x] Save any entity (or group) as a reusable prefab asset (e.g., "Golden Lampstand")
- [x] Place prefab instances in any scene — drag from the asset browser
- [x] Edit the master prefab and have all instances update automatically
- [x] Override individual instance properties (e.g., different scale on one copy)
- [x] Prefab library browser in the asset panel

#### Scene Hierarchy Panel
- [x] Tree view of all entities in the scene
- [x] Drag to reparent entities (make one a child of another)
- [x] Right-click context menu (rename, duplicate, delete, add child)
- [x] Search/filter entities by name
- [x] Icons to distinguish entity types (mesh, light, camera, empty)

---

### Phase 5C: Materials, Textures, and Lighting (COMPLETE)
**Goal:** Assign and tweak the look of every surface, and place lights, all through the editor.

#### Material Editor Panel
- [x] Visual material editor — sliders for diffuse color, specular color, shininess
- [x] Texture slot assignment — drag a texture from the asset browser onto a material slot
- [x] Live preview — changes update in the viewport immediately
- [x] Material library — save/load reusable materials (e.g., "Gold", "Acacia Wood", "Linen")
- [x] UV tiling controls — adjust how many times a texture repeats per meter
- [x] POM height scale slider (when height maps are assigned)
- [x] PBR material slots (when PBR is implemented): albedo, normal, roughness, metallic, AO, emissive

#### Texture Management
- [x] Asset browser panel — shows all textures in `assets/textures/` with thumbnail previews
- [x] Drag-and-drop textures onto objects in the viewport to assign them
- [x] Texture import — drag external image files into the asset browser to import
- [x] Texture preview — click a texture to see it full-size with metadata (resolution, format, size)
- [x] Texture filtering options (nearest, linear, anisotropic) per texture

#### Entity Inspector Panel
- [x] Shows all components on the selected entity
- [x] Edit any property — transform, material, light settings, etc.
- [x] "Add Component" button — attach new components to an entity
- [x] "Remove Component" button
- [x] Component-specific sub-panels (material expands to show all material properties, etc.)

#### Light Placement and Editing
- [x] Place lights from the toolbar (directional, point, spot)
- [x] Visual light indicators in the viewport (icons, range spheres, cone wireframes)
- [x] Light property editing in the inspector (color picker, intensity, range, shadow toggle)
- [x] Directional light visualized as an arrow showing direction
- [x] Point light visualized as a sphere showing range
- [x] Spot light visualized as a cone showing angle and range

---

### Phase 5D: Scene Persistence (COMPLETE)
**Goal:** Save and load scenes so work persists between sessions. This is essential — without it, every scene is lost when the editor closes.

#### Scene Serialization
- [x] Save scene to JSON file (human-readable, version-controlled)
- [x] Load scene from JSON file
- [x] Auto-save (periodic backup while editing)
- [x] Scene metadata (name, author, description, creation date)
- [x] All entity data serialized: transforms, components, material references, light settings
- [x] Asset references by path (textures, meshes — not embedded in scene file)

#### Undo/Redo System
- [x] Command pattern — every editor action is a reversible command
- [x] Ctrl+Z to undo, Ctrl+Shift+Z (or Ctrl+Y) to redo
- [x] Undo history panel showing recent actions
- [x] Essential for a non-destructive workflow — mistakes can be reverted

#### Project Management
- [x] Project file that tracks all scenes, assets, and settings
- [x] "New Scene" / "Open Scene" / "Save Scene" / "Save As" from the File menu
- [x] Recent files list
- [x] Asset directory monitoring — detect when new textures/models are added to `assets/`

---

### Phase 5E: Effects Editors (COMPLETE)
**Goal:** Configure particle effects (fire, smoke, water) through the editor, not code.

#### Particle System Editor
- [x] Place particle emitter entities in the scene
- [x] Visual particle editor — adjust emission rate, lifetime, velocity, size, color over time
- [x] Presets for common effects: torch fire, candle flame, smoke, dust, sparks, embers
- [x] Real-time preview in the viewport while editing parameters
- [x] Save/load particle presets as reusable assets

#### Water Body Editor
- [x] Place water surface entities (rectangular water plane)
- [x] Set water dimensions and elevation in meters
- [x] Water property editor: color tint, opacity, reflection strength, wave speed/amplitude
- [x] Presets: still bath, gentle pool, flowing stream

---

### Phase 5F: Editor Utilities (COMPLETE)
**Goal:** Quality-of-life tools that make the editor efficient and informative.

#### Performance Overlay
- [x] FPS counter (always visible in corner)
- [x] Frame time graph (rolling chart of frame times)
- [x] Draw call count, triangle count, texture memory usage
- [x] Per-object stats (vertex count on selected object)

#### Console / Log Panel
- [x] In-editor log viewer (scrollable, filterable by severity)
- [x] Color-coded messages (info=white, warning=yellow, error=red)
- [x] Search/filter log messages

#### Additional Tools
- [x] Screenshot capture (F12 or menu — saves to a screenshots folder)
- [x] Fullscreen viewport toggle (hide all panels for a clean view)
- [x] Keyboard shortcut reference panel (searchable list of all hotkeys)
- [x] Welcome screen / getting started tutorial on first launch
- [x] Scene statistics panel (total entities, meshes, textures, lights, memory usage)
- [x] Validation warnings (e.g., "Light has no shadow map", "Texture missing", "Object outside scene bounds")

---

### Phase 5G: Environment Painting (COMPLETE)
**Goal:** Paint natural environments directly onto surfaces — grass, gravel, trees, paths, streams — using an intuitive brush-based system. This is the primary tool for building outdoor scenes around the Temple complex.

#### Foliage Brush
- [x] Grass rendering system (instanced billboards or geometry shader, leveraging existing instanced rendering)
- [x] Foliage brush tool — paint grass/flowers/low plants onto any surface with adjustable radius and density
- [x] Density falloff (full density at brush center, tapering at edges for natural blending)
- [x] Random rotation, scale, and tint variation per instance (configurable ranges)
- [x] Wind animation (vertex shader sway — amplitude, frequency, direction as brush parameters)
- [x] Foliage presets: short grass, tall grass, wildflowers, reeds, desert scrub
- [x] Density map layer — a per-surface texture controlling where foliage can grow (paintable in-editor)

#### Scatter Brush
- [x] Scatter brush tool — paint rocks, gravel, debris, fallen leaves onto surfaces
- [x] Object palette — select which meshes to scatter (small rocks, pebbles, pottery shards)
- [x] Random placement within brush radius with configurable spacing and overlap rules
- [x] Surface alignment — scattered objects orient to the surface normal (rocks sit flat on slopes)
- [x] Scale/rotation randomization ranges per object type
- [x] Eraser mode — remove scattered objects within brush radius

#### Tree and Large Object Placement
- [x] Tree brush — paint clusters of trees with species selection and spacing rules
- [x] Single-place mode — click to place one tree precisely, then adjust with transform gizmos
- [x] Minimum spacing enforcement (trees don't overlap trunks)
- [x] Tree species presets: olive, cedar, palm, acacia (with placeholder meshes until real models are imported)
- [x] LOD system for trees (full mesh up close, billboard at distance — leverages instanced rendering)

#### Path and Road Tool
- [x] Spline-based path drawing — click waypoints, engine generates a smooth path between them
- [x] Path width and material selection (gravel, dirt, stone pavers, sand)
- [x] Terrain texture blending along path edges (smooth transition from path to surrounding ground)
- [x] Automatic foliage clearing — foliage within the path footprint is removed
- [x] Path presets: narrow footpath, wide road, stone walkway

#### Water Painting
- [x] Stream/river spline tool — draw a path, engine generates a flowing water surface mesh
- [x] Stream width and depth controls per waypoint (narrow creek to wide river)
- [x] Flow direction and speed (derived from spline direction)
- [x] Pool/pond tool — draw a closed shape to create a still water body
- [x] Water material integration (reflective/refractive shader from Phase 6, or basic placeholder)
- [x] Automatic bank blending — terrain textures transition to wet sand/mud near water edges

#### Biome Presets
- [x] Biome system — named combinations of ground texture + foliage + scatter + trees
- [x] Built-in presets: "Garden" (green grass, flowers, olive trees), "Desert" (sand, scrub, rocks), "Temple Courtyard" (stone pavers, sparse grass at edges), "Cedar Forest" (forest floor, ferns, cedar trees)
- [x] Paint entire areas with a biome brush — applies all layers at once
- [x] Per-layer override — paint a biome then selectively adjust individual layers
- [x] Save/load custom biome presets

#### Performance
- [x] Frustum culling for foliage instances (don't submit off-screen grass to the GPU)
- [x] Distance-based density fade (reduce foliage density at distance, fade out beyond a threshold)
- [x] Chunk-based spatial partitioning (group nearby foliage into spatial cells for efficient culling)
- [x] Target: 60 FPS with dense grass fields covering the Temple courtyard (~100k instances visible)

---

### Phase 5 Milestone
~~**A complete scene editor where architectural environments can be designed, textured, lit, and saved entirely through the GUI.** The Tabernacle and Solomon's Temple can be built in-editor by a non-programmer. Outdoor areas around the Temple can be painted with grass, trees, paths, and water using the environment painting tools.~~ DONE

---

## Phase 6: Particle and Effects System (COMPLETE)
**Goal:** Fire, smoke, water, and other real-time visual effects.

These engine features are prerequisites for the editor's effects tools (Phase 5E) but can be developed alongside the early editor phases.

### Fire and Flame
- [x] GPU particle system (compute shader driven for thousands of particles at 60 FPS)
- [x] Billboard particle rendering (camera-facing quads with alpha blending)
- [x] Fire shader (animated color gradient: white core → yellow → orange → red → smoke)
- [x] Torch fire preset (upward emission with flickering, ember sparks)
- [x] Candle flame preset (small, gentle, mostly yellow)
- [x] Campfire preset (larger, with smoke and floating embers)
- [x] Light coupling — fire emitters automatically create a flickering point light

### Smoke and Atmosphere
- [x] Smoke particles (grey, slow-rising, expanding, fading)
- [x] Dust motes (tiny, slow, ambient particles in indoor spaces)
- [x] Incense smoke (thin, wispy, rising column — perfect for the Tabernacle)

### Water
- [x] Water surface shader (reflective/refractive with Fresnel effect)
- [x] Animated water normals (scrolling normal maps for wave appearance)
- [x] Water bath/pool (contained rectangular body — no terrain needed)
- [x] Depth-based color (water darkens with depth)
- [x] Water caustics (animated light patterns projected onto surfaces below water — scrolling noise texture modulates light intensity, simulating refraction through waves)
- [x] Optional: simple wave vertex displacement

### Milestone
~~Torches with flickering fire illuminate a room, smoke rises from an altar of incense, and a bronze laver contains reflective water.~~ DONE

---

## Phase 7: Animation (COMPLETE)
**Goal:** Bring the world to life with skeletal animation and motion.

Animated objects and characters are essential for doors, swinging censers, priestly processions, and any living scene. glTF already carries skeletal animation data — this phase makes the engine able to play it.

### Skeletal Animation System
- [x] Bone/joint hierarchy (skeleton loaded from glTF) — Phase 7A
- [x] Skinned mesh rendering (vertex skinning with bone weights in the vertex shader) — Phase 7A
- [x] Animation clip playback (keyframe interpolation — position, rotation, scale) — Phase 7A
- [x] Animation blending (cross-fade between clips, layered blending) — Phase 7B
- [x] Animation state machine (idle → walk → run transitions with conditions) — Phase 7B
- [x] Root motion support (animation drives entity movement, not just visual) — Phase 7B

### Object Animation
- [x] Property animation system (animate any numeric property over time — position, rotation, color, intensity) — Phase 7C
- [x] Animation curves (linear, ease-in/out, cubic bezier) — Phase 7C
- [x] Looping, ping-pong, and one-shot playback modes — Phase 7C
- [x] Animation events (trigger callbacks at specific keyframes — play sound, spawn particles) — Phase 7C

### Inverse Kinematics (IK)
- [x] Two-bone IK solver (arms, legs — reach for a target position) — Phase 7D
- [x] Foot IK (plant feet correctly on slopes, stairs, and uneven terrain) — Phase 7D
- [x] Hand IK (reach for door handles, grab objects, brace against walls) — Phase 7D (uses two-bone IK)
- [x] Look-at / head IK (characters turn head toward points of interest) — Phase 7D
- [x] IK blending with animation clips (IK layer on top of baked animations) — Phase 7D (weight parameter)

### Facial Animation and Lip Sync
- [x] GPU morph target deformation pipeline — Phase 7F
  - Morph target deltas uploaded to SSBO (binding 3) at model load time
  - Vertex shader applies weighted position+normal deltas before bone skinning (up to 8 targets)
  - WEIGHTS animation channel sampling in SkeletonAnimator (linear + step interpolation)
  - Procedural morph weight API: setMorphWeight(index, value) for game-driven expressions
  - glTF loader updated: WEIGHTS channels no longer skipped for non-joint nodes
  - Mesa-safe dummy SSBO binding for morph target buffer
- [x] Facial blend shape system (emotion presets — happy, sad, angry, surprised, pain)
  - Layer facial animation on top of body animation
  - Smooth blending between expressions with configurable transition speed
- [x] Audio-driven lip sync
  - Phoneme extraction from audio files (viseme mapping)
  - Real-time blend shape weights from phoneme stream
  - Fallback: simple jaw open/close from audio amplitude
- [x] Eye animation
  - Look-at targets (eyes track points of interest, player, or other NPCs)
  - Blink animation (random blink interval, blink on startle)
  - Pupil dilation (optional — fear, darkness adaptation)

### Motion Matching
Data-driven animation selection that replaces hand-authored state machines with continuous pose searching. Used by The Last of Us Part II, For Honor, UE5 (built-in), and modern AAA games for fluid locomotion.
- [x] Motion database — import and index a large set of animation clips with per-frame feature vectors
  - Feature vector: joint positions, joint velocities, trajectory (future path positions + facing)
  - KD-tree or brute-force search over feature vectors for nearest match
  - Clip annotation: tag clips with locomotion type (walk, run, strafe, turn, stop)
- [x] Runtime pose matching — each frame, search the database for the best next pose
  - Current pose features: extract from current skeleton state
  - Desired trajectory: from player input (gamepad stick → desired velocity + facing)
  - Cost function: weighted sum of pose distance + trajectory distance + transition cost
  - Inertialization blending: smooth transition to matched pose without visible pops (Bollo, GDC 2018)
- [x] Motion database preprocessing
  - Offline feature extraction and normalization
  - Clip segmentation: split long mocap takes into indexed segments
  - Mirror generation: auto-generate left/right mirrored clips to double database size
  - Velocity and acceleration caching for fast runtime queries
- [x] Spring-based trajectory prediction
  - Critically damped spring model for desired velocity/facing (responsive to input changes)
  - Predict future trajectory 0.5-1.0s ahead for matching
- [x] Editor integration
  - Motion database browser — view clips, features, and annotations
  - Pose search debugger — visualize matched poses and cost breakdown in real-time
  - Weight tuning UI for cost function parameters
- [x] Performance optimization
  - KD-tree acceleration for large databases (1000+ clips)
  - Feature normalization for balanced distance metrics
  - Budget-based search (limit candidates per frame)
  - Thread-safe: search on worker thread, apply result on main thread

### glTF Animation Import
- [x] Import skeletal animations from glTF files — Phase 7A
- [x] Import morph target / blend shape animations — Phase 7E
- [x] Multiple animation clips per model (walk, idle, gesture) — Phase 7A

### Milestone
~~Animated characters walk through the Temple courts, doors swing open on approach, and a golden censer swings rhythmically over the altar of incense.~~ DONE

---

## Phase 8: Physics (COMPLETE)
**Goal:** Physical simulation for realistic object interaction, cloth, and world dynamics.

Physics enables curtains blowing in the wind, objects responding to gravity, doors with realistic hinges, and the linen fabrics of the Tabernacle draping naturally.

### Rigid Body Dynamics
- [x] Physics engine integration (Jolt Physics v5.2.0) — Phase 8A
- [x] Rigid body component (mass, friction, restitution) — Phase 8A
- [x] Collision shapes: box, sphere, capsule — Phase 8A
- [x] Collision shapes: convex hull, triangle mesh
- [x] Gravity and force application — Phase 8A
- [x] Collision detection and response (bouncing, sliding, resting) — Phase 8A
- [x] Kinematic bodies (scripted movement that still collides — doors, platforms) — Phase 8A
- [x] Physics-based character controller (CharacterVirtual with stair climbing, floor sticking) — Phase 8B

### Constraints and Joints
- [x] Hinge joints (doors, gates, lids — with motor + angle limits) — Phase 8C
- [x] Spring/damper constraints (distance constraint with spring params) — Phase 8C
- [x] Fixed joints (weld objects together) — Phase 8C
- [x] Rope/chain constraints (distance + point constraints) — Phase 8C
- [x] Breakable constraints (force threshold triggers break) — Phase 8C
- [x] Slider constraints (linear movement along axis) — Phase 8C
- [x] Physics debug visualization (wireframe bodies + constraint lines) — Phase 8C
- [x] Raycasting (physics world ray queries) — Phase 8B

### Cloth Simulation
- [x] Cloth component (XPBD position-based dynamics) — Phase 8D
- [x] Wind interaction (cloth responds to wind direction and strength, gust state machine) — Phase 8D
- [x] Pin constraints (attach cloth corners to fixed points — hanging curtains) — Phase 8D
- [x] Presets: linen curtain, tent fabric, banner/flag, heavy drape, stiff fence — Phase 8E
- [x] Primitive colliders: sphere, plane, cylinder, box — Phase 8E
- [x] Editor UI for cloth parameters — Phase 8E
- [x] Collision with rigid bodies (cloth drapes over objects) — see Cloth Collision below

### Cloth Physics Improvements
Improvements to the XPBD cloth solver identified through research and testing.
- [x] Dihedral bending constraints — replace skip-one distance constraints with true angle-based bending
  - Dihedral constraints measure the angle between adjacent triangle pairs and enforce a rest angle
  - Provides genuine "flattening" force that actively straightens cloth (Müller 2007, Jolt Physics)
- [x] Constraint ordering optimization — solve top-to-bottom (from pins downward)
  - Propagates corrections from fixed boundary in a single pass instead of requiring multiple iterations
  - Dramatically improves convergence for hanging cloth topologies (sweep ordering)
- [x] Adaptive damping — increase damping during calm periods for faster settling
  - Ramp damping from 0.02 (wind) to 0.12 (calm) based on gust intensity
  - Prevents oscillation during the return-to-rest phase
- [x] Friction on collider surfaces — static and kinetic friction for stable folds
  - Decompose velocity into normal + tangential after collision correction
  - Static friction zeroes tangential velocity below a threshold (stable folds on surfaces)
  - Kinetic friction reduces tangential velocity proportional to normal impulse
- [x] Thick particle model (marble model) — inflate particle collision radius to cover mesh edges
  - Each particle's collision radius = 0.6-0.8× rest length so adjacent spheres overlap
  - Filter collisions between connected particles

### Physically-Based Fabric Material System
A material-driven cloth system analogous to PBR for rendering — define real-world fabric properties and the simulator derives correct behavior automatically.
- [x] `FabricMaterial` struct with real-world textile properties
  - Areal density (GSM — grams per square meter) → maps to particle mass
  - Tensile stiffness (N/m) → maps to stretch compliance
  - Shear stiffness (N/m) → maps to shear compliance
  - Bending rigidity (N·m) → maps to bend compliance
  - Internal friction (dimensionless) → maps to velocity damping
  - Air permeability (L/m²/s) → maps to drag coefficient
  - Thickness (mm) → maps to collision margin
- [x] KES-to-XPBD mapping function — convert Kawabata Evaluation System measurements to simulation parameters
  - Mapping function converts physical units to compliance values
- [x] Built-in fabric database — ship common fabric types with correct physical properties
  - Linen (ancient hand-woven, 200-350 GSM), cotton (80-150 GSM), silk (20-60 GSM)
  - Goat hair (300-600 GSM), leather (500-1200 GSM), velvet (300-500 GSM)
- [x] Editor integration — select fabric type from dropdown, all parameters auto-filled
  - Override individual properties for custom materials

### Cloth Collision (Hybrid Approach)
A hybrid collision system: fast primitive colliders for simple geometry (walls, pillars, floors) and triangle mesh colliders for complex/irregular geometry (carved decorations, imported models).
- [x] Triangle mesh collider — use actual model triangles as collision surfaces
  - Extract triangle data from loaded meshes at scene build time
  - BVH (Bounding Volume Hierarchy) acceleration structure for fast spatial queries
  - Per-particle: query BVH for nearby triangles, test penetration, push outside
- [x] Automatic collider generation from scene geometry
  - Simple shapes auto-detected: axis-aligned boxes, cylinders, spheres fitted to mesh bounds
  - Complex shapes: auto-build triangle mesh collider from model data
  - Editor toggle per-entity: "None", "Primitive (auto)", "Mesh (exact)", "Custom"
- [x] Jolt Physics integration for mesh collision (optional path)
  - Use Jolt's `MeshShape` for static scene geometry already registered with the physics world
  - Query Jolt's collision system for cloth particle penetration tests
- [x] Edge/triangle collision for cloth mesh (not just particles)
  - Test cloth mesh edges against collider surfaces to prevent pass-through between particles
  - Continuous collision detection (CCD) for fast-moving cloth to prevent tunneling
- [x] Self-collision detection
  - Spatial hashing to find nearby cloth particles on the same mesh
  - Prevent cloth from passing through itself when folding

### Ragdoll Physics
Skeleton-driven ragdoll for realistic death animations, zero-G corpses, and physics-based stumbling.
- [x] Skeleton-to-ragdoll transition (on death or trigger, switch from animation to physics-driven bones)
  - Map skeleton bones to rigid body chain with joint limits per bone
  - Blend from animation to ragdoll (active ragdoll — partial physics influence for stumbling, hit reactions)
  - Preserve final animation pose as ragdoll initial state (no snapping)
- [x] Joint limit configuration per bone (hinge for knees/elbows, cone-twist for shoulders/hips)
- [x] Ragdoll presets (humanoid, quadruped, custom — auto-generate from skeleton)
- [x] Powered ragdoll (muscles — ragdoll tries to maintain a pose while physics acts on it)
  - Use case: enemies stumbling, characters bracing against walls, partial dismemberment

### Object Interaction System
Pick up, hold, throw, and manipulate physics objects in the world.
- [x] Grab system — raycast from camera/hand to select nearby physics objects
  - Distance-limited (2-3m range), weight-limited (can't grab massive objects)
  - Visual indicator (highlight, cursor change) when looking at grabbable objects
- [x] Hold and carry — grabbed object follows a target point with spring constraint
  - Object rotates to face camera or maintains grabbed orientation
  - Collision with world while held (object pushes player away if stuck)
- [x] Throw — release grabbed object with velocity from mouse/stick movement
  - Configurable throw force, arc prediction line (optional)
- [x] Physics puzzles — pressure plate component with overlap detection and activation callbacks
- [x] Stasis/slow-motion on individual objects (per-body freeze/slow-motion via StasisSystem)

### Dynamic Destruction
Breakable objects, pre-fractured meshes, and runtime deformation.
- [x] Pre-fractured mesh system — artist defines fracture pieces at import time
  - Voronoi fracture tool (split mesh into N pieces along Voronoi cell boundaries)
  - Fracture pieces stored as child meshes, invisible until break triggers
  - Break threshold: force/impulse exceeds configurable limit
- [x] Runtime break response — on threshold exceeded:
  - Hide original mesh, spawn fracture pieces as dynamic rigid bodies
  - Apply radial impulse from impact point
  - Debris particle spawning (dust, sparks, splinters)
- [x] Deformable meshes — vertex displacement from impact (dents in metal, cracks in stone)
  - Damage decals at deformation point (crack textures, scorch marks)
- [x] Breakable constraints — glass panes, wooden boards, weak walls
  - Fixed joints that break when force exceeds threshold (already in Jolt constraint system)
- [x] Chain destruction — breaking one object can trigger adjacent breaks (domino, collapse)

### Dismemberment System
Runtime limb separation for combat and horror games (Dead Space-style strategic dismemberment).
- [x] Dismemberment zones — define severable joints in skeleton (neck, shoulders, elbows, wrists, hips, knees)
  - Per-joint health/damage threshold for separation
  - Damage accumulation per zone (not just single hits)
- [x] Runtime mesh splitting at dismemberment joints
  - Generate cap geometry at cut point (procedural disc mesh with wound texture)
  - Detached limb becomes ragdoll rigid body
  - Stump gets gore/wound decal
- [x] Behavior modification after dismemberment
  - AI adjusts movement/attack patterns based on remaining limbs
  - Crawling enemy (both legs removed), one-armed attacks, headless stumbling
- [x] Gore system (optional, toggle in settings)
  - Blood particle spray at cut point
  - Blood decals on nearby surfaces
  - Dismembered limbs persist for configurable duration

### Milestone
~~The Tabernacle's linen curtains sway gently, the entrance veil drapes realistically from its poles, and doors throughout Solomon's Temple swing on hinged joints. Cloth correctly drapes over any scene geometry without manual collider placement. Objects can be grabbed and thrown, enemies ragdoll on death with dismemberable limbs, and destructible objects shatter realistically.~~ DONE

---

## Formula Pipeline (Cross-Cutting Infrastructure) — COMPLETE

Unified physics/lighting formula storage, evaluation, and code generation. Every physics and rendering system (cloth, water, foliage, particles, lighting) currently implements its own formulas independently. The Formula Pipeline provides a shared system for discovering, storing, compiling, and using mathematical formulas across the entire engine.

### Completed
- [x] EnvironmentForces system — centralized environmental query API (wind, weather, buoyancy, temperature, humidity, wetness)
- [x] Expression tree AST — 5 node types (literal, variable, binary op, unary op, conditional) with JSON round-trip
- [x] FormulaLibrary — named formula registry with categories, typed inputs/outputs, coefficients, quality tiers
- [x] Expression evaluator — scalar tree-walking interpreter for tool-time use
- [x] Physics templates — 15 built-in formulas (aerodynamic drag, Stokes drag, Fresnel-Schlick, Beer-Lambert, Gerstner wave, buoyancy, caustic depth fade, water absorption, inverse-square falloff, exponential fog, Hooke spring, Coulomb friction, terminal velocity, wet darkening, wind deformation)

### Completed (cont.)
- [x] FormulaCompiler — C++ code generator (expression tree → inline C++ functions with coefficient inlining)
- [x] FormulaCompiler — GLSL code generator (expression tree → GLSL function snippets, no std:: prefix)
- [x] LUT generator — sample formulas over input ranges into binary lookup tables (VLUT format with FNV-1a axis hashing)
- [x] LUT loader — load VLUT files with 1D/2D/3D linear/bilinear/trilinear interpolation, O(1) lookup

### Completed (cont.)
- [x] CurveFitter — custom Levenberg-Marquardt optimizer (zero external deps, Gaussian elimination, R²/RMSE/max error)
- [x] FormulaPreset system — named bundles of coefficient overrides for visual styles (9 built-in: Realistic Desert, Tropical Forest, Arctic Tundra, Underwater, Anime/Cel-Shaded, Painterly, Stormy Weather, Calm Interior, Biblical Tabernacle)
- [x] FormulaWorkbench — standalone ImGui/ImPlot tool with template browser, data editor (CSV import), LM fitter, curve visualizer, residual plots, train/test validation, preset browser, JSON export

### Completed (cont.)
- [x] Water formula optimization — quality-tiered caustics (Full: 6 reads + chromatic aberration, Approximate: 2 reads, Simple: 1 read), quality-tiered water FBM noise (3/2/0 octaves), APPROXIMATE expressions for Fresnel and Beer-Lambert, new caustic_depth_fade and water_absorption templates
- [x] FormulaQualityManager — global + per-category quality tier selection with JSON persistence, per-water-surface quality dropdown in inspector, wired into scene/terrain/water renderers

### Outstanding tool-loop follow-ups (low priority, tracked here for visibility)
Source of truth: [`docs/SELF_LEARNING_ROADMAP.md`](docs/SELF_LEARNING_ROADMAP.md). Surfaced in the main roadmap so these don't stay invisible; the design/test context lives in the sibling doc.

- [ ] **FW W5 (cont.)** — add a reference-regression spec per library formula (~17 more JSON specs) for broader regression coverage as the formula library grows. Ten shipped so far; each new spec is auto-discovered by `tests/test_reference_harness.cpp`.
- [ ] **Audit X1** — Phase 3 of the audit self-learning loop: a "propose-fix" layer that, after N runs, emits a markdown file flagging rules whose FP set has an exclude-pattern signature the user hasn't applied yet. Mirrors the Workbench's §3.6 suggestion engine for rule tuning.
- [ ] **Audit/FW X2** — write a unified-pattern doc ("how we built self-learning into two tools with the same observe → act structure"). Useful as blog-post material for the open-source repo and as a template for future self-learning subsystems (e.g. renderer perf autotuning).

---

## Phase 9: Domain-Driven System Architecture
**Goal:** Evolve the engine toward a domain-driven system model where each natural domain (vegetation, water, cloth, terrain, etc.) is owned by a dedicated system that encapsulates ALL behavior for that domain — rendering, physics, animation, audio, defaults, and editor integration. Scenes compose by pulling in only the systems they need.

This is NOT a rewrite — it's a refactor and extension of what already exists. Vestige already partially follows this pattern (terrain system, foliage manager, water renderer, cloth simulator exist as separate subsystems). This phase formalizes the pattern.

Reference: `Vestige Overhaul.md` contains the full architecture document.

---

### Phase 9A: System Infrastructure (COMPLETE)
**Goal:** Create the formal system abstraction and registry without breaking any existing code.

Shipped via commit `dfbb96b` — "Phase 9A: Domain system infrastructure — ISystem, SystemRegistry, cross-system events".

#### ISystem Interface
- [x] `ISystem` base class with 4 pure virtuals (`getSystemName`, `initialize`, `shutdown`, `update`) and opt-in virtual no-ops (`fixedUpdate`, `submitRenderData`, `onSceneLoad`, `onSceneUnload`, `drawDebug`, `reportMetrics`) — `engine/core/i_system.h`. The original brainstorm listed `syncPhysics` / `registerEditorUI` / `serialize` / `deserialize` as opt-ins; those did not ship on the interface and are handled elsewhere (physics owns its own sync, serializer is a free function, editor UI is its own layer).
- [x] Performance budget API (`getFrameBudgetMs` / `setFrameBudgetMs`) — `i_system.h:119`
- [x] `getOwnedComponentTypes()` — each system declares which component types it manages (minor rename from design: `getOwnedComponents` → `getOwnedComponentTypes`)

#### SystemRegistry
- [x] Registry that manages all domain system instances (`registerSystem`, `getSystem<T>`, `initializeAll`, `updateAll`, `shutdownAll`) — `engine/core/system_registry.h`
- [x] Auto-activation: scan scene entities for component types, match against each system's `getOwnedComponentTypes()`, activate only systems with matching components present (`system_registry.cpp:209` "auto-activated")
- [x] `isForceActive()` for always-on systems (Atmosphere, Lighting) regardless of scene contents (minor rename: `forceActivate()` → `isForceActive()` — `system_registry.cpp:181`)
- [x] Integrate into Engine update loop (`updateAll`, `fixedUpdateAll`, `submitRenderDataAll` — `engine.cpp:956`)

#### Cross-System Interaction
- [x] Typed event structs for discrete occurrences on `engine/core/system_events.h` — `SceneLoadedEvent`, `SceneUnloadedEvent`, `WeatherChangedEvent`, `EntityDestroyedEvent`, `TerrainModifiedEvent`, `AudioPlayEvent`, `NavMeshBakedEvent`. Additional concept-level events (`ObjectEnteredWaterEvent`, `LightningStrikeEvent`) can be added lazily as domains need them — the pattern and EventBus plumbing are in place.
- [x] Query model for continuous position-dependent data — systems query `EnvironmentForces` / `Terrain` directly via shared infrastructure (see `AtmosphereSystem`, `WaterSystem`)
- [x] Rule: events for discrete occurrences, queries for continuous data. Systems never `#include` each other.

---

### Phase 9B: Wrap Existing Code into Domain Systems
**Goal:** Wrap each existing subsystem into a formal domain system class. Each step: create the system class, have it own the existing subsystem instances, register with shared infrastructure. Tests must pass after each step.

#### Domain Systems to Wrap
- [x] **Terrain System** — wrap `environment/terrain`, `renderer/terrain_renderer`, brush tools. Owns: heightfield editing, splatmap texturing, collision mesh, LOD, terrain-vegetation integration, road/path system
- [x] **Vegetation System** — wrap `environment/foliage_manager`, `environment/density_map`, `renderer/foliage_renderer`, `environment/biome_preset`. Owns: all tree/grass/shrub entities, procedural placement, wind animation (queries EnvironmentForces), LOD, species presets
- [x] **Water & Fluid System** — wrap `renderer/water_renderer`, `renderer/water_fbo`, `scene/water_surface`. Owns: ocean/lake/river/puddle entities, surface simulation, rendering (reflections, refractions, caustics), buoyancy (queries EnvironmentForces), shore blending
- [x] **Cloth & Soft Body System** — wrap `physics/cloth_simulator`, `physics/cloth_component`, `physics/cloth_presets`, `physics/fabric_material`, `physics/cloth_mesh_collider`, `physics/spatial_hash`. Owns: all cloth entities, XPBD simulation, fabric presets, collision, wind response (queries EnvironmentForces). Performance budget enforced: distant/offscreen cloths auto-reduce quality
  - [x] **GPU Compute Cloth Pipeline** — XPBD solver migrated to compute shaders. SSBO storage for positions / prev positions / velocities / normals / constraints / dihedrals / colliders / LRA. Per-frame substep loop dispatches: wind → integrate → distance constraints (per colour) → dihedrals (per colour) → collision → LRA → normals. Greedy graph colouring (CPU-side, one-shot at init) partitions distance + dihedral constraints so each colour is solved in parallel without atomics. Pin support + LRA tethering on GPU. Auto CPU↔GPU selection via `cloth_backend_factory` (≥1024-particle threshold). Files: `engine/physics/{cloth_solver_backend.h, cloth_constraint_graph.{h,cpp}, gpu_cloth_simulator.{h,cpp}, cloth_backend_factory.{h,cpp}}`, shaders `assets/shaders/cloth_{wind,integrate,constraints,dihedral,collision,normals,lra}.comp.glsl`. **GPU cloth follow-ups (deferred):** GPU self-collision (needs spatial-hash on GPU), GPU mesh collider (needs BVH on GPU), GPU tearing (irregular post-tear topology), Vulkan-compute port, full perf-acceptance gate (100×100 ≥ 120 FPS on RX 6600).
- [x] **Destruction & Physics System** — wrap `physics/fracture`, `physics/deformable_mesh`, `physics/breakable_component`, `physics/dismemberment`, `physics/rigid_body`, `physics/grab_system`, `physics/ragdoll`. Owns: rigid bodies, destructibles, dismemberment, debris, grabbing, joints, ragdoll
- [x] **Character & Animation System** — wrap `animation/` (skeletal, state machine, motion matching, IK, facial, lip sync, morph targets), `physics/physics_character_controller`. Owns: animation playback, blending, motion matching, IK, facial animation, character controller, ragdoll transition
- [x] **Particle & VFX System** — wrap `renderer/particle_renderer`, `renderer/gpu_particle_system`, `scene/particle_emitter`, `scene/gpu_particle_emitter`, `scene/particle_presets`. Owns: CPU/GPU particles, VFX presets, decals, trails, screen effects
- [x] **Lighting System** — wrap all light/shadow/probe/IBL/radiosity code. Owns: all light types, shadow mapping, IBL, light probes, radiosity baking, volumetric light shafts
- [x] **Atmosphere & Weather System** — wrap `environment/environment_forces` (already implemented), `renderer/skybox`, `renderer/environment_map`. Extends EnvironmentForces with sky rendering, clouds, time-of-day, fog, weather transitions, lightning

#### Consistency Guarantees
Each system provides sensible defaults for its domain. Objects behave correctly the moment they're created:
- Vegetation automatically responds to wind, has LOD, casts shadows, sways naturally
- Every water body has correct reflections, refractions, caustics, and depth coloring
- Cloth automatically gets correct simulation based on fabric type preset
- Destruction patterns driven by material type, not per-object configuration
- Characters automatically get working animation pipeline with terrain-adaptive feet

---

### Phase 9C: New Domain Systems (FOUNDATIONS SHIPPED)
**Goal:** Build domain systems for capabilities that don't yet exist in the engine.

Foundations shipped via commit `fa0b100` — "Phase 9C: New domain systems — Audio (OpenAL Soft), UI/HUD, Navigation (Recast/Detour)". Each sub-system landed with an `ISystem` wrapper and the minimum viable feature set; richer capabilities are queued for later phases as noted per sub-system.

#### Audio System
*Scope: multi-month initiative requiring library selection and dedicated design document.*
- [x] Audio library integration — OpenAL Soft selected; `engine/audio/audio_engine.{h,cpp}` + `engine/systems/audio_system.{h,cpp}`; dr_libs/stb_vorbis decoders.
- [x] Spatial audio — 3D positioned sound sources with distance attenuation (`AudioSourceComponent::spatial`, `AudioEngine` listener pose).
- [ ] Ambient soundscapes (biome-based, time-of-day-based) — **deferred to Phase 10.**
- [ ] Sound material interactions (footstep sounds derived from physics material types) — **deferred to Phase 10.**
- [ ] Music system (layered tracks, transitions, adaptive intensity) — **deferred to Phase 10.**
- [ ] Reverb zones (indoor/outdoor, auto-detect room geometry) — **deferred to Phase 10.**
- [ ] Audio occlusion (raycast-based, material-based transmission) — **deferred to Phase 10.**
- [ ] Voice/dialogue playback (integrates with existing lip sync system) — **deferred to Phase 10.**
- [ ] Editor: sound emitter placement, reverb zone painting, audio preview — **deferred to Phase 10.**

**Note:** Detailed audio specs are in Phase 10 (Polish and Features). Phase 9C implemented the Audio domain-system wrapper + OpenAL integration + spatial audio; Phase 10 will deliver the full feature set.

#### UI & HUD System
- [x] In-game UI rendering — `engine/ui/sprite_batch_renderer.{h,cpp}` + `UIElement` hierarchy (`UICanvas`, `UIImage`, `UILabel`, `UIPanel`) + `engine/systems/ui_system.{h,cpp}`; sprite shaders `assets/shaders/ui_sprite.{vert,frag}.glsl`. Separate from the ImGui editor overlay.
- [x] In-world UI — `engine/ui/ui_world_label.{h,cpp}` (3D-anchored floating text with frustum-culled world-to-screen projection via the pure-CPU `ui_world_projection.{h,cpp}` helper) + `ui_interaction_prompt.{h,cpp}` (`UIWorldLabel` subclass with "Press [KEY] to action" formatting + linear distance-based alpha fade between `fadeNear` and `fadeFar`). Nameplates use `UIWorldLabel` directly with a per-frame `worldPosition` setter from game code that follows the entity. 11 unit tests in `tests/test_ui_world_projection.cpp`.
- [x] Screen-space UI (HUD, minimap, crosshair) — `engine/ui/ui_crosshair.{h,cpp}` (centred plus with configurable arms / gap), `ui_progress_bar.{h,cpp}` (ratio-based fill, clamped), `ui_fps_counter.{h,cpp}` (smoothed EMA + TextRenderer drawing). Minimap deferred (needs render-target + camera-frustum overlay).
- [x] Menu system (main menu, pause, settings) — `engine/ui/menu_prefabs.{h,cpp}` ships factory functions `buildMainMenu`, `buildPauseMenu`, `buildSettingsMenu` populating a `UICanvas` with positioned widgets per the `vestige-ui-hud-inworld` Claude Design layouts. Widget set: `UIButton` (default/primary/ghost/danger/sm), `UISlider`, `UICheckbox`, `UIDropdown`, `UIKeybindRow` — see `engine/ui/ui_*.{h,cpp}`. `UITheme` widened with the design's full token set (palette + sizing + type + font logical names) and a `UITheme::plumbline()` static for the alternative monastic-minimal register. Settings ships chrome only (header / sidebar / footer); per-game projects append their per-category controls. Nav state machine + signal wiring is per-game integration.
- [x] UI theming — `engine/ui/ui_theme.h` (`UITheme` struct: bg / text / accent palettes + crosshair / progress-bar sizes + default text scale; `UISystem::getTheme()` returns mutable ref so game projects can override per-field at startup).
- [x] Input routing — `UISystem::setModalCapture(bool)` for sticky modal capture (pause menu, dialog) + `updateMouseHit()` for cursor-over-interactive-element capture. `wantsCaptureInput()` is the union; game input handlers consult it each frame.
- [x] Editor: visual UI layout editor, theme editor — `engine/editor/panels/ui_layout_panel.{h,cpp}` shipped as a `Window → UI Layout` ImGui panel. Inspects the element tree of any `UICanvas` passed in (per-element position / size / anchor / visibility / interactivity live-editable) + full color-picker surface over `UITheme` (backgrounds / strokes / text / accent / HUD / sizes). Vellum and Plumbline reset buttons for quick-switch between registers. **Follow-up enhancements:** drag-place widget palette (needs viewport mouse capture + drag math) and JSON canvas serialisation (needs per-element-type reflection). Both gated on factoring the editor's ImGui viewport out of `editor.cpp`.

#### AI & Navigation System (Basics)
*Scope: requires Recast/Detour library integration and dedicated design document.*
- [x] Navmesh generation from terrain and static geometry — Recast via `engine/navigation/nav_mesh_builder.{h,cpp}` + `engine/systems/navigation_system.{h,cpp}`.
- [x] Pathfinding (A* on navmesh) — Detour via `engine/navigation/nav_mesh_query.{h,cpp}`; `NavAgentComponent` for per-entity agents.
- [x] Editor: navmesh visualization and bake controls — `engine/editor/panels/navigation_panel.{h,cpp}`. `Window → Navigation` menu toggle; ImGui controls for all `NavMeshBuildConfig` fields; `Bake` / `Clear` buttons drive `NavigationSystem::bakeNavMesh()`; "Show polygon overlay" toggle draws every navmesh polygon's edges via `DebugDraw::line` (configurable colour + Y-lift). 6 unit tests in `tests/test_navigation_panel.cpp`. Patrol-path placement deferred to Phase 16 (AI behaviour trees).

**Note:** Phase 9C ships only the *movement* layer of AI — navmesh bake + A* pathfinding + per-entity agents. The *decision* layer (state management, behaviour trees, perception, AI director, patrol-path placement) is deliberately parked for Phase 16 (Scripting and Interactivity), where it can be designed as one coherent system alongside scripting and blackboards.

---

### Phase 9D: Editor Enhancements (COMPLETE)
**Goal:** Asset viewers and game type templates that make the editor a complete creation tool.

Shipped via commit `6a40da4` — "Phase 9D: Editor enhancements — asset viewers and game type templates".

#### Model Viewer Panel
Implementation: `engine/editor/panels/model_viewer_panel.{h,cpp}`
- [x] Orbiting camera around the model
- [x] Material/texture display
- [x] Animation playback (if model has animations)
- [x] Skeleton visualization
- [x] Bounding box and vertex/triangle count display
- [x] Drag from model viewer into scene viewport to place

#### Texture Viewer Panel
Implementation: `engine/editor/panels/texture_viewer_panel.{h,cpp}`
- [x] Full-resolution display with zoom/pan
- [x] Channel isolation (R, G, B, A, RGB)
- [x] Mipmap level visualization
- [x] Tiling preview (repeat texture to see how it tiles)
- [x] Texture metadata (resolution, format, memory size)
- [x] PBR texture set grouping (albedo + normal + roughness shown together)

#### HDRI Viewer Panel
Implementation: `engine/editor/panels/hdri_viewer_panel.{h,cpp}`
- [x] Spherical/equirectangular preview
- [x] Exposure adjustment
- [x] Preview as skybox in mini-viewport
- [x] Show irradiance and specular prefiltered versions
- [x] One-click set as scene environment map

#### Game Type Templates
Implementation: `engine/editor/panels/template_dialog.{h,cpp}` — `GameTemplateType` enum with all 6 variants.
- [x] **3D First Person** — perspective camera, full 3D physics, FPS controller (`FIRST_PERSON_3D`)
- [x] **3D Third Person** — perspective camera with follow/orbit cam, character system (`THIRD_PERSON_3D`)
- [x] **2.5D** — 3D rendering with gameplay constrained to a plane (`TWO_POINT_FIVE_D`)
- [x] **Isometric** — fixed-angle orthographic camera, grid-based or free movement (`ISOMETRIC`)
- [x] **Top-Down** — orthographic overhead camera, free movement (`TOP_DOWN`)
- [x] **Point-and-Click** — fixed or panning camera, click-to-move navigation (`POINT_AND_CLICK`)

Each template configures: camera type/constraints, physics dimensionality, default input mapping, required systems, starter scene layout.

**Note:** 2D templates (Side-Scroller, Shmup) require Phase 9F's sprite renderer and 2D physics.

---

### Phase 9E: Visual Scripting
**Goal:** No-code gameplay logic via a node-based graph editor.

*Scope: major initiative — essentially building a programming language with an IDE. Requires dedicated design document and evaluation of existing node graph libraries (imnodes, imgui-node-editor).*

**Node Graph Infrastructure:**
- [x] Node graph data structures (NodeGraph, Node, Port, Connection in engine/formula/node_graph.h/.cpp)
- [x] Node type registry (factory helpers: createMathNode, createFunctionNode, createLiteralNode, createVariableNode, createOutputNode)
- [x] Connection validation (cycle detection via BFS, type checking, duplicate rejection)
- [x] Graph serialization (JSON round-trip with version tag, 61 unit tests)
- [x] Graph-to-ExpressionTree conversion (bidirectional: toExpressionTree + fromExpressionTree)
- [x] Visual scripting data model (ScriptValue, Blackboard, ScriptGraph, ScriptNodeDef, ScriptConnection in engine/scripting/)
- [x] Node type registry (NodeTypeDescriptor with execute functions, categorized palette lookup)
- [x] Graph interpreter (ScriptContext: impulse-driven execution, lazy data pull, call depth + node count safety limits)
- [x] Runtime instances (ScriptInstance with per-graph blackboard, latent action queue, event subscriptions)
- [x] ScriptComponent (entity attachment) + ScriptingSystem (ISystem with update loop, latent tick, event bridge)
- [x] 10 core node types (OnStart, OnUpdate, OnDestroy, Branch, Sequence, Delay, SetVariable, GetVariable, PrintToScreen, LogMessage)
- [x] 6-level variable scoping (Flow, Graph, Entity, Scene, Application, Saved) via Blackboard
- [x] 43 unit tests for scripting infrastructure
- [x] Design document with library evaluation (docs/PHASE9E_DESIGN.md, imgui-node-editor chosen)
- [x] **Phase 9E-2:** EventBus bridge — string-dispatched subscription from event nodes to typed C++ engine events; per-instance subscription tracking; automatic cleanup on scene unload
- [x] **Phase 9E-2:** 12 event nodes (OnKeyPressed, OnKeyReleased, OnMouseButton, OnSceneLoaded, OnWeatherChanged, OnCustomEvent wired; OnTrigger/Collision/Audio/Variable stubs registered for palette completeness)
- [x] **Phase 9E-2:** 15 action nodes (PlaySound, SpawnEntity, DestroyEntity, SetPosition, SetRotation, SetScale, ApplyForce, ApplyImpulse, SetVisibility, SetLightColor, SetLightIntensity, PublishEvent live; PlayAnimation/SpawnParticles/SetMaterial stubs)
- [x] **Phase 9E-2:** 22 pure nodes (GetPosition, GetRotation, FindEntityByName, MathAdd/Sub/Mul/Div, MathClamp, MathLerp, GetDistance, VectorNormalize, DotProduct, CrossProduct, BoolAnd/Or/Not, CompareEqual/Less/Greater, ToString, HasVariable, Raycast)
- [x] **Phase 9E-2:** 7 flow control nodes (SwitchInt, SwitchString, ForLoop with iteration cap, WhileLoop with safety valve, Gate, DoOnce, FlipFlop — stateful nodes persist across executions via ScriptNodeInstance::runtimeState)
- [x] **Phase 9E-2:** 4 latent nodes (WaitForEvent, WaitForCondition, Timeline with onTick progress callback, MoveTo with entity-lookup-by-ID for safety)
- [x] **Phase 9E-2:** ScriptCustomEvent for PublishEvent ↔ OnCustomEvent round-trip with name filtering
- [x] **Phase 9E-2:** 29 new unit tests — registration, math, vector, boolean, comparison, flow control, latent scheduling, and end-to-end EventBus bridge delivery (70 registered node types total)
- [x] Node graph renderer (imgui-node-editor integration) — Phase 9E-3
  - Step 4 canvas + File-menu are code-complete; a shutdown SEGV was fixed by routing `Config::SaveSettings`/`LoadSettings` through a shutdown-gated callback so `ed::DestroyEditor` no longer touches torn-down ImGui state. Settings persistence re-enabled at `~/.config/vestige/NodeEditor.json`. Runtime-verified 2026-04-20: clean shutdown with no crash, and dragged node positions survive restart (`NodeEditorWidget::hasPersistedPosition` parses the saved JSON at init so `ScriptEditorPanel` skips its template-default seeding for nodes whose layout was already serialized — without it, the template loader stomped persistence every time).
- [x] Graph compilation to executable logic (beyond expression trees) — Phase 9E-5. `engine/scripting/script_compiler.{h,cpp}` adds a `ScriptGraphCompiler` that produces a validated, index-based `CompiledScriptGraph` IR from a `ScriptGraph` + `NodeTypeRegistry`. Validation passes: node type resolution, duplicate-id detection, connection endpoint resolution, pin kind match, pin data-type compatibility (INT→FLOAT / BOOL→INT/FLOAT / ENTITY→INT / COLOR↔VEC4 / all→STRING widening + ANY wildcard, matching `ScriptValue` runtime coercions), input fan-in ≤ 1, pure-data cycle detection (execution cycles and exec fan-out intentionally permitted — shipped templates use the latter), entry-point discovery (event nodes + OnStart / OnUpdate + anything in the "Events" category, so stub event nodes still count), and reachability classification with warnings for orphaned impure nodes. `ScriptingSystem::registerInstance` runs the compiler and refuses to activate instances with fatal errors, logging each diagnostic against the graph name. 16 unit tests in `tests/test_script_compiler.cpp` (every shipped gameplay template compiles clean, every error class exercised, wiring round-trip verified).

**Gameplay Scripting Nodes:**
- [x] Event nodes (keypress, mouse button, scene loaded, weather changed, custom events; trigger/collision stubs pending engine events)
- [x] Action nodes (move entity, play sound, spawn entity, destroy entity, set variable, visibility, light color/intensity, publish custom event; animation/particle/material stubs pending integration)
- [x] Flow control (branch, sequence, delay, switch, for/while loops, gate, do-once, flip-flop)
- [x] Variable system (per-graph blackboard; per-entity/scene/app scoping infrastructure in place — inspector exposure lands with Phase 9E-3 editor UI)
- [x] Pre-built gameplay templates (door that opens, collectible item, damage zone, checkpoint, dialogue trigger) — Phase 9E-4. `engine/scripting/script_templates.{h,cpp}` ships 5 starter graphs (`GameplayTemplate::DOOR_OPENS / COLLECTIBLE_ITEM / DAMAGE_ZONE / CHECKPOINT / DIALOGUE_TRIGGER`) wired from the existing registered nodes. Each template is a self-contained `ScriptGraph` with sensible property overrides (clip paths, variable names, event names) so designers drop it on a trigger volume entity and iterate. Tests: 8 in `tests/test_script_templates.cpp` (each template validates + resolves every connection pin against `NodeTypeRegistry`; JSON round-trip; metadata coverage; trigger-entry-point invariant). Uses `OnTriggerEnter` + `OnCollisionEnter` — still stub events on the EventBus side, but graphs are graph-valid now and fire automatically once those events are wired.

**Formula Node Editor** (extends FormulaWorkbench):
- [x] Math node types (add, multiply, sin, cos, pow, clamp, lerp, etc.) — factory helpers implemented
- [x] Variable input nodes (bind to formula inputs and coefficients) — createVariableNode()
- [x] Convert node graph to ExpressionTree for use in FormulaLibrary — toExpressionTree()
- [x] Bidirectional: load existing ExpressionTree formulas into the node editor — fromExpressionTree()
- [x] Output node with real-time curve preview. `FormulaNodeEditorPanel` (`tools/formula_workbench/formula_node_editor_panel.{h,cpp}`) renders the active `NodeGraph` inside a `NodeEditorWidget` canvas and plots the output via ImPlot underneath. The headless helper `sampleFormulaCurve()` (split into `formula_node_editor_core.cpp` so tests compile without ImGui / ImPlot) converts graph → `ExprNode` → sampled `(xs, ys)` over a user-configurable sweep variable + range, with safe fallbacks for missing variables, constant-only graphs, empty graphs, and broken trees. Tests: 13 in `tests/test_formula_node_editor_panel.cpp` (monotonicity on `ease_in_sine`, linearity on `aerodynamic_drag`, sweep-variable auto-pick, clamp bounds, error paths).
- [x] Drag-and-drop from PhysicsTemplates catalog into the node graph. The panel's left pane lists every `PhysicsTemplates::createAll()` entry grouped by category; each entry is both clickable (replaces the current graph via `NodeGraph::fromExpressionTree(def.FULL)`) and an ImGui drag-drop source with a `FORMULA_TEMPLATE` payload. The canvas child is a drop target that resolves the payload back to `loadTemplate()`. Input defaults + coefficient values carry across so the preview picks a sensible X-axis immediately.
- [x] Visual formula composition UI (ImGui node editor rendering). Same `FormulaNodeEditorPanel` — reuses `NodeEditorWidget` for pan / zoom / layout persistence (settings file `formula_node_editor.json`), so the formula canvas and the script-editor canvas share the imgui-node-editor wrapper without leaking state between their `ed::EditorContext`s. Toggled from the Workbench's `View → Node Editor` menu item; lifecycle tied to `Workbench::initializeGui()` / `shutdownGui()` so `ed::DestroyEditor` runs before `ImGui::DestroyContext`.
- [x] **CONDITIONAL node type**. `NodeGraph::createConditionalNode()` ships a 3-input branching node (Condition / Then / Else → Result); `fromExpressionTree` builds it directly from `ExprNodeType::CONDITIONAL` instead of the former lossy `literal(0)` fallback, and `nodeToExpr` detects `operation == "conditional"` to rebuild `ExprNode::conditional` on export. Unblocks PhysicsTemplates with ternary saturation curves. Tests: `NodeGraph_Factory.CreateConditionalNode`, `NodeGraph_ExprTree.FromExpressionTreeConditional`, `RoundTripConditionalExpr`, `RoundTripNestedConditional`.

Prioritize basic event-to-action chains first. The formula node editor builds on the same node graph infrastructure. Advanced flow control and variable systems come later.

---

### Phase 9F: 2D Game Support
**Goal:** Enable 2D games alongside existing 3D capabilities.

*Requires renderer additions (sprite batch, 2D physics integration) before 2D game templates become viable.*

- [x] **Phase 9F-1:** Sprite renderer (textured quads with z-ordering, tint, flip) — `engine/renderer/sprite_renderer.{h,cpp}` + `engine/systems/sprite_system.{h,cpp}`; instance-rate VBO (80 bytes/sprite), separate from UI's CPU-merged `SpriteBatchRenderer`. Sort-by-(layer, order, y, id) is headless so tests validate it without a GL context. `assets/shaders/sprite.{vert,frag}.glsl` reconstructs the 2D affine from two packed rows.
- [x] **Phase 9F-1:** Sprite atlas / batch renderer — `engine/renderer/sprite_atlas.{h,cpp}` loads TexturePacker JSON (array + hash forms), pre-normalised UVs, optional per-frame pivots.
- [x] **Phase 9F-1:** Sprite sheet animation — `engine/animation/sprite_animation.{h,cpp}`; Aseprite-compatible per-frame-duration clips, forward/reverse/ping-pong, loop control, multi-clip state machine on a SpriteComponent. 27 new unit tests.
- [x] **Phase 9F-2:** 2D physics integration — Jolt 2D constraint mode via per-body `EAllowedDOFs::Plane2D`. No second physics engine; shared broadphase/narrowphase with the 3D world. `engine/systems/physics2d_system.{h,cpp}` registers an ISystem, wraps the Engine's PhysicsWorld, exposes a 2D-native API (applyImpulse, setLinearVelocity, setTransform all in `glm::vec2`).
- [x] **Phase 9F-2:** 2D collision shapes — `engine/scene/collider_2d_component.{h,cpp}` ships Box, Circle, Capsule, Polygon (convex), and EdgeChain (static-only chain mesh). Thin extruded-Z-slab representation makes them collide against the shared 3D world. 15 new tests covering gravity, DOF lock, shape coverage, sensor mode, fixed-rotation lock.
- [x] **Phase 9F-4:** 2D character controller (platformer movement, wall slide, coyote time) — `engine/scene/character_controller_2d_component.{h,cpp}` ships coyote time, jump buffering, variable-jump cut, wall slide, ground/air acceleration, ground friction. Step-function helper returns `true` on fired jump so callers drive SFX/particles. Shared commit with the 2D camera (ec62677).
- [x] **Phase 9F-3:** Tilemap system — `engine/scene/tilemap_component.{h,cpp}` ships multi-layer grids (TileId uint16, 0 = empty), named layers with sort order, animated tiles via frame-sequence definitions, global animation time wrapping at 1 hour. `engine/renderer/tilemap_renderer.{h,cpp}` converts a tilemap into SpriteInstance records for the shared sprite pass — no dedicated shader. Auto-tiling defers to Phase 18 per the design doc; authoring + rendering + animation are live. 12 new unit tests.
- [x] **Phase 9F-4:** 2D camera (orthographic with smooth follow, deadzone, bounds) — `engine/scene/camera_2d_component.{h,cpp}` ships orthoHalfHeight + follow offset + deadzone + Unity-style SmoothDamp spring + optional world-bounds clamp. `updateCamera2DFollow(camera, target, dt)` is the step helper.
- [x] **Phase 9F-5:** 2D game type templates (Side-Scroller, Shmup) — `engine/scene/game_templates_2d.{h,cpp}` ships `createSideScrollerTemplate` (player + ground + platforms + Camera2D follow) and `createShmupTemplate` (kinematic player + scrolling tilemap + locked ortho camera). Optional atlas binding via `GameTemplate2DConfig`; templates ship without sprites when designers want to plug in their own art later.
- [x] **Phase 9F-6:** Editor panels — `engine/editor/panels/sprite_panel.{h,cpp}` (load TexturePacker JSON, list frames, assign to selection), `engine/editor/panels/tilemap_panel.{h,cpp}` (layer list + resize knobs + palette picker + headless paint/erase helpers). Template dialog now exposes Side-Scroller 2D and Shmup 2D; `applyTemplate` dispatches 2D types to the Phase-9F-5 template generators. Viewport-click paint pipeline and slicing-from-PNG defer to Phase 18.

**Note:** Full 2D specs are in Phase 18 (2D Game and Scene Support). This phase implements the core; Phase 18 has the complete feature set.

---

### Phase 9 Key Principles

1. **Don't break what works.** Every refactoring step leaves the engine functional. Wrap existing code; don't delete and rewrite it.
2. **Consistency through defaults.** Each system provides sensible defaults. Objects behave correctly the moment they're created. Defaults applied in component constructors and `onSceneLoad`.
3. **Systems don't know about each other.** Typed events on the Event Bus for discrete occurrences. Query shared infrastructure (EnvironmentForces, Terrain) for continuous data. No `#include` between domain systems.
4. **The editor is the product.** Every system must have editor UI. If a designer can't use a feature from the editor, it's not done.
5. **Shared infrastructure stays generic.** The renderer knows meshes and materials, not "trees." The vegetation system knows trees and submits the right meshes to the renderer.
6. **Performance budgets are mandatory.** Every simulation system (cloth, particles, physics, AI) respects a per-frame time budget and automatically reduces quality to maintain 60 FPS.
7. **Auto-activation over manual configuration.** The registry discovers required systems from scene component types. No manual system lists.

### Milestone
All existing subsystems wrapped in formal domain system classes. Audio, UI, and AI basics operational. Model/Texture/HDRI viewer panels in the editor. Game type templates for 3D styles. Visual scripting with basic event-to-action chains. 2D sprite rendering and tilemap support. Designers can build complete games in the editor without writing C++.

---

## Phase 10: Polish and Features
**Goal:** Complete the experience — rendering enhancements, localization, accessibility, and cinematic effects.

**Note:** Audio System and UI System are now implemented as domain systems in Phase 9C. Camera modes are partially covered by Phase 9D's game type templates. This phase focuses on rendering enhancements, cinematic effects, localization, and accessibility that are system-agnostic.

### Features
- [x] In-game UI system (menus, HUD, information panels/plaques). `engine/ui/game_screen.{h,cpp}` — pure-function `GameScreen` state machine (MainMenu / Loading / Playing / Paused / Settings / Exiting) with total transition table and `isWorldSimulationSuspended` / `suppressesWorldInput` predicates (slice 12.1). `UISystem::setRootScreen` / `pushModalScreen` / `popModalScreen` / `applyIntent` + per-screen `ScreenBuilder` hook (`setScreenBuilder`) so game projects can override MainMenu / Pause / Settings with studio-branded prefabs without touching engine code; Engine wiring routes ESC through `applyIntent(Pause/Resume/CloseSettings)` (slice 12.2). `engine/ui/ui_notification_toast.{h,cpp}` — headless `NotificationQueue` (FIFO, default cap 3, push-newest/drop-oldest) with `NotificationSeverity::{Info, Success, Warning, Error}` and a pure `notificationAlphaAt(elapsed, duration, fade)` envelope (fade-in / plateau / fade-out, collapses to rectangle under reduced-motion). `UISystem::update` advances the queue against `UITheme::transitionDuration`; `UINotificationToast` renders a severity-accented panel + title + body with full accessibility metadata (slice 12.3). `buildDefaultHud(canvas, theme, textRenderer, uiSystem)` populates the `Playing` canvas with crosshair (CENTER) + FPS counter (TOP_LEFT, hidden by default) + interaction-prompt anchor (BOTTOM_CENTER) + top-right notification stack (three pre-created toast slots); `Playing` now has a built-in default `ScreenBuilder` (slice 12.4). `engine/editor/panels/ui_runtime_panel.{h,cpp}` — four-tab editor surface (State / Menus / HUD / Accessibility): current-screen readout + manual intent firing + scrollback of the last 20 screen transitions; MainMenu / Pause / Settings prefab preview with rebuild button; per-HUD-element visibility toggles that write through to the live `UISystem` canvas; live compose of scale preset + high-contrast + reduced-motion (slice 12.5). ~60 new unit tests across `test_game_screen.cpp`, `test_ui_system_screen_stack.cpp`, `test_notification_queue.cpp`, `test_default_hud.cpp`, `test_ui_runtime_panel.cpp`. See `docs/PHASE10_UI_DESIGN.md` for the full design, inventory, and sign-off log.
- [x] Text rendering (TrueType fonts). `engine/renderer/font.{h,cpp}` — FreeType-backed TTF loader with `GlyphInfo` atlas (per-codepoint UV offset + size, pixel bitmap size, baseline bearing, horizontal advance in 1/64-pixel units); atlas uploaded once at load to an `r8` `Texture`. `engine/renderer/text_renderer.{h,cpp}` — screen-space `renderText2D(text, x, y, scale, color, screenWidth, screenHeight)` and world-space 3D `renderText3D` paths over the glyph atlas, with upper-bound-per-call batching and an ortho projection built per draw so callers don't have to juggle matrices. Consumed by the Phase 10 UI widget library (`UILabel`, `UIButton`, `UIFpsCounter`, `UINotificationToast`, `UIInteractionPrompt`, menu prefabs) — the bullet ships the rendering primitive that every in-game UI surface already relies on. Covered by `tests/test_text_rendering.cpp`.
- [ ] Scene/level configuration files (define scenes in data, not code)
- [ ] Settings system (resolution, quality presets, keybindings)
- [ ] Loading screens (for scene transitions)
- [ ] Information plaques — approach an object to see a text description

### Audio System
Full spatial audio pipeline with dynamic mixing, occlusion, and adaptive music. The Audio domain system (Phase 9C) provides the system wrapper and basic functionality; the detailed feature specs below define the complete implementation.
- [x] Audio engine integration (OpenAL Soft chosen over FMOD — zlib-compatible, vendored via FetchContent, no runtime licensing concerns for MIT-open-source launch). Streaming playback via `engine/audio/audio_music_stream.{h,cpp}` — `MusicStreamState` + `planStreamTick(state, decoderAtEof)` model the decode-side state machine (frames decoded vs. consumed, sample-rate-aware buffered-seconds math, min/max buffered targets for back-pressure, chunk-sized refill, loop-policy routing). One-shot playback shipped via `AudioEngine::playSound` (pre-loaded via `AudioClip::loadFromFile`, cached per-path in `AudioEngine::loadBuffer`). Audio source component shipped as `AudioSourceComponent` with all the Phase 10 accumulated fields (attenuation / velocity / occlusion). 16 new unit tests cover buffered-seconds math, consumed/decoded notification, finished-after-full-drain invariant, back-pressure at max-buffered cap, chunk-sized refill when below min, EOF-triggered rewind under infinite + finite loop policy, finished state hold.
- [x] Spatial audio (3D positioned sound sources — crackling torch, splashing water) — distance-attenuation curves (4 models incl. pass-through) + Doppler shift with listener/source velocity + HRTF selection (Auto / Forced / Disabled with dataset picker). Pure-function cores under `engine/audio/` keep CPU-side math and OpenAL native evaluation in agreement. 44 new unit tests across the three slices.
  - [x] Distance attenuation curves (linear, logarithmic, custom) — `AttenuationModel` enum (`None` / `Linear` / `InverseDistance` / `Exponential`) + `AttenuationParams` (referenceDistance / maxDistance / rolloffFactor) + pure-function `computeAttenuation(model, params, distance)` in `engine/audio/audio_attenuation.{h,cpp}`. Each model matches an OpenAL `AL_*_DISTANCE_CLAMPED` constant via `alDistanceModelFor(model)`. `AudioEngine::setDistanceModel` swaps the engine-wide curve; `AudioEngine::playSoundSpatial(path, position, params, volume, loop)` accepts per-source attenuation; `AudioSourceComponent` carries `attenuationModel` + `rolloffFactor`. Canonical forms, no magic constants — Formula Workbench rule doesn't apply (no coefficients to fit). 15 new unit tests.
  - [x] HRTF support for headphones (head-related transfer function for accurate 3D positioning) — `HrtfMode` enum (`Disabled` / `Auto` / `Forced`) + `HrtfStatus` enum (mirrors `ALC_HRTF_STATUS_SOFT` values: Disabled / Enabled / Denied / Required / HeadphonesDetected / UnsupportedFormat / Unknown) + `HrtfSettings` (mode + preferredDataset name) + `resolveHrtfDatasetIndex(available, preferred)` in `engine/audio/audio_hrtf.{h,cpp}`. `AudioEngine::setHrtfMode` / `setHrtfDataset` / `getHrtfStatus` / `getAvailableHrtfDatasets` drive the ALC_SOFT_HRTF extension via `alcResetDeviceSOFT` + `alcGetStringiSOFT` (loaded via `alcGetProcAddress` — extension presence gated so drivers without it no-op). `Auto` mode defers to driver heuristics (headphone detection auto-enables); `Forced` requests unconditional HRTF (driver may still deny on surround output). 10 new headless unit tests cover label stability, HrtfSettings equality, empty-list / empty-preferred / exact-match / unknown-name / case-sensitivity resolution cases.
  - [x] Doppler effect for fast-moving sources — `DopplerParams` (`speedOfSound`, `dopplerFactor`) + pure-function `computeDopplerPitchRatio(params, srcPos, srcVel, listenerPos, listenerVel)` in `engine/audio/audio_doppler.{h,cpp}` matching the OpenAL 1.1 §3.5.2 formula `f' = f·(SS − DF·vLs)/(SS − DF·vSs)` with velocity projections clamped to [−SS/DF, SS/DF] so supersonic inputs saturate rather than explode. `AudioEngine::setDopplerFactor`/`setSpeedOfSound` push to OpenAL and stay in sync with the engine's CPU-side math; `AudioEngine::setListenerVelocity` + the new `playSoundSpatial(path, pos, velocity, params, volume, loop)` overload set `AL_VELOCITY` on the listener and per-source so OpenAL's native Doppler evaluation matches. `AudioSourceComponent` gains a `glm::vec3 velocity` field (zero default so stationary emitters cost nothing). 14 new unit tests cover zero-velocity/no-axis/disabled-factor pass-throughs, approach/recede sign conventions, perpendicular motion producing no shift, dopplerFactor scaling, and the supersonic clamp.
- [x] Audio occlusion and obstruction — material-based transmission + obstruction gain/low-pass model in `engine/audio/audio_occlusion.{h,cpp}`. `AudioOcclusionMaterialPreset` enum covers the 8 canonical materials (Air / Cloth / Wood / Glass / Stone / Concrete / Metal / Water) with transmission coefficients + low-pass amounts calibrated for first-person walkthroughs (Concrete blocks most, Cloth muffles least). `computeObstructionGain(openGain, transmission, fraction)` blends linearly between open path and fully transmitted path via `openGain · (1 − f · (1 − t))`; `computeObstructionLowPass(amount, fraction)` gives the matching EFX low-pass target. `AudioSourceComponent::occlusionMaterial` + `occlusionFraction` fields carry the raycaster's output into the audio system. Diffraction intentionally lives one layer up — the engine-side raycaster picks a secondary source position that hugs the diffraction edge and feeds that into the normal attenuation + obstruction path, keeping the pure-function layer blind to geometry for testability. 15 new unit tests cover label stability, preset ordering invariants (Concrete least transmissive, Cloth least muffling, all within [0, 1]), gain/low-pass blend math, and out-of-range clamping.
- [x] Reverb zones — per-room reverb presets + weight falloff + linear crossfade in `engine/audio/audio_reverb.{h,cpp}`. `ReverbPreset` enum ships 6 presets (`Generic` / `SmallRoom` / `LargeHall` / `Cave` / `Outdoor` / `Underwater`) adapted from Creative Labs EFX preset table — values stick to the non-EAX standard EFX subset that `AL_REVERB_*` properties consume. `ReverbParams` carries `decayTime` + `density` + `diffusion` + `gain` + `gainHf` + reflection/late delays. `computeReverbZoneWeight(radius, falloff, distance)` gives each zone a sphere-with-linear-falloff weight in [0, 1] — inside core = 1, linear decay across the band, 0 outside. `blendReverbParams(a, b, t)` component-wise lerps between two zones so the engine-side ReverbSystem can crossfade the highest-weighted zone against its nearest neighbour through thresholds (doorways, cave mouths). Auto-detection of geometry → decay time is deferred to the engine-side ReverbSystem (needs physics AABB). 13 new unit tests cover label stability, EFX range invariants, SmallRoom-shortest / Cave-longest decay + Underwater-strongest-HF-damp ordering, weight falloff (inside core, hard step at band=0, linear mid-band, negative clamps), and blend math (t=0/0.5/1 + out-of-range clamp).
- [x] Environmental ambient audio — 3 pure-function primitives in `engine/audio/audio_ambient.{h,cpp}` that the engine-side AmbientSystem composes. (1) `AmbientZone` (clipPath + coreRadius + falloffBand + maxVolume + priority) with `computeAmbientZoneVolume(zone, distance)` reusing the reverb-zone falloff profile so both subsystems share a single sphere-with-linear-falloff curve. (2) `TimeOfDayWindow` enum (Dawn/Day/Dusk/Night) + `TimeOfDayWeights` struct + `computeTimeOfDayWeights(hourOfDay)` — triangle-envelope mapping of a 24-hour clock to the four windows, normalised so weights always sum to 1.0 (crickets → birdsong crossfades continuously, no hard cuts at midnight). (3) `RandomOneShotScheduler` + `tickRandomOneShot(scheduler, dt, sampleFn)` — cooldown-based scheduler that draws fresh `[min, max]` intervals from an injected uniform-sample callback so tests stay deterministic; fires at most once per tick so a framerate stall can't avalanche one-shots. Weather-driven modulation (rain/wind/thunder) is deferred to the Phase 15 weather controller — no coupling in this module. 17 new unit tests cover zone volume (core / falloff / outside / clamp), time-of-day labels + weights-always-sum-to-one + per-peak-dominance + 24h-wrap + midnight-night-dominates, scheduler fire/cooldown, single-fire-per-tick cap, interval selection from sampler value, sampler clamp, null-sampler fallback, negative-dt no-op, and deterministic sampler-sequence fire chain.
- [x] Dynamic music system — three independent primitives in `engine/audio/audio_music.{h,cpp}` that the engine-side MusicSystem composes. (1) `MusicLayer` enum (Ambient / Tension / Exploration / Combat / Discovery / Danger) + `MusicLayerState` (currentGain, targetGain, fadeSpeedPerSecond) + `advanceMusicLayer(state, dt)` — per-layer slew toward `targetGain` without overshoot, so per-frame target pokes never produce audible zipper artifacts. (2) `intensityToLayerWeights(intensity, silence)` — maps a single [0, 1] gameplay signal to the per-layer mix via triangle envelopes with peaks at 0.00 (Ambient) / 0.25 (Exploration) / 0.50 (Tension + Discovery subtler bed) / 0.75 (Combat) / 1.00 (Danger). The `silence` parameter multiplicatively scales every layer so scripted quiet beats drop the full mix without disturbing the intensity routing. (3) `MusicStingerQueue` — FIFO queue with fixed capacity (DEFAULT_CAPACITY=8), push-newest / drop-oldest eviction so the latest event always wins; `advance(dt)` decrements delays and returns the stingers that fired this tick in FIFO order. 21 new unit tests cover layer labels, slew (reaches target / no overshoot / fades down / clamps / zero-delta no-op), intensity routing at every anchor point + blend midpoints + out-of-range clamp, silence scaling + uniformity across layers, and the stinger queue (fire after delay, FIFO multi-fire, capacity eviction, setCapacity trims in place, zero capacity rejects, clear, negative delta no-op).
- [x] Audio mixing and priorities — three pure-function primitives in `engine/audio/audio_mixer.{h,cpp}` that the engine-side AudioSystem composes. (1) `SoundPriority` enum (Low/Normal/High/Critical) + `soundPriorityRank` numeric ordering; (2) `AudioBus` enum (Master/Music/Voice/Sfx/Ambient/Ui) + `AudioMixer` struct with per-bus gains + `effectiveBusGain(mixer, bus)` returning `master * bus` clamped to [0, 1]; (3) `DuckingState` + `DuckingParams` (attackSeconds, releaseSeconds, duckFactor) + `updateDucking(state, params, dt)` — symmetric dB-distance slew toward `duckFactor` (triggered) / 1.0 (released), floor-clamped both ways; (4) `VoiceCandidate` (priority + effectiveGain + ageSeconds) + `voiceKeepScore` (priority·1000 + gain·10 − age, so priority dominates and age is a tiebreaker) + `chooseVoiceToEvict(voices)` picks the lowest keep-score for pool-pressure eviction. 19 new unit tests cover priority labels + monotonic ranks, bus labels + per-bus unity default + master multiplication + master-ignores-self-double + clamp, ducking attack/release/floor/unity caps + negative-dt no-op + zero-duration epsilon-guard, and eviction (empty sentinel, lower-priority-first, quieter-within-tier, oldest-within-tier, Critical-dominates, keep-score ordering).
- [x] Editor integration — `AudioPanel` in `engine/editor/panels/audio_panel.{h,cpp}` ships a four-tab editor surface over the Phase 10 audio pipeline: **Mixer** (per-bus gains for Master/Music/Voice/Sfx/Ambient/Ui + dialogue-duck trigger + attack/release/floor controls + live current-gain readout), **Sources** (iterates scene via `Scene::forEachEntity` picking `AudioSourceComponent`, per-entity mute/solo checkboxes + volume/pitch/min-max-distance sliders + attenuation model readout), **Zones** (reverb-zone add/remove/select with name + center + core radius + falloff band + preset combo; mirror placement surface for ambient zones with clipPath + priority), **Debug** (audio-availability indicator + distance model + Doppler factor + speed-of-sound + HRTF mode/status/dataset + available-dataset enumeration). Panel exposes `computeEffectiveSourceGain(entityId, bus)` — mute beats solo, solo-exclusive routing when any source soloed, otherwise `master · bus · duckGain` clamped to [0, 1]. Registered via `Engine::initialize` → `Editor::setAudioSystem(m_systemRegistry.getSystem<AudioSystem>())` + drawn each editor frame alongside NavigationPanel. 18 headless unit tests cover defaults, open/close toggle, zone add/remove/selection-shift, mute/solo state, effective-gain routing, and overlay toggle.

### Camera Modes
*Moved to Phase 10.8 — see that phase for the full list. Retained here as a pointer so anyone reading Phase 10 knows where the section went.*

### Localization
- [ ] Multi-language text support (UTF-8, language selection)
- [ ] Translatable string table system (all UI/plaque text referenced by key, not hardcoded)
- [ ] Hebrew, Greek, and Latin text rendering (right-to-left support for Hebrew)
- [ ] Language selection in settings menu

### Accessibility
- [x] Colorblind modes (Deuteranopia, Protanopia, Tritanopia LUT modes applied post-tonemap — ref: IGDA GA-SIG GDC 2026 roundtable) — `ColorVisionMode` enum + `colorVisionMatrix()` lookup in `engine/renderer/color_vision_filter.{h,cpp}`. Canonical Viénot/Brettel/Mollon 1999 3×3 RGB simulation matrices. `Renderer::setColorVisionMode` feeds `u_colorVisionEnabled` + `u_colorVisionMatrix` to `screen_quad.frag`, applied between the artistic LUT and the sRGB gamma stage. Identity is a fast path (no multiply when Normal). 12 new unit tests.
- [x] Subtitle / closed caption system for spatial audio cues, with size presets (Small / Medium / Large / XL) — `SubtitleQueue` (FIFO with per-tick countdown, push-newest/drop-oldest on overflow, default cap 3 per BBC / Romero-Fresco caption guidelines) + `Subtitle` / `ActiveSubtitle` structs + `SubtitleCategory` enum (Dialogue / Narrator / SoundCue) + spatial `directionDegrees` field in `engine/ui/subtitle.{h,cpp}`. `SubtitleSizePreset` ladder (Small 1.00× / Medium 1.25× / Large 1.50× / XL 2.00×) mirrors `UIScalePreset` and composes with it. Headless core; rendering slice is deferred pending audio-event wiring. 17 new unit tests.
- [x] Fully remappable controls (keyboard, mouse, gamepad) — action-map architecture (Unity Input System / Unreal Enhanced Input / Godot InputMap pattern). `engine/input/input_bindings.{h,cpp}` ships `InputDevice` enum (None / Keyboard / Mouse / Gamepad), `InputBinding` with factory helpers + equality + `isBound()`, `InputAction` (id / label / category + primary / secondary / gamepad slots + `matches()`), and `InputActionMap` (insertion-order registry with parallel defaults snapshot; `findAction` / `findActionBoundTo` / `findConflicts(binding, excludeSelfId)` / per-slot setters / `clearSlot` / `resetToDefaults` / `resetActionToDefaults`). `bindingDisplayLabel(binding)` returns a readable name for every GLFW key / mouse button / gamepad button. Pure-function `isActionDown(map, id, bindingChecker)` is the query path; `InputManager::isBindingDown` + `isActionDown` are thin GLFW shims that poll every connected gamepad slot. 30 new unit tests. Persistence (JSON save/load) + actual gamepad polling wiring to game code are follow-ups.
- [x] UI scaling presets (1.0× / 1.25× / 1.5× / 2.0×) — `UIScalePreset` enum + `UITheme::withScale(factor)` pure transform applied via `UISystem::setScalePreset`. Multiplies every pixel-size field (buttons, sliders, checkboxes, dropdowns, keybinds, type sizes, crosshair, focus ring, panel borders) while leaving palette + motion timing untouched. Rebuild is idempotent and composes with high-contrast mode.
- [x] High-contrast mode for UI elements — `UITheme::withHighContrast()` pure transform applied via `UISystem::setHighContrastMode`. Pure-black surfaces, pure-white primary text, full-alpha strokes, saturated amber accent. Sizing stays under scale-preset control so users can compose 2.0× scale + high-contrast.
- [x] Reduced-flashing / photosensitivity safe mode (caps camera shake, strobes, muzzle-flash alpha) — `PhotosensitiveLimits` struct + pure clamp helpers (`clampFlashAlpha`, `clampShakeAmplitude`, `clampStrobeHz`, `limitBloomIntensity`) in `engine/accessibility/photosensitive_safety.{h,cpp}`. Caps grounded in WCAG 2.2 SC 2.3.1, Epilepsy Society guidance, and IGDA GA-SIG / Xbox / Ubisoft accessibility bullets (max flash α 0.25, shake×0.25, ≤ 2 Hz strobe, bloom×0.6). `UITheme::withReducedMotion()` pure transform zeroes `transitionDuration`; `UISystem::setReducedMotion` composes with scale + high-contrast in `rebuildTheme`. Identity pass-through when disabled. 18 new unit tests (13 clamp, 5 UI composition).
- [x] Depth-of-field toggle (off by default in accessibility preset) — `PostProcessAccessibilitySettings::depthOfFieldEnabled` in `engine/accessibility/post_process_accessibility.{h,cpp}`. Defaults to `true` (normal visual quality); `safeDefaults()` one-click accessibility preset flips it to `false`. The DoF effect itself lands in the Phase 10 Post-Processing Effects Suite — it will consult this flag on the day it merges, so user preferences survive the feature's arrival.
- [x] Motion-blur toggle (off by default in accessibility preset) — `PostProcessAccessibilitySettings::motionBlurEnabled`. Same pattern as DoF (default on, `safeDefaults()` flips off). WCAG 2.2 SC 2.3.3 + Game Accessibility Guidelines "Avoid motion blur; allow it to be turned off". 5 unit tests cover default state, safe-preset distinctness, equality, and per-field independence.
- [x] Screen-reader friendly UI labels (ARIA-like semantic tags on ImGui widgets where feasible) — `UIAccessibleRole` enum (Button / Checkbox / Slider / Dropdown / KeybindRow / Label / Panel / Image / ProgressBar / Crosshair / Unknown) + `UIAccessibleInfo` (role + label + description + hint + value) in `engine/ui/ui_accessible.{h,cpp}`. Every `UIElement` carries `m_accessible`; every widget sets its role in its constructor. `UIElement::collectAccessible()` walks the tree, skips hidden subtrees entirely, and emits a flat `UIAccessibilitySnapshot` list for a future TTS bridge. `UICanvas::collectAccessible()` returns the canvas-wide enumeration. 13 new unit tests. Scope note: the in-game `UIElement` tree is covered; ImGui editor widgets are a separate surface and are deferred — they need per-widget label attachment at call sites rather than per-type constructor-set roles.

### Decal System
*Moved to Phase 10.8 — see that phase for the full list.*

### Post-Processing Effects Suite
*Moved to Phase 10.8 — see that phase for the full list, including the tonemapping policy note.*

### Rendering Enhancements
- [ ] Subsurface scattering (SSS) — light transmission through thin/translucent materials
  - Per-material SSS parameters: thickness, transmission color, scattering distance
  - Wrap lighting model for thin surfaces (curtains, fabric, leaves, candle wax)
  - Thickness map support (variable transmission across a surface)
  - The Tabernacle's dyed linen curtains should glow softly when sunlight hits the exterior
  - Fast approximation: pre-integrated skin/fabric BRDF lookup (no ray marching needed)
- [ ] Screen-space global illumination (SSGI) — real-time dynamic indirect light
- [ ] **Motion vectors from geometry pass via MRT.** Today the renderer uses a per-object overlay pass that re-renders opaque geometry after the full-screen camera-motion pass with per-draw `u_model` / `u_prevModel` matrices (`assets/shaders/motion_vectors_object.{vert,frag}.glsl`). This fixes rigid-body TAA ghosting but re-draws every opaque item. Cleaner: emit motion vectors directly from the main scene pass via an MRT attachment (RG16F motion buffer alongside the HDR color target) and drop the overlay pass. Must also handle skinned and morph-target meshes — currently those fall through to rigid-body motion only, which undershoots by the animation delta. Prerequisite for correct TAA + FSR 2.x on any content with animated characters. Once the previous-frame normal buffer is retained alongside motion vectors, `V_mask = α(1 − n_cur · n_prev)` (nVidia GDC 2024 "rain puddles" technique) is a cheap complementary rejection signal — catches animated surfaces where motion vectors agree but the surface is different (undulating water, foliage, animated decals). See Phase 10.8 Decal System for the decal-side consumer.

### Fog, Mist, and Volumetric Lighting
- [x] Distance fog (linear, exponential, exponential-squared) — pure-function primitives shipped in `engine/renderer/fog.{h,cpp}`. `FogMode` enum (`None` / `Linear` / `Exponential` / `ExponentialSquared`) + `FogParams` (linear-RGB colour, start, end, density). `computeFogFactor(mode, params, distance)` implements the three canonical forms: Linear `(end-d)/(end-start)`, GL_EXP `exp(-density·d)`, GL_EXP2 `exp(-(density·d)²)` — returns *surface visibility* in [0,1], matches OpenGL Red Book §9 / D3D9 fog-formulas. Guards every degenerate param (zero span, negative density, sub-camera distance) with pass-through behaviour. 15 unit tests cover knees, monotonicity, and edge cases.
- [x] Height fog — exponential fog that thickens below a configurable altitude (ground-hugging mist, valley fog). `HeightFogParams` (colour, fogHeight, groundDensity, heightFalloff, maxOpacity) + closed-form `computeHeightFogTransmittance(params, cameraY, rayDirY, rayLength)` — Quílez 2010 analytic integral of `d(y) = a·exp(-b·(y - fogHeight))` along a view ray. Uses `expm1` for numerical stability near horizontal rays; separate `|rd.y| < 1e-5` branch collapses to Beer-Lambert so the horizon line stays smooth. `maxOpacity` clamp mirrors UE `FogMaxOpacity` so the sky doesn't fully vanish on long sightlines. 7 unit tests cover zero-length, zero-density, monotonic decay, horizontal ↔ Beer-Lambert equivalence, altitude thinning, maxOpacity floor, small-angle ↔ horizontal-branch agreement. **Desert heat haze** variant (subtle distortion) is a follow-up.
- [x] Sun inscatter lobe — directional brightening toward the sun (UE "DirectionalInscatteringColor" pattern). `SunInscatterParams` (colour, exponent, startDistance) + `computeSunInscatterLobe(params, viewDir, sunDir, viewDistance)` evaluates `pow(max(dot(viewDir, -sunDir), 0), exponent)` with zero below startDistance and zero on backlit rays. 5 unit tests. Enables the "haze glow at sunset" look without committing to volumetric fog's compute-shader cost.
- [x] **Fog composite shader integration** — the three non-volumetric fog primitives (distance, height, sun inscatter) are now wired into `assets/shaders/screen_quad.frag.glsl` and composed in linear HDR after contact shadows and before bloom add (matches `docs/PHASE10_FOG_DESIGN.md` §4 HDR composition order, UE / HDRP convention — bloom must sample fogged radiance). GPU path reconstructs world-space hit position from the reverse-Z depth buffer via `u_fogInvViewProj`; sky pixels (depth == 0) skip fog so the skybox colour survives. CPU composite helper `composeFog(surfaceColour, FogCompositeInputs, worldPos)` in `fog.{h,cpp}` mirrors the GLSL byte-for-byte so the CPU spec-test pins the GPU path. Renderer setters (`setFogMode` / `setFogParams` / `setHeightFogEnabled` / `setHeightFogParams` / `setSunInscatterEnabled` / `setSunInscatterParams`) drive the new state; `m_cameraWorldPosition` is cached each `renderScene()` so the composite doesn't need a second camera traversal. Depth sampler on unit 12 (shared with SSAO / contact-shadow passes, re-bound in the composite for Mesa declared-sampler safety). 7 new `FogComposite` unit tests cover identity-when-disabled, far-end gives fog colour, near-camera gives surface, sun-inscatter warps distance-fog colour, height-fog fully obscures surface when dense, two-layer 50/50 composition algebra, and zero-view-distance pass-through.
- [x] **Fog accessibility transform** (slice 11.9) — pure-function `applyFogAccessibilitySettings(authored, PostProcessAccessibilitySettings) → effective` in `fog.{h,cpp}` now consumes the previously-unwired `fogEnabled` / `fogIntensityScale` / `reduceMotionFog` flags from `PostProcessAccessibilitySettings` and maps them to per-layer adjustments: master disable beats every other flag; intensity scales EXP/EXP2 density linearly, pushes Linear `end` outward (collapsing to `FogMode::None` at scale ≤ 1e-3 to avoid divide-by-zero), scales height-fog `groundDensity` + `maxOpacity` together, and dims the sun-inscatter lobe *colour* (not exponent — the lobe shape stays authored); reduce-motion halves the lobe colour further, matching WCAG 2.2 SC 2.3.3 / Xbox AG 117 photosensitivity guidance. `Renderer` stores `m_postProcessAccessibility` and runs the transform each frame between authored state and GPU uniform upload — authored parameters are never mutated so users can toggle accessibility without losing their scene-authored look. 12 new `FogAccessibility` unit tests pin every flag (master disable, each intensity scale case, sun-inscatter colour vs exponent invariant, reduce-motion vs frame-static layers, end-to-end via `safeDefaults()`, flag-precedence guard).
- [ ] Volumetric fog — ray-marched participating media with light scattering
  - Froxel-based volume (frustum-aligned 3D texture, typically 160x90x64)
  - Temporal reprojection for stable, low-cost accumulation across frames
  - Per-light volumetric contribution (directional sun + point/spot lights)
  - Fog density noise (3D Perlin/Worley) for non-uniform, natural-looking fog
  - Artist controls: density, anisotropy (forward/back scatter), albedo, extinction
- [ ] Volumetric god rays / crepuscular rays — visible light shafts from the sun through openings
  - Screen-space radial blur approach (fast, good for single dominant light)
  - Integration with volumetric fog for physically-based light shafts
  - The Tabernacle's tent entrance should cast visible light beams into the dusty interior
  - God rays through gaps in clouds (when volumetric clouds are available from Phase 15)
- [ ] Mist / ground fog — localized fog volumes placeable per-entity
  - Box and sphere fog volumes with soft edges (density falloff at boundaries)
  - Animated density (slow turbulence for rolling mist effect)
  - Use case: morning mist around the Bronze Laver, dust clouds near the altar

### Milestone
A complete walkthrough experience with UI, spatial audio with occlusion and dynamic music, information displays, multi-language support, accessibility options, decals, cinematic post-processing, and advanced material rendering (SSS, volumetric fog, god rays).

---

## Phase 10.5: Editor Usability Pass
**Goal:** Make the editor genuinely usable for people who have never opened it — both solo creators working without AI help and AI-assisted users who need the editor to meet them halfway.

Phases 5A–9D have built an extensive editor (entity inspector, scene viewport, model / texture / HDRI viewers, navigation panel, audio panel, formula workbench, node graph editor). The surface is wide, but discoverability, workflow ergonomics, and first-run experience haven't had a dedicated pass. This phase is where the editor becomes something a new user can open and get useful work done in without reading the source code first.

**Scope principle:** no new major features. Every item is about *making existing functionality findable, obvious, and fast*. If a proposed item would add a feature rather than polish one, it belongs in another phase.

### Discoverability — "where is the thing?"
- [ ] Command palette (Ctrl+Shift+P / Cmd+Shift+P) — fuzzy search over every menu item, panel, tool, and action with keyboard-only navigation. One surface to find anything without memorising menus.
- [ ] "What can I do here?" contextual help overlay (F1 over any viewport / panel shows a tooltip of its purpose + the keyboard shortcuts + the 5 most-used actions).
- [ ] Searchable settings / preferences (the accessibility + input + graphics preferences are scattered; a single Ctrl+, search-over-keys dialog surfaces them).
- [ ] Panel launcher with pinning — first-run shows a pinnable "Most-used panels" strip so the common Window submenu discovery isn't a memorisation exercise.
- [ ] In-editor glossary — hover any domain term (e.g. "navmesh", "froxel", "cascade", "IBL") to see a 1-2 sentence definition + link to the relevant docs page.

### Onboarding — "I just installed this; now what?"
- [ ] First-run welcome dialog — project template picker (empty / 3D first-person / 2.5D / isometric / biblical walkthrough / blank biblical template) with live preview thumbnails. Already partially covered by Phase 9D template system; this adds the first-run wrapper.
- [ ] Guided tour (dismissable, resumable) — highlights the 6 things a new user needs to know: viewport navigation, panel layout, entity inspector, assets panel, save/load, play mode. ~2 minutes total, spatially anchored callouts.
- [ ] "Next step" hint pane — surfaces the single most useful next action given the current scene state ("scene has no lighting — add a directional light?", "entity has no collider — add a box collider?"). Dismissable per-project.
- [ ] Sample scenes shipped with the engine — each template project opens with a minimal scene that shows the system working, not a blank viewport.
- [ ] Opt-in telemetry (local file only, never uploaded) — tracks which panels the user opens so the first-run tour can adapt.

### Workflow ergonomics — "I know what to do; make it fast"
- [ ] Keyboard-driven workflow parity — every mouse action in the scene viewport should have a keyboard shortcut (move, rotate, scale, duplicate, delete, parent, frame-selected, toggle-gizmo-space).
- [ ] Chord shortcuts (Ctrl+K Ctrl+S, Leader-style) for advanced actions — matches VSCode/Emacs user expectations and avoids key-space starvation.
- [ ] Undo/redo everywhere, not just scene edits — panel layouts, preference changes, asset operations, workbench fits, shader parameters. Phase 9 started this; polish across remaining surfaces.
- [ ] Unified copy/paste for entities, components, materials, and node-graph subgraphs — same shortcuts, same clipboard.
- [ ] Drag-and-drop across every panel (model viewer → scene, texture viewer → material slot, HDRI viewer → environment, asset panel → entity component).
- [ ] Multi-select editing — select N entities, edit one component field, apply to all.
- [ ] Auto-save + recovery — every N minutes, on window lose-focus, on process crash. File-based, not memory-only.
- [ ] Project-relative paths — the editor must never bake absolute paths into saved scenes (moving a project directory shouldn't break it).

### Tooltips & contextual help — "what does this do?"
- [ ] Every widget has a tooltip with a 1-sentence description + the keyboard shortcut if any. Audit every panel.
- [ ] Status-bar hints — hovering a menu item shows the full description + link to the relevant docs page.
- [ ] Inline warnings on invalid inputs (red outline + explanation of what's wrong + "fix" button where possible). Already partial in the Formula Workbench; extend to every numeric / enum / path input across the editor.
- [ ] "Why is this greyed out?" — right-click a disabled control and get the reason ("Bake button disabled: no navmesh geometry tagged in the scene").

### AI assistance integration hooks
- [ ] Editor-exposed command API — every menu action, panel operation, and entity mutation callable via a stable string-based command ID. Same surface as the command palette. Enables AI assistants to drive the editor without hooking directly into C++ headers.
- [ ] Scene-state snapshot / diff format — JSON serialisation of the current scene suitable for feeding to an AI assistant as context (entity list, component values, current selection). Already exists partially via scene serialisation; this formalises the contract.
- [ ] AI chat panel (optional, off-by-default) — hostable via any Claude API / Anthropic SDK key in the user's environment. Does NOT require an AI vendor to use the editor; the editor is fully functional without it. Matches the Rule "editor must be usable without AI".
- [ ] Prompt templates stored per-project — "add a point light near the selected entity", "generate 5 variations of this material", "create a character controller for this mesh". Users without AI get a "prompt library" of suggested workflows as a discovery aid.
- [ ] Keyboard-driven agent invocation (Alt+Enter) — text box where the user types a short description, agent interprets → proposes a diff → user accepts / rejects. Deferred until there's a stable command API.

### Performance & responsiveness — "the editor shouldn't feel slow"
- [ ] Audit every editor panel's per-frame cost; target < 1 ms per panel at idle. The editor overlay should never push the scene-viewport frame time below 60 FPS on target hardware.
- [ ] Async asset imports — model / texture / HDRI imports must not block the editor UI. Progress shown in a status bar.
- [ ] Incremental scene saves — a 10,000-entity scene shouldn't take 2 seconds to save. Chunk by dirty regions.
- [ ] Panel-level collapse to single line — users rarely need all 30 panels visible; aggressive one-line collapse of inactive panels reclaims screen space.

### Accessibility (editor-side, complementing the engine-side Phase 10 work)
- [ ] Editor UI scaling presets (independent of the game UI scaling shipped in Phase 10) — 1.0× / 1.25× / 1.5× / 2.0×.
- [ ] Editor high-contrast mode — mirrors the game UI high-contrast toggle but applied to ImGui panels, menu chrome, and the scene viewport gizmos.
- [ ] Screen-reader labels on every ImGui widget (extends the Phase 10 `UIAccessibleRole` enum to the editor surface — currently only the in-game `UIElement` tree is covered).
- [ ] Colourblind-safe gizmo / wireframe / selection palettes.
- [ ] Keyboard-only workflow — verify every panel is navigable with Tab / Shift+Tab / Enter / Space and no mouse. This is the big-ticket item from partially-sighted / motor-impaired users.

### Docs surface — "how do I learn this?"
- [ ] **In-editor help browser** — dedicated `Help → Documentation` panel (or `F1` shortcut) that renders the shipped docs inline as a searchable, navigable markdown viewer. No external browser needed. Contents:
  - Every markdown file under the engine's `docs/` directory (auto-indexed by `.md` filename; shipped alongside the engine binary in a `help/` resource folder so offline users still get the full manual).
  - Tree-of-contents sidebar grouped by subsystem (rendering / physics / audio / UI / scripting / editor / settings / …).
  - Full-text search (ripgrep-style fuzzy match over all loaded docs).
  - Back / forward history + bookmarks, matching browser-style navigation muscle memory.
  - External-link behaviour: anchor tags with `http(s)://` open in the system browser; internal `docs/foo.md` anchors navigate within the panel.
- [ ] **Context-sensitive "Help with this" affordance** — every panel, widget group, and major editor feature exposes a small `?` or `Help` button that deep-links into the in-editor help browser at the exact topic for that feature. Examples:
  - `?` next to the Environment Panel's foliage-brush group → opens `docs/PHASE5G_DESIGN.md` at the `## Foliage brush` anchor.
  - `?` on the Formula Workbench's Levenberg-Marquardt solver tab → opens `docs/FORMULA_WORKBENCH_SELF_LEARNING_DESIGN.md` at the convergence-criteria section.
  - `?` on the Settings editor's Photosensitive Safety section → opens the Phase 10 accessibility design doc at that section.
  - `?` on any menu item in the main menu bar → opens the docs page that documents it.
  - Shortcut: hovering any widget and pressing `F1` does the same thing as clicking its `?` button.
- [ ] **Help anchor registry** — a central map (widget id / feature id → `docs/file.md#anchor`) so `Help with this` buttons across the editor always resolve consistently. Source of truth for which docs page owns which feature; rebinding a help anchor is a one-line change rather than hunting through ImGui code. Editor-internal; never exposed to user-facing strings.
- [ ] **Help authored inline when it lives alongside code** — every new subsystem ships with a matching `docs/<SUBSYSTEM>.md` page following a template (Overview / When to use it / How to use it in the editor / API reference / Gotchas). Post-phase audit checks that every user-facing feature has a help anchor pointing somewhere non-empty.
- [ ] Video / GIF embeds in panel tooltips for complex operations (one-shot 3-5 second MP4s).
- [ ] Troubleshooting decision tree — "my scene doesn't render" → click-through diagnostic ending in a fix or a GitHub-issue prefill.

### Code-hygiene tooling — stop the clang-tidy whack-a-mole
- [ ] **Principled clang-tidy baseline.** Replace the current `bugprone-*,performance-*,modernize-*,readability-*,cppcoreguidelines-*` wildcard (with its ~8 excludes accumulated across audit tool 2.15.0 → 2.17.0) with an explicit enumeration of the ~30 checks the project actually wants active. Commit as a project-level `.clang-tidy` file so IDE / editor integrations pick it up too. Ends the "suppress the top rule, next layer appears" pattern documented in audit tool 2.15–2.17 CHANGELOG entries.
- [ ] **Bulk clang-format sweep.** Whole-codebase `clang-format -i` pass against the existing `.clang-format` config (which has been deliberately deferred per the file's own header comment). Land in a single dedicated commit so git blame damage is contained to one reviewable unit. Re-enables `readability-braces-around-statements` once the unbraced sites are cleaned up by this pass or by the hybrid-adoption path (CODING_STANDARDS.md §3).

### Milestone
A person who has never opened Vestige can open it, follow the first-run tour, create a scene, place a few entities, bake a navmesh, export a build — all without reading source code, watching a tutorial, or asking anyone. AI-assisted users get the same surface plus an optional chat panel. Keyboard-only users can drive every action without a mouse. The editor feels responsive even with 10k-entity scenes.

---

## Phase 10.7: Accessibility + Audio integration ✅ **Complete (2026-04-23)**
**Goal:** Retrofit existing audio/accessibility consumers so they read from the engine-owned Settings stores introduced in Phase 10 slice 13.5e. Phase 10 delivered the Settings UI + the store-sink plumbing; this phase closes the loop by making every effect consumer actually consult those stores at runtime.

**Completed 2026-04-23** across 8 commits (design + 3 subtitle + 1 photosensitive + 3 audio). All approved milestones delivered. Camera shake and flash overlay retrofits remain deferred to Phase 11 per the scope reduction captured below — their originating subsystems do not exist in the codebase today. `docs/PHASE10_7_DESIGN.md` contains the full design + slice breakdown.

**Context:** Slice 13.5e added `Engine::m_audioMixer`, `Engine::m_subtitleQueue`, `Engine::m_photosensitiveLimits` + `m_photosensitiveEnabled`. Settings drives these live via sinks, but downstream consumers don't read from them yet — user changes propagate to the store but not to the audio path / effect call sites.

### Audio mixer → playback path
- [x] Pipe `Engine::getAudioMixer()` into the OpenAL gain resolution on every `AudioSource` so the user's per-bus gain choice actually affects playback. **A2** — playback registry in `AudioEngine` + per-frame `updateGains()` sweep + `resolveSourceGain(master × bus × source)` pure helper (commit `0bac0e6`).
- [x] Decide bus-gain multiplication order: master × bus × source. Documented in `docs/PHASE10_7_DESIGN.md` §4.1.
- [x] Editor-side `AudioPanel` currently owns a standalone `AudioMixer`. **A3** — panel now wires into the engine mixer via `wireEngineMixer`; bus-slider edits route through `SettingsEditor::mutate`; mute / solo / ducking stay panel-local (commit `525c182`).

### Subtitle rendering
- [x] Wire `Engine::getSubtitleQueue().tick(dt)` into the per-frame game loop. **B1** — `AccessibilityTick` profiler scope in `Engine::run` (commit `593fc2f`).
- [x] Render `queue.activeSubtitles()` through the HUD pipeline with configured size preset + per-category styling. **B2** — pure-function layout + `SpriteBatchRenderer` plates + `TextRenderer` glyph runs, wired through `UISystem::renderUI` as the last overlay pass (commit `738b106`).
- [x] Wire audio-event triggers to enqueue captions. **B3** — declarative `assets/captions.json` map loaded at engine init; `CaptionMap::enqueueFor(clipPath, queue)` fires a caption when a mapped clip plays (commit `be7cdae`). Auto-trigger on playback arrives in Slice A2's component-driven path when game code adopts the new per-frame gain pass.

### Photosensitive caps → consumers

**Scope reduction (2026-04-23).** Of the 4 consumers originally listed, only bloom and strobe/flicker have real consumers in the codebase today. Camera shake and flash overlay subsystems do not exist yet — the clamp helpers (`clampShakeAmplitude`, `clampFlashAlpha`) sit unused. Phase 10.7 retrofits the 2 that exist; the other 2 are *deferred retrofits* that Phase 11 (combat / UI transitions) must wire into the originating subsystems as part of their initial implementation. See `docs/PHASE10_7_DESIGN.md` §4.3 for rationale.

- [x] Retrofit bloom post through `limitBloomIntensity`. **C1** — `Renderer::setPhotosensitive` setter + clamp at the `u_bloomIntensity` upload site; engine pushes state per-frame in `AccessibilityTick` (commit `4d94cd4`).
- [x] Retrofit strobe / flicker emitters through `clampStrobeHz`. **C2** — `ParticleEmitterComponent::getCoupledLight` converts `flickerSpeed` to Hz (`/ 2π`), clamps, converts back; `Scene::collectRenderData` threads photosensitive state through (commit `4d94cd4`).
- [ ] *(Deferred to Phase 11)* Retrofit camera shake through `clampShakeAmplitude` when the shake subsystem ships. Affects kickback, explosions, earthquake triggers.
- [ ] *(Deferred to Phase 11)* Retrofit flash overlays (hit flashes, screen-wipe transitions) through `clampFlashAlpha` when the overlay subsystem ships.

### Slice plan (approved 2026-04-23, order B → C → A)

Each slice is a review-sized commit with tests + CHANGELOG entry. B and C are independent; A depends on neither.

- **Slice B — subtitles:** `SubtitleQueue::tick(dt)` in `Engine::update` (B1); 2D HUD render pass for `activeSubtitles()` (B2); declarative `assets/captions.json` map + auto-enqueue on clip playback (B3).
- **Slice C — photosensitive:** bloom intensity retrofit via `Renderer::setPhotosensitive` setter (C1); particle flicker retrofit via `clampStrobeHz` at emitter tick (C2).
- **Slice A — audio:** `AudioBus` field on `AudioSourceComponent` + serializer (A1); `AudioSystem` per-frame gain-resolution pass pushing `master × bus × source` to `AL_GAIN` (A2); `AudioPanel` unification — bus sliders route through `SettingsEditor`, mute/solo/ducking stay panel-local (A3).

### Milestone
Toggling any Settings tab option (bus gain, subtitle size, photosensitive safe mode, HRTF) produces an immediately-observable change in running game state — not just in the store. Every effect consumer reads from `Engine` getters rather than hard-coded values or its own parameter struct. No "set in Settings, nothing happens" gap remains.

---

## Phase 10.8: Rendering & Camera Prerequisites
**Goal:** Complete the Phase 10 rendering and camera subsystems that Phase 11B depends on. This phase was split out of Phase 10's tail bullets to make the dependency chain explicit: Phase 11B combat, damage feedback, and vehicle work cannot build without these.

**Note:** The three sections below — Camera Modes, Decal System, Post-Processing Effects Suite — were previously bullets in Phase 10. They have been lifted into their own phase so the ordering is enforced rather than implicit. Phase 10's remaining bullets (in-game UI, text rendering, audio, localization, accessibility, fog, rendering enhancements) stay in Phase 10; they have either landed or can land in parallel with Phase 11A infrastructure work without blocking it.

**Cross-phase prerequisite note.** Three items originally planned inside Phase 10.8 — `PhysicsWorld::sphereCast` (CM3 / CM4 third-person wall-probe), centripetal Catmull-Rom parameterisation and arc-length spline evaluation (CM7 cinematic camera) — moved to Phase 10.9 Slices 7 + 10. Reason: the 2026-04-23 ultrareview surfaced that the existing uniform-Catmull-Rom implementation contradicts the research doc, and `sphereCast` is a more general primitive than a single-consumer Phase 10.8 slice. They land in Phase 10.9 with their tests, and Phase 10.8 consumes them rather than authoring them. CM1 (base types) already shipped 2026-04-23; CM2 begins after Phase 10.9 Slices 1 + 3 land.

### Camera Modes
- [ ] Camera mode system (switchable projection and control schemes per scene/game)
- [ ] First-person camera (existing — WASD + mouse look, perspective projection)
- [ ] Third-person camera (follow entity with orbit controls, perspective projection)
- [ ] **Runtime first-person ↔ third-person toggle** — single input binding (default `V`, matching Skyrim / GTA / Minecraft convention) that swaps the active CameraComponent between the player entity's first-person rig and the third-person follow rig without teardown. Shared entity / input / HUD state; only the camera source changes. Configurable: which input action triggers it, whether the toggle is allowed during combat / dialogue / vehicle mode, and per-scene opt-out for first-person-locked walkthroughs (e.g. biblical content). Smooth transition optional (lerp over 150 ms) or instant; respects `AccessibilitySettings.reducedMotion`. Edge case: third-person interior cameras clip walls — mitigate with a physics sphere-cast on the boom arm (standard third-person pattern) before hitting this phase's implementation.
- [ ] Isometric camera (fixed-angle orthographic projection, click-to-move input, Diablo-style)
- [ ] Top-down camera (overhead orthographic, suitable for strategy or map views)
- [ ] Cinematic camera (spline-based flythrough for guided tours and cutscenes) — splines already shipped (`engine/utils/catmull_rom_spline.{h,cpp}`, `engine/environment/spline_path.{h,cpp}`).

### Decal System
Projected textures for blood splatters, scorch marks, claw scratches, bullet holes, and environmental storytelling.
- [ ] Deferred decal rendering (project decal texture onto G-buffer geometry)
  - Box-projected decals (oriented bounding box defines projection volume)
  - Decals modify albedo, normal, roughness, and/or metallic channels
  - Depth-tested to prevent projection onto distant surfaces
- [ ] Persistent decals that accumulate during gameplay
  - Decal pool with maximum count (oldest decals fade when limit reached)
  - Configurable lifetime and fade curve per decal type
- [ ] Animated decals (spreading blood pools, flickering holograms, growing cracks)
  - UV animation (scrolling, scaling over time)
  - Sprite sheet frame animation for complex effects
- [ ] Decal presets: blood splatter, bullet hole, scorch mark, claw scratch, footprint, water stain
- [ ] Editor integration — place and orient decals in editor, preview projection volume
- [ ] **Normal-disocclusion mask for decals** (nVidia GDC 2024, rain puddles). Compute `V_mask = α(1 − n_current · n_previous)` (previous-frame normal fetched via motion vectors) and modulate decal contribution by `V_mask` so animated decals don't streak across disocclusion boundaries. Useful beyond rain puddles — any animated surface detail (flowing water on stone, blood spread over time, cracks propagating). Depends on the MRT motion-vector work in Phase 10 "Rendering enhancements" / the previous-frame normal-buffer retention we'd want anyway for TAA. Reference pattern:

  ```
  prev_n  = sampleNormal(prev_normal_buf, uv - motion_uv);
  v_mask  = clamp01(alpha * (1.0 - dot(n_current, prev_n)));
  decal  *= (1.0 - v_mask);  // or use as blend weight for decal history
  ```

### Post-Processing Effects Suite
Cinematic and atmospheric post-processing for horror, drama, and stylized rendering.

**Tonemapping policy:** ACES 1.3 remains the default (matches current `HDR rendering and tone mapping` item from Phase 4). ACES 2.0 is opt-in only — the 2.0 committee prioritised SDR compatibility over HDR gamut usage, which desaturates HDR highlights by design (ref: Tarpini / Gilbert commentary, March 2026 — https://daejeonchronicles.com/2026/03/11/aces-2-0-we-are-in-for-some-further-years-of-bad-hdr/). Author any custom exposure / highlight curves via the Formula Workbench.

- [ ] ACES 2.0 tonemap option (opt-in alternative to the ACES 1.3 default; kept side-by-side for HDR output, user-selectable)
- [ ] Film grain (noise overlay, configurable intensity and grain size)
- [ ] Chromatic aberration (RGB channel offset, stronger at screen edges)
- [ ] Vignette (screen edge darkening, configurable intensity and radius)
- [ ] Screen distortion (barrel/pincushion, heat haze, damage effects)
- [ ] Damage/low-health screen effects (red vignette, blur, heartbeat pulse, desaturation) — consumed by Phase 11B Health & Damage feedback.
- [ ] Death screen effects (slow desaturation, blur ramp, fade to black) — consumed by Phase 11B Health.
- [ ] Color grading per-scene (warm golden interiors, cold blue corridors, sickly green toxic areas)
- [ ] Lens flare (anamorphic streaks and ghost artifacts from bright lights)
- [ ] Sharpen filter (contrast-adaptive sharpening for post-upscale clarity)

### Milestone
Every Phase 11B gameplay hook that depends on rendering (hit decals, bullet-hole decals, damage screen effects, death fade, vehicle cameras, camera-mode switching) has a working sink. Phase 11A infrastructure can land in parallel; Phase 11B cannot ship until both 10.8 and 11A are complete.

**Cross-phase prerequisite note.** Phase 10.9 Slice 7 ships `PhysicsWorld::sphereCast`, Slice 10 ships centripetal Catmull-Rom + arc-length-parameterised spline evaluation. Both are consumed by Phase 10.8 CM3 / CM4 / CM7 and were originally scheduled inside this phase; they move to Phase 10.9 so the remediation sweep can land them alongside their test coverage rather than under Phase 10.8's slice count.

---

## Phase 10.9: Post-Ultrareview Remediation
**Goal:** Close findings from the 2026-04-23 independent multi-agent code review (14 subsystems, 14 independent reviewers, each given the design docs + source but *not* the tests — deliberately breaking the "self-marking homework" loop that let several Phase 10.7 features ship with passing tests but silently-broken behaviour). **Second /indie-review on 2026-04-23 (12 reviewers, post-F1–F5)** added Slices 14–17 (scripting/formula safety, audio ergonomics, shader parity relabel, cloth regressions) and Slice 0 (ROADMAP truth-up) — three independent lanes converged on SSR-zombie (W9), Mesa sampler-binding (R6), atomic-write inconsistency (F7), and ~20 other `[x]`-boxes grep proved false. Slices are ordered by dependency: truth-up → foundations → Phase 10.7 gaps → safety / rendering / parsing / animation / physics / wiring / input / splines / systems / editor / performance → scripting → audio → shader parity → cloth regressions. Later slices can be parallel-tracked once their upstream slices land.

**Context.** The review surfaced 14 CRITICAL and ~47 HIGH findings. Five of the CRITICAL findings were Phase 10.7 features that passed every test we wrote but delivered a subset of the design doc — the tests encoded what the code did, not what the design doc said. This phase closes those gaps before Phase 11 builds on top.

**Process discipline for this phase.** Every slice follows the design-doc-first-tests pattern: failing regression tests authored from the design-doc or external-standard clause (cited in the test name — e.g. `TEST(Photosensitive, WCAG_2_3_1_*)`) land as a "red" commit; the fix lands as a "green" commit. Each slice also triggers an independent-reviewer subagent pass before ship.

### Slice 0: ROADMAP truth-up — prerequisite for all downstream zombie-feature remediation
- [ ] **T0.** Grep-audit every `[x]` DONE claim in this ROADMAP for ≥1 non-declaration, non-test call site of its primary entry-point. Demote falsely-claimed items (expected hits: Ragdoll, Fracture, Dismemberment, `GrabSystem`, `StasisSystem`, `BreakableComponent::fracture`, `MotionMatcher`, `MotionDatabase`, `LipSyncPlayer`, `FacialAnimator`, `EyeController`, `MirrorGenerator`, `Inertialization::apply`, `SpritePanel`, `TilemapPanel`, SSR, contact-shadow, `GpuCuller::cull`) to `[ ]` or relocate to `engine/experimental/` with a note. Subsequent slices (W12–W15) can then trust the baseline. Second /indie-review 2026-04-23 confirmed ≥20 such zombies against `[x]` boxes.

### Slice 1: Foundations — enable everything else
Small, isolated fixes with no upstream dependencies. Every downstream slice benefits.

- [x] **F1.** `engine.cpp:412` — fix `m_assetPath + "/captions.json"` double-concatenation. Regression test loads a fixture caption file and verifies key lookup. **Shipped 2026-04-23** (red `7b9f116`, green `5572aa5`, nit `4641124`). Extracted the join into `engine/core/engine_paths.{h,cpp}` as a unit-testable helper; `stripTrailingSlash(assetPath) + "/captions.json"`. Four spec tests in `tests/test_engine_paths.cpp` pin the single-slash join and the bare-filename short-circuit. Independent reviewer pass: accept-with-nit.
- [x] **F2.** `Component::clone()` — make the base pure-virtual; every concrete component explicitly deep-copies. Backfill missing overrides (audit `entity_serializer` allowlist while there). **Shipped 2026-04-23** (red `f911aa5`, green `75b64a3`). `ClothComponent` was the only subclass relying on the base default; backfilled with the WaterSurfaceComponent / GPUParticleEmitter config-only clone shape. Serializer allowlist audit captured in the F2 CHANGELOG entry as the coverage list for F3 (`ComponentSerializerRegistry`).
- [x] **F3.** `ComponentSerializerRegistry` — widen `entity_serializer` so components register their own JSON read/write rather than being dropped by a fixed allowlist. Fixes silent-drop of `AudioSourceComponent::bus` (Phase 10.7 A1 latent bug), sets up CameraMode persistence. **Shipped 2026-04-23** (red `4056ce0`, green `f640d1c`). Registry holds `{typeName, trySerialize, deserialize}` tuples at `engine/utils/component_serializer_registry.{h,cpp}`. Eight built-ins registered (seven pre-F3 allowlist types, behaviour-preserved, plus AudioSource with every user-editable field round-tripping — `bus`, `pitch`, `velocity`, `attenuationModel`, `occlusionMaterial`, `occlusionFraction`, `autoPlay`, `minDistance` / `maxDistance` / `rolloffFactor`, `loop`, `spatial`). Unknown JSON keys log a warning instead of silently dropping. CameraMode serialisation is now a one-line registration — deferred to the Phase 10.8 CM* / Slice 10 slice that introduces the serialisable fields.
- [x] **F4.** Clamp helpers (`clampFlashAlpha`, `clampShakeAmplitude`, `clampStrobeHz`, `limitBloomIntensity`) sanitise NaN / ±∞ / negatives in **both** enabled and disabled paths. **Shipped 2026-04-23** (red `c7023ae`, green `50aa1bc`). Factored a private `sanitiseNonNegative(x)` helper in `photosensitive_safety.cpp` that returns `0.0f` for non-finite / negative values and the value itself otherwise, then applied it at the top of all four clamp helpers before the enabled/disabled branch. 13 WCAG-2.2-SC-2.3.1-tagged tests (`PhotosensitiveSafety.WCAG_2_3_1_*`) pin both paths, plus `WCAG_2_3_1_DisabledPreservesFinitePositives` pins that finite non-negatives still pass through unmodified when safe mode is off. Header docstring names the sanitisation contract alongside the existing cap/scale semantics.
- [x] **F5.** `clampStrobeHz` hard-caps at WCAG 3 Hz. `std::min(hz, std::min(limits.maxStrobeHz, WCAG_MAX_STROBE_HZ))`. Prevents user-edited `maxStrobeHz = 29.0` from defeating safe mode. **Shipped 2026-04-23** (red `559b4bd`, green `778fb29`). Published `WCAG_MAX_STROBE_HZ = 3.0f` as a module-level `inline constexpr`; enabled-path cap is now `std::min(limits.maxStrobeHz, WCAG_MAX_STROBE_HZ)`. Tighter caller caps (horror beats at 0.5 Hz) still win; looser caller caps (29 Hz from a mis-tuned config) get clamped back to 3 Hz. Safe-mode-disabled identity contract from F4 preserved. Five new tests (`WCAG_2_3_1_StrobeHzConstantIsThreeHz`, `*HardCapsEvenWhenLimitsRelaxed`, `*BelowWCAGCeilingStillPassesThrough`, `*TighterCallerCapStillWins`, `*HardCapDoesNotApplyWhenDisabled`) pin all four corners of the interaction.
- [x] **F6.** OBJ loader — support OBJ-spec-mandated negative indices; add 1 MiB per-line read cap. **Shipped 2026-04-23** (red `371c8bc`, green `f6c2375`). `resolveObjIndex` handles positive 1-based, negative relative-to-current-list-size at face-parse time (OBJ spec Appendix B), and zero/malformed → invalid. `readBoundedLine` replaces unbounded `std::getline` with a 1 MiB per-line cap (CWE-400 Uncontrolled Resource Consumption mitigation); over-limit logs + returns false without keeping the over-size buffer.
- [x] **F7.** Atomic-write unification: delete duplicate `scene_serializer.cpp::atomicWriteFile`; route `Window::saveWindowState`, `PrefabSystem::savePrefab`, `FileMenu` auto-save + `.path` sidecar, and terrain heightmap/splatmap writes through `engine/utils/atomic_write.cpp` (the only path with full fsync(file)+rename+fsync(dir) durability). CLAUDE.md Rule 3 (reuse before rewriting). **Shipped 2026-04-23** (red `26e5e62`, green `ea1e133`). Scene serializer's 50-line duplicate deleted; prefab / window / autosave / terrain-sidecar paths all converge on `AtomicWrite::writeFile`. Test `POSIXRename_PrefabSaveClearsStaleTmpSidecar_Rule3` pins the routing: a stale `.tmp` sidecar from a prior crashed save is overwritten + renamed away by the helper's write-tmp / rename cycle. Net 134 lines deleted / 62 added across five files; five durability bugs closed by removing four copies of a contract the codebase already had one correct implementation of.
- [x] **F8.** `SettingsEditor::mutate` — one-line `validate(m_pending)` before `pushPendingToSinks()`. Runtime-UI slider path currently bypasses every clamp policy (bus gains >1.0, strobe Hz ignored, etc.). **Shipped 2026-04-23** (red `357f8a9`, green `92af103`). `validate()` lifted from `settings.cpp` anonymous namespace to the `Vestige` namespace (declared in `settings.h`) and called from `SettingsEditor::mutate` after the mutator, before `pushPendingToSinks()`. Three red tests pin the gap: `MutateClamps{AudioBusGain,RenderScale,StrobeHz}BeforePushingToSink_F8` — all showed raw slider values reaching subsystems pre-F8, now all clamp as the persistence path already did. `Settings::fromJson` already ran `validate()` on load; F8 is the symmetric runtime wire-up so live previews can't diverge from what persists. Net +22 / -3 lines across three files; zero regressions (2783/2783 pass).
- [x] **F9.** `Logger` thread-safety: `std::mutex` around `s_logFile` / `s_entries` in `logger.cpp::log`. `AsyncTextureLoader` already logs from a worker thread — live race on `std::deque` + `ofstream`. **Shipped 2026-04-24** (red `a113e32`, green `87818fa`). One private static `s_logMutex` in `logger.cpp` plus `std::lock_guard` in `log()` (after the lock-free level-filter early-out so filtered messages stay uncontended), `clearEntries()`, and `getEntries()`. `getEntries()` API changed from `const std::deque<LogEntry>&` to `std::deque<LogEntry>` (by value) — the old reference was racy against worker-thread writers by construction; `getEntries()` now returns a snapshot copied under the lock. Both existing call sites in `editor.cpp` iterate the whole deque for a per-frame debug panel, so per-frame copy of ~1000 `LogEntry` is trivial. Two RED tests in `tests/test_logger.cpp` SEGV'd deterministically on shipping code (`ConcurrentLoggingPreservesAllEntries_F9`, `ConcurrentLoggingRespectsRingBufferCap_F9` — 8 threads × 100 and × 500 `Logger::info` respectively); both pass post-fix. `openLogFile`/`closeLogFile` left unlocked (single-threaded startup/shutdown lifecycle). Net +19 / -3 lines across two files; 2785/2785 pass (1 pre-existing skip unchanged; +2 new tests vs. F8).
- [x] **F10.** `SystemRegistry::initializeAll` partial-init cleanup: on failure of system N, shutdown the initialized 0..N-1 prefix in reverse before returning false. Prevents GL/AL/Jolt resource leak through destructor-only cleanup. **Shipped 2026-04-24** (red `bd6ebe8`, green `840c651`). `initializeAll()` now tracks a local `initializedCount` of systems that returned true from `initialize()`; on the first failure a reverse loop over `[0, initializedCount)` calls `shutdown()` + `setActive(false)` on each and logs `SystemRegistry: rolling back 'Name'` before returning false. System N itself gets no `shutdown()` — its `initialize()` returned false, meaning the resource contract was never established (matches the existing single-system convention so subsystems don't need shutdown-without-init defensiveness). `m_initialized` stays false on the failure path so a subsequent `shutdownAll()` remains a clean no-op; `clear()` still runs destructors exactly once. Five RED tests in `tests/test_system_registry.cpp` — three of them (`...ShutsDownPrefixInReverseOnFailure_F10`, `...ShutsDownEveryPrecedingOnFailure_F10`, `...FailureDeactivatesPrefix_F10`) fail on shipping code; the other two (`...FailureOnFirstSystemShutsDownNothing_F10`, `...FailureLeavesRegistryReInitable_F10`) pass as-is and pin the invariants. Net +19 / -0 lines in one file; 2791 / 2791 pass (1 pre-existing skip unchanged; +5 new tests vs. F9).
- [x] **F11.** "Max strobe Hz" slider honesty at `settings_editor_panel.cpp:516` — drop slider max to `3.0f` OR render "Capped at WCAG 2.2 SC 2.3.1 ceiling of 3 Hz in safe mode" helper text. Current UI persists 7.00 while F5 enforces 3.0 — partially-sighted user is being lied to. **Shipped 2026-04-24** (red `c6db868`, green `5d52006`). `SettingsEditorPanel::SAFE_MODE_STROBE_HZ_SLIDER_MAX` added to the panel header and initialised to `WCAG_MAX_STROBE_HZ` (3.0 Hz, per WCAG 2.2 SC 2.3.1 "Three Flashes or Below Threshold"); slider call at `settings_editor_panel.cpp:516` refactored to consume the constant so the panel header and the runtime clamp in `PhotosensitiveSafety::clampStrobeHz` now share one source of truth. Safe-mode slider values above 3 Hz that were previously persisted and silently discarded are no longer reachable. Settings-schema `validate()` still clamps the persisted field to `[0, 30]` — F11 closes the narrower "what can the safe-mode UI offer" gap which is strictly tighter. Two RED tests in `tests/test_settings.cpp` (`SafeModeStrobeSliderMaxEqualsWcagCeiling_F11`, `SafeModeStrobeSliderMaxIs3Hz_F11`) both failed at runtime on the deliberately-encoded 10.0 lie; both pass post-fix. Net +3 / -1 lines in one header; 2793/2793 pass (1 pre-existing skip unchanged; +2 new tests vs. F10). **Slice 1 complete** — F1…F11 all shipped. Next: **Slice 2 (Phase 10.7 completion)**.
- [ ] **F12.** Close `entity_serializer` component-registry gap (~18 unregistered types: `ClothComponent`, `RigidBody`, `BreakableComponent`, `CameraComponent`, `SkeletonAnimator`, `TilemapComponent`, 2D physics/camera/sprite components, `InteractableComponent`, `PressurePlateComponent`, `GPUParticleEmitter`, `FacialAnimator`, `TweenManager`, `LipSyncPlayer`, `NavAgentComponent`, `CameraMode`). Either register via F3's registry OR emit a loud save-time warning for entities owning unregistered components. F3 registered 8; reality has ~26. Silent data-loss round-tripping save→load today.

### Slice 2: Phase 10.7 completion — what actually shipped vs. what was spec'd
Finish the gain-chain, subtitle, caption, and HRTF features that passed tests but delivered subsets of the design. Depends on Slice 1.

- [x] **P1.** Subtitle 40-char soft-wrap + 2-line cap + plate sizing from `max(lineWidth)` (PHASE10_7_DESIGN.md §4.2). Unit-test word-boundary wrap, overlong tokens, line-count cap. **Shipped 2026-04-24** (red `23f845a`, green `3248476`). `wrapSubtitleText(text, maxChars=SUBTITLE_SOFT_WRAP_CHARS=40, maxLines=SUBTITLE_MAX_LINES=2)` in `engine/ui/subtitle.{h,cpp}` — greedy word-boundary packing; hard-breaks single tokens over the char limit at the limit (so a 55-char URL doesn't silently overflow the plate); truncates with a U+2026 ellipsis when the full input would produce more than `maxLines` rows (so the reader sees that content was trimmed, not lost). `SubtitleLineLayout` gains `wrappedLines` + `lineStepPx`; `computeSubtitleLayout` sizes the plate off `max(measureTextPx(row))` (longest rendered row, not pre-wrap total) with height `lineHeightPx + (rows - 1) × (basePx + lineSpacingPx)`; Y anchor stays pinned at `screenHeight × (1 - bottomMarginFrac)` so taller plates rise upward. `renderSubtitles` emits one `renderText2D` call per wrapped row stepped by `lineStepPx`. 13 new tests across `tests/test_subtitle.cpp` + `tests/test_subtitle_renderer.cpp` — 8 failed at runtime on the deliberately-wrong single-line stub; all 13 pass post-fix. Net +193 / -37 lines across four files; 2806 / 2806 pass (1 pre-existing skip unchanged; +13 new tests vs. F11).
- [x] **P2.** `AudioSystem` per-frame component-iteration pass: `std::unordered_map<Entity, ALuint> m_activeSources` + per-frame `AL_POSITION` / `AL_VELOCITY` / `AL_PITCH` / `finalGain` push. Brings `pitch`, `velocity`, `attenuationModel`, `minDistance`, `maxDistance`, `rolloffFactor`, `autoPlay`, `occlusionMaterial`, `occlusionFraction` from dead data to live. **Shipped 2026-04-24** (red `4c2f1c3`, green `a96cccd`). Four-layer wire: (1) **pure compose** — new `engine/audio/audio_source_state.{h,cpp}` with `AudioSourceAlState` + `composeAudioSourceAlState(comp, pos, mixer, duck)` runs every component field through the occlusion → mixer → duck pipeline (occlusion `computeObstructionGain` folds into the volume input of the 4-arg `resolveSourceGain` from P3 so no new clamp site); (2) **AL state push** — `AudioEngine::applySourceState(source, state)` issues the full `alSource*f` set for the eight per-frame fields; (3) **source-alive probe** — `AudioEngine::isSourcePlaying(source)` wraps `alGetSourcei(AL_SOURCE_STATE)` so the reap pass doesn't poke engine internals; (4) **per-frame iteration** — `AudioSystem::update` walks `scene->forEachEntity`, auto-acquires an AL source for any `AudioSourceComponent{autoPlay=true, clipPath!=""}` not yet tracked, pushes composed state every frame, reaps stopped / destroyed entries. `m_activeSources` exposed via const `activeSources()` accessor for test observation. **Debt folded in:** every `playSound*` overload now returns `unsigned int` (AL source ID, 0 on failure) instead of `void` so trackers get a real handle — latent 3-arg `resolveSourceGain` calls in the initial-upload paths also switched to the 4-arg form so a sound acquired *during* a duck is audible at the ducked level from frame 1. 13 new tests across `test_audio_source_state.cpp` + `test_audio_mixer.cpp` — 10 failed at runtime on the deliberately-wrong stub; all 13 pass post-fix. Net +540 / -68 lines across ten files; 2844/2844 pass (1 pre-existing skip unchanged; +13 new tests vs. P3).
- [x] **P3.** `DuckingState::currentGain` folded into `resolveSourceGain` — ducking is computed but never applied; P2 is the hook. **Shipped 2026-04-24** (red `389af46` + amend `5fdf618`, green `2eda0ff`). Three-layer wire: (1) **math** — 4-arg `resolveSourceGain(mixer, bus, volume, duckingGain)` overload in `audio_mixer.cpp` multiplies `clamp01(duckingGain)` after `master × bus × volume` then clamps the product to [0, 1]; (2) **storage** — `AudioEngine::setDuckingSnapshot/getDuckingSnapshot` with `m_duckingSnapshot` member clamped on ingest; `updateGains` threads the snapshot through every `resolveSourceGain` call so every live source's `AL_GAIN` includes the duck; (3) **publish** — `AudioSystem::update` advances `updateDucking(m_engine->getDuckingState(), ...)` by the frame delta then publishes `currentGain` to `m_audioEngine.setDuckingSnapshot`. `Engine` owns `m_duckingState` + `m_duckingParams` as authoritative state with `getDuckingState/getDuckingParams` accessors; `AudioPanel::wireEngineDucking(state*, params*)` mirrors the `wireEngineMixer` pattern so the panel's Debug tab mutates the authoritative state rather than a local copy that never reached AL. `Engine::initialize` calls `wireEngineDucking(&m_duckingState, &m_duckingParams)` alongside `wireEngineMixer`. Panel keeps a null-pointer fallback for standalone / test usage. 10 new tests across `tests/test_audio_mixer.cpp` + `tests/test_audio_panel.cpp` — 5 failed at runtime on the deliberately-wrong stubs, all pass post-fix. No stubs remain; no TODO/FIXME; every new public symbol documented. Net +222 / -12 lines across ten files; 2831/2831 pass (1 pre-existing skip unchanged; +10 new tests vs. P4).
- [x] **P4.** Caption auto-enqueue on `playSound*` entry (Slice B3 closure). `CaptionMap::enqueueFor(path, queue)` fires once at source-acquire, not every frame. Depends on F1. **Shipped 2026-04-24** (red `e8c2308`, green `5986736`). Adds `AudioEngine::setCaptionAnnouncer(std::function<void(const std::string&)>)` — an optional hook invoked at the top of every `playSound*` overload (`playSound`, two `playSoundSpatial`, `playSound2D`), BEFORE the `!m_available` short-circuit. Firing before the availability check is the accessibility contract: users with broken audio hardware / deafness / zero-volume output still need the caption when game code *intends* to play a sound (captions are the accessibility substitute for the audio, not a side-effect of audio reaching the speakers). `Engine::initialize` installs the announcer as a lambda forwarding to `m_captionMap.enqueueFor(clip, m_subtitleQueue)` immediately after loading the caption map. No changes required at individual `playSound*` call sites — script-graph `PlaySound` nodes, `AudioSourceComponent` autoplay, ambient emitters, UI clicks all route captions as a side-effect now. 8 new tests in `tests/test_caption_map.cpp` (natural home — the end-to-end integration test already instantiates both a CaptionMap and a SubtitleQueue there); 6 failed at runtime on the unwired cpp, 2 passed (no-announcer safety + unmapped-clip no-op, both defensive pins). Net +220 / -3 lines across four files; 2821/2821 pass (1 pre-existing skip unchanged; +8 new tests vs. P5).
- [x] **P5.** `SubtitleQueueApplySink::setSubtitlesEnabled` → consumer read path (either `SubtitleQueue::setEnabled(bool)` filtering `activeSubtitles()` output, or `Engine::getSubtitlesEnabled()` proxy). Today the toggle writes a flag nothing reads. **Shipped 2026-04-24** (red `2e91605`, green `cd16aa5`). Took option 1: `SubtitleQueue::setEnabled(bool)` + `isEnabled()`; `activeSubtitles()` returns a static empty vector when disabled, `size()` returns 0, `empty()` returns true. Internal `m_active` keeps ticking so captions enqueued during a disabled window keep their countdown — re-enabling shows only captions that would still be on screen, not stale ones. `SubtitleQueueApplySink::setSubtitlesEnabled` forwards to `m_queue.setEnabled(enabled)` (dropped the local `m_enabled` flag — one source of truth). `UISystem::renderUI` already early-returns on `activeSubtitles().empty()` so no renderer change required; the empty view stops the overlay pass. 7 new tests — 3 failed at runtime on the stub's unfiltered view, 4 passed (intact tick/enqueue side-effect path). All pass post-fix. Net +138 / -16 lines across four files; 2813/2813 pass (1 pre-existing skip unchanged; +7 new tests vs. P1).
- [ ] **P6.** Narrator styling decision — italic font atlas + flag through `TextRenderer`, or spec-revised to use a differentiating colour. Blocks §4.2 compliance claim.
- [ ] **P7.** Voice-eviction wiring — `chooseVoiceToEvict` into `playSound*` retry when pool is exhausted. Adds `SoundPriority` to `AudioSourceComponent`.
- [ ] **P8.** HRTF init-order fix + `HrtfStatusChanged` device-reset event so the Settings UI can surface "Requested: Forced / Actual: Denied (UnsupportedFormat)".

### Slice 3: Safety surfaces
Crash vectors + accessibility claims that passed tests but leak to real users.

- [ ] **S1.** `Scene::removeEntity` nulls `m_activeCamera` if the deleted subtree contains it. Regression test: delete camera entity, assert `getActiveCamera() == nullptr`.
- [ ] **S2.** Component-mutation-inside-`update()` contract — snapshot component pointers before iterating OR ban mid-update add/remove with an explicit assert + deferred-mutation queue. Resolves the scripting-vs-update collision Phase 11B AI will trigger.
- [ ] **S3.** `UIElement::hitTest` recurses into children; `UICanvas::hitTest` walks nested trees. Unblocks grouped-widget layouts that are silently broken today (only root-level elements are reachable).
- [ ] **S4.** Keyboard navigation — focus ring + Tab / arrow / Enter + modal-aware focus traversal. Menu footer advertises "UP DOWN NAVIGATE" but no handler exists. XAG 102 conformance.
- [ ] **S5.** `UIPanel` delegates hit-test to children when non-interactive.
- [ ] **S6.** `PressurePlateComponent` overlap query uses `getWorldPosition()` not local transform — fails in any parented hierarchy today.
- [ ] **S7.** `Scene::removeEntity` / `unregisterEntityRecursive` nulls `m_activeCamera` if the destroyed subtree owned it. Raw `CameraComponent*` currently dangles after camera-entity delete; renderer dereferences.
- [ ] **S8.** `NavMeshQuery::findPath` surfaces `DT_PARTIAL_RESULT` (tuple return or out-param). Agents currently arrive silently 20m short of unreachable targets; AI has no hook to re-plan or notify.
- [ ] **S9.** `UITheme` default contrast — bump `textDisabled` and `panelStroke` defaults to satisfy WCAG 1.4.11 (3:1 non-text) and 1.4.3 (4.5:1 text-disabled comfort). Current Vellum `panelStroke` alpha 0.22 yields ≈1.8:1 over base. Load-bearing for partially-sighted primary user.

### Slice 4: Rendering correctness
IBL corruption is load-bearing: every PBR material since day one has been lit with corrupted irradiance / prefilter values. Fix early.

- [ ] **R1.** Wrap IBL capture paths in `ScopedForwardZ` — `EnvironmentMap::generate`, `LightProbe::generateFromCubemap`, prefilter, convolution. Also the init-time first-generation path in `renderer.cpp:683-692` (currently no save/restore at all). Parity test: render a probe with + without the wrap, diff the prefilter output.
- [ ] **R2.** GPU compute SH projection replacing per-face `glReadPixels` + CPU projection in `captureSHGrid`. Editor "Bake GI" moves from ~1 FPS to full pipeline speed.
- [ ] **R3.** Shadow-pass state save/restore for `GL_CLIP_DISTANCE0` + `GL_DEPTH_CLAMP`. Extend `ScopedForwardZ` or a sibling RAII.
- [ ] **R4.** `ScopedBlendState` + `ScopedCullFace` RAII applied to foliage, water, tree, particle renderers. Replaces hand-rolled enable/disable that assumes caller-state.
- [ ] **R5.** `GpuCuller` — cache `GLint m_planeLocation0` at init, upload via `glUniform4fv(loc, 6, data)`. Eliminates per-frame `std::to_string` allocations.
- [ ] **R6.** Mesa sampler-binding fallbacks at foliage-no-shadow (`foliage_renderer.cpp:178-200`, unit 3 `u_cascadeShadowMap` unbound), water-first-frame (units 3/4/5/6 no fallback bind), GPU-particles-no-collision (`gpu_particle_system.cpp:281-289`, compute shader unit 0), procedural-skybox (`renderer.cpp:3143-3158`, samplerCube vs sampler2D at unit 0). Pattern exists at `renderer.cpp:749-768`; apply 4 more times. Systemic Mesa AMD `GL_INVALID_OPERATION` hazard.
- [ ] **R7.** SH probe grid double cosine-lobe convolution: either drop `SHProbeGrid::convolveRadianceToIrradiance` and store radiance-SH, OR replace shader constants at `scene.frag.glsl:569` with pure Y_ℓm basis constants. Currently multiplies by A_ℓ twice — band-0 ambient ≈π× over-bright. Ramamoorthi 2001 §4.1 / Sloan 2008.
- [ ] **R8.** SDSM synchronous `glGetNamedBufferSubData` at `depth_reducer.cpp:97` → double-buffered PBO + `glMapNamedBufferRange` (match the bloom luminance pattern at `renderer.cpp:1113-1158`). Main-thread GPU stall today; blocks 60 FPS on Mesa AMD.
- [ ] **R9.** Bloom `bloom_downsample.frag.glsl` Karis path — restore 0.5 centre + 0.125×4 corner energy weighting (Jimenez 2014 slide 147 / Unreal `BloomDownsample.usf`). Current Karis path drops first-mip centre weight → "softness pop" and energy loss.
- [ ] **R10.** `m_prevWorldMatrices` unconditional clear at `renderScene` entry (currently cleared only in TAA branch — grows unbounded across MSAA/SMAA/None modes and hands stale mat4s to motion-overlay on mode switches).

### Slice 5: Data / asset parsing robustness
Security + correctness for untrusted or malformed inputs. Gates community-mod / Steam-Workshop futures.

- [ ] **D1.** Path sandbox in `ResourceManager::loadTexture` / `loadMesh` — move the `resolveUri`-style base-dir check to a choke-point inside ResourceManager. Every scene-JSON path flows through it automatically.
- [ ] **D2.** tinygltf `FsCallbacks` with a custom `ReadWholeFile` that rejects paths outside `gltfDir`. Closes the confused-deputy / TOCTOU race where tinygltf reads bytes before `resolveUri` sees them.
- [ ] **D3.** `.cube` LUT loader: file-size cap + path sandbox. Same shape as OBJ's `JsonSizeCap`-style reader.
- [ ] **D4.** OBJ MTL support (or declared "not supported" log). Currently `usemtl` silently drops multi-material imports to one material.
- [ ] **D5.** `resolveUri` empty-return → substitute default texture explicitly rather than passing `""` through to `loadTexture`.
- [ ] **D6.** OBJ vertex-key hash: `boost::hash_combine`-style combiner, not `h3 << 32` (UB on 32-bit `size_t`).
- [ ] **D7.** Scene-JSON recursion-depth cap: `deserializeEntityRecursive`, `collectRenderDataRecursive`, `countJsonEntities` take a `depth` parameter and reject `> 128`. Lift the `pysr_parser::DepthGuard` RAII. Current path is a classic JSON stack-bomb (256 MB of nested `{"children":[...]}` blows the 8 MB default stack).
- [ ] **D8.** Terrain config size caps: `Terrain::deserializeSettings` hard-caps `width, depth ≤ 8193`, `gridResolution`, `maxLodLevels ≤ 10`. Current `width * depth * sizeof(float)` on attacker JSON requests ~320 GB of heap — instant OOM kill.
- [ ] **D9.** Heightmap / splatmap per-file size cap (256 MB) in `Terrain::loadHeightmap`/`loadSplatmap`. Combined with D8, closes the two-stage attack (set width×depth to match a crafted binary, then feed the binary).
- [ ] **D10.** glTF bounds checks: `gltf_loader.cpp:1041-1044` child-index `< gltfModel.nodes.size()`; `:1051` `sceneIdx`; `:1056-1059` root-node indices. Eliminate primitive-count pre-scan drift by returning `{startIdx, count}` directly from `loadMeshes` (currently a separate pre-scan at `:957-968` can diverge from actual loaded set, shifting every mesh's primitive offsets).
- [ ] **D11.** Path-traversal guards on `AudioClip::loadFromFile`, `LipSyncPlayer::loadTrack`, `MotionDatabase` load paths. Single shared `validatePath()` helper; reject absolute + `..` segments unless caller opts in via "trusted" flag. MIT-open-source trap.
- [ ] **D12.** tinygltf `extensionsRequired` allowlist in the loader — fail (don't skip) on unknown required extensions per glTF 2.0 §3.12. Silent fallback today.

### Slice 6: Animation correctness
Three silent-corruption bugs affecting anyone importing glTF characters.

- [ ] **A1.** Skeleton DFS update order — build `m_updateOrder: std::vector<int>` in DFS pre-order at `Skeleton` construction; iterate that in `computeBoneMatrices`. Debug-build assert parent-idx < child-idx. Remap animation channel joint indices if joints are reordered.
- [ ] **A2.** CUBICSPLINE quaternion double-cover fix — before Hermite blend, `if dot(vk, vk1) < 0: flip vk1 and ak1`. Unit test with a deliberately-wrapped keyframe pair.
- [ ] **A3.** Motion-matching query frame-of-reference parity with database — rotate trajectory positions + directions by current character root yaw in `buildQueryVector`.
- [ ] **A4.** IK pole-vector alignment uses post-solve mid position, not re-rotated pre-solve mid.
- [ ] **A5.** Inertialisation axis-angle stability — `sqrt(1-w²)` + clamp instead of `acos(w)` + `sin(angle/2)`.

### Slice 7: Physics determinism — gates Phase 11A replay
Phase 11A's Replay Recording Infrastructure requires deterministic physics. These findings break that contract today.

- [ ] **Ph1.** Move character-controller + breakable-constraint checks inside the fixed-step loop. Divide breakable lambda by `m_fixedTimestep`, not frame dt.
- [ ] **Ph2.** `PhysicsWorld::rayCast` gains optional `BodyFilter` / `ObjectLayerFilter` — Phase 11B combat + grab system need self-hit exclusion. Fix the `interactRange` double-scaling at `engine.cpp:775-780`.
- [ ] **Ph3.** `PhysicsWorld::sphereCast` API (originally scheduled as Phase 10.8 CM3 — pulled here so Phase 10.8 consumes it rather than authors it).
- [ ] **Ph4.** Breakable-constraint force sums rotation lambdas + slider position-limit lambdas — hinge / slider limit breaks feel wrong without them.
- [ ] **Ph5.** Character-vs-character pair filter — split PLAYER_CHARACTER / NPC_CHARACTER, or change the filter + use collision groups. Otherwise ragdolls in CHARACTER layer pass through the player.
- [ ] **Ph6.** `std::unordered_map<uint32_t, PhysicsConstraint> m_constraints` → `std::map` (or paired `vector<handle>` iteration index). Same for `StasisSystem::m_stasisMap`. Hash-dependent iteration order breaks deterministic break-order tests and Phase 11A replay. Per-slot generation counter replaces global `++m_constraintGeneration`.
- [ ] **Ph7.** Slider `normalAxis` deterministic basis at `physics_world.cpp:515-523` — use Hughes-Möller orthonormalize, not world-Y comparison. Two scenes with identical geometry rotated 90° currently solve differently.
- [ ] **Ph8.** Constraint creation uses `BodyLockMultiWrite` on `{bodyA, bodyB}` — raw `JPH::Body*` currently escapes a single-body `BodyLockWrite` scope at `physics_world.cpp:322-344` and is used at 404/429/467/494/538 outside the lock. UB under concurrent broadphase update.
- [ ] **Ph9.** `RigidBody::syncTransform` stop round-tripping rotation through Euler (`rigid_body.cpp:174-175`). Gimbal loss past ±90° pitch on tumbling bodies. Store quaternion in `Transform` and write directly.

### Slice 8: Subsystem wiring / dead-code cleanup
Per CLAUDE.md Rule 6 (no over-engineering) + Rule 10 (no workarounds-as-fixes). Finish-or-delete, not cargo-cult.

- [ ] **W1.** `AsyncTextureLoader` — either construct it + guard placeholder texture during upload (`Texture::isReady()` atomic), or delete the header + member + `processAsyncUploads()`.
- [ ] **W2.** `FileWatcher` — wire callbacks + `ResourceManager::reload(path)` + editor "Reload" action, or delete. Docstring currently claims callbacks the class doesn't have.
- [ ] **W3.** `PostProcessAccessibilitySettings.depthOfFieldEnabled` / `.motionBlurEnabled` — document as awaiting-consumer or hide the UI toggles until the effects land. No-op toggles mislead users.
- [ ] **W4.** Screen-reader bridge — wire `UIAccessibility::collectAccessible` to AT-SPI (Linux) / UIA (Windows), or drop the collector + update the roadmap claim as Phase 11+ work.
- [ ] **W5.** `AudioSystem::isForceActive() = true` — infrastructure systems own global state; "scene has no owned components → deactivate" heuristic is wrong for them. Same audit for UISystem, LightingSystem, TerrainSystem.
- [ ] **W6.** Listener-sync-after-camera-step — either split `AudioSystem` into `update()` + explicit `syncListener()` called post-camera, or give the ordering mechanism from Slice 11 a late-phase marker for AudioSystem.
- [ ] **W7.** `AudioEngine::setMixerSnapshot` → `const AudioMixer*` pointer (or seqlock), not per-frame struct copy. Current claim "thread-safe snapshot" doesn't hold.
- [ ] **W8.** `AudioEngine::m_bufferCache` eviction + per-scene flush + wire streaming music path (`audio_music_stream` has no loader consumer today).
- [ ] **W9.** Delete SSR pipeline (`m_ssrShader`, `m_ssrFbo` 16 MB RGBA16F, `ssr.frag.glsl` with its 8 never-set uniforms) OR gate behind a CMake option. Three independent reviewers converged: zero callers today. Re-add cleanly when Phase 5 G-buffer lands.
- [ ] **W10.** Delete contact-shadow pipeline (`m_contactShadowFbo`, `renderer.cpp:1162-1185`) OR gate. Same dead-subsystem pattern.
- [ ] **W11.** Delete `GpuCuller` OR wire `cull()` into the MDI path. Zero callers today despite compiled shader + allocated VBO.
- [ ] **W12.** Animation zombie cluster (`MotionMatcher`, `MotionDatabase`, `LipSyncPlayer`, `FacialAnimator`, `EyeController`, `MirrorGenerator`, `Inertialization::apply`): wire one end-to-end demo driving motion-matching + lip-sync + facial animation OR relocate to `engine/experimental/animation/` with README note. ~4.4 kLoC currently registered as nothing's consumer. Depends on Slice 0.
- [ ] **W13.** Physics zombie cluster (`Ragdoll`, `Fracture`, `Dismemberment`, `GrabSystem`, `StasisSystem`, `BreakableComponent::fracture`): wire a real `DestructionSystem::update` that pumps them OR demote ROADMAP claim + relocate. Current 41-line `destruction_system.cpp` is a pass-through stub. Depends on Slice 0.
- [ ] **W14.** `SpritePanel`, `TilemapPanel` — wire into `Editor::drawPanels` (add members + draw call) OR delete `sprite_panel.cpp`, `tilemap_panel.cpp`, and their tests. Currently compiled + tested, not instantiated. Depends on Slice 0.
- [ ] **W15.** Inspector per-entity `AudioSource` draw section (mirror `drawParticleEmitter`). Closes F3's round-trip gap visibly; `AudioPanel` is scene-wide, not a per-entity editor.

### Slice 9: Input subsystem — spec-vs-code reconciliation
Spec mandates scancode; code stores keycode. Fix the contradiction and the missing axis-binding path.

- [ ] **I1.** `InputBinding::code` stores scancode. Rename factory to `InputBinding::scancode(int glfwScancode)`; capture `glfwGetKeyScancode(key)` at rebind; translate back via `glfwGetKeyName(key, scancode)` for display. Fixes WASD-on-AZERTY silent-layout-flip.
- [ ] **I2.** `InputActionMap::toJson` / `fromJson` live in `engine/input/`, not in `engine/core/settings*.cpp`. Per PHASE10_SETTINGS_DESIGN.md slice 13.4.
- [ ] **I3.** `InputDevice::GamepadAxis` + `float axisValue(...)` query — analog sticks cannot currently be bound to actions at all.
- [ ] **I4.** `findConflicts` device-scope filter (or document cross-device intent). Keyboard and gamepad bindings can't conflict physically.
- [ ] **I5.** `addAction` re-registration — assert / warn when called after `Settings::load()` has populated user rebinds; silent nuke today.
- [ ] **I6.** `keyboardName()` in `input_bindings.cpp:151` completes numpad (`KP_0..KP_9`, `KP_ADD`, `KP_SUBTRACT`, `KP_MULTIPLY`, `KP_DIVIDE`, `KP_ENTER`, `KP_DECIMAL`, `KP_EQUAL`), `Pause`, `PrintScreen`, `ScrollLock`, `NumLock`, `Menu`, `F13..F25`, `WORLD_1/WORLD_2`. Keyboard-primary user currently sees `"Key 320"` in rebind UI for half their keyboard.

### Slice 10: Environment / splines
Research-doc conformance + Phase 10.8 CM7 cinematic-camera dependencies.

- [ ] **E1.** `SplinePath::catmullRom` → centripetal parameterisation. Per FOLIAGE_VEGETATION + CSM_FOLIAGE research docs. Unit-test against a known cusp case.
- [ ] **E2.** `SplinePath::evaluateByArcLength(s)` accessor. Phase 10.8 CM7 cinematic camera requires constant-speed playback.
- [ ] **E3.** GPU foliage culling via the existing `frustum_cull.comp.glsl` (currently unwired). Rule 12 compliance — per-instance scale + pure arithmetic + packable.
- [ ] **E4.** `FoliageChunk::getBounds` Y-range queried from terrain, not magic `[-100, 200]` ceiling.

### Slice 11: Systems update-order mechanism
Registration-order is an implicit contract that Phase 11A / 11B AI systems will break.

- [ ] **Sy1.** `ISystem::getUpdateOrder()` or coarse phase tags (`PreUpdate / Update / PostCamera / PostPhysics / Render`). Stable-sort `m_systems` once after `registerSystem` returns. AudioSystem = PostCamera; UI = late; physics-sync = early. Unblocks W6.

### Slice 12: Editor undo / hygiene
Five inspector types bypass undo entirely today; several write files non-atomically.

- [ ] **Ed1.** Replace `IsItemDeactivatedAfterEdit`-at-end-of-block pattern with per-widget pre-snapshot + any-deactivated bracket (the `drawTransform` pattern). Fixes drag-release events silently dropping undo entries.
- [ ] **Ed2.** Add undo brackets to water / cloth / rigid-body / emissive-light / material inspector edits. All currently bypass `CommandHistory`.
- [ ] **Ed3.** Multi-delete — canonicalise selection to roots (filter descendants) before wrapping into `CompositeCommand`.
- [ ] **Ed4.** Atomic writes for prefab / recent-files / welcome-flag (write-to-temp + rename, matching `scene_serializer`).
- [ ] **Ed5.** `PanelRegistry` + `IPanel` interface — reduces per-new-panel churn (currently requires editing `editor.h` + `editor.cpp` + menu wiring for each panel).
- [ ] **Ed6.** `CreateEntityCommand::execute` on redo — record sibling-index in ctor, re-insert via `insertChild(idx)` (mirror `DeleteEntityCommand`). Currently `addChild` appends, shifting every sibling's position after undo→redo.
- [ ] **Ed7.** Delete `FileMenu::m_isDirty` dual source-of-truth. Route Ctrl+G group + every menu mutation through `CompositeCommand` / `CommandHistory`. Once removed, undo-to-clean works; today `markDirty()` sticks forever regardless of undo.
- [ ] **Ed8.** Brush-stroke `endStroke` → one `CompositeCommand` across foliage + scatter + tree sub-edits. Single Ctrl+Z reverts full stroke (currently up to 3 commands = 3 presses).
- [ ] **Ed9.** Curve + gradient editor widget drag state uses `ImGui::GetStateStorage` slots, not file-static `s_dragIndex` / `s_dragId`. Nested / duplicated widget safety.
- [ ] **Ed10.** `recent_files.cpp:101` — `fs::absolute` with `error_code` overload. Current throw on invalid-UTF-8 path escapes the ImGui frame.
- [ ] **Ed11.** Scene-save envelope atomicity: fold `environment` + `terrain` JSON + heightmap + splatmap into a single manifest-backed atomic sequence (no partial-state post-crash). Depends on F7.

### Slice 13: Performance hygiene
Post-10.7, pre-11 performance sweep. Not 60-FPS-critical today but blocks the Phase 11 load.

- [ ] **Pe1.** `TextRenderer` batch across strings per frame — `begin/queue/end` semantics; one draw call for all HUD labels + subtitles + toasts (currently ~18 draws/frame in a normal HUD).
- [ ] **Pe2.** `SpriteSystem::render` + `Physics2DSystem::update` — member vectors `clear()`ed each frame, not constructed. `Physics2DSystem` iterates `m_bodyByEntity` directly instead of full-tree `forEachEntity`.
- [ ] **Pe3.** `FoliageRenderer::uploadInstances` — triple-buffered persistent-mapped buffer or 2× grow-in-place. Eliminates mid-frame `glDeleteBuffers` + reallocation for every pass × every foliage type.
- [ ] **Pe4.** `EventBus::publish` — reentrancy sentinel + deferred-add queue. Removes the per-dispatch listener-vector copy that heap-allocates at 60 Hz per hot event.
- [ ] **Pe5.** `ResourceManager` unbounded cache → LRU + max-resident VRAM cap. Level-streaming guard.
- [ ] **Pe6.** Hoist `glm::transpose(glm::inverse(modelMatrix))` out of `drawMesh` / per-cloth hot path (`renderer.cpp:1511`, `:3095`). Precompute at scene-data-assembly OR emit normal matrix from vertex shader for uniform-scale case.
- [ ] **Pe7.** `FirstPersonController` joystick-probe rate-limit: 60 Hz × 16 slots → `glfwSetJoystickCallback` event-driven, or 1 Hz fallback. ~960 probes/sec for zero benefit on gamepad-less machines.
- [ ] **Pe8.** Cloth LRA regen `generateLraConstraints` O(P·N) → row-indexed bucket / KD-tree. 16.8M iterations per `rebuildLRA()` at 256² with 256 pins.
- [ ] **Pe9.** Cloth `applyCollisions` per substep — build spatial hash once, pass to both collision passes (currently 2× rebuild × 10 substeps = 20 hash rebuilds/frame).

### Slice 14: Scripting / formula safety
Gates Phase 11B AI + any user-authored graph or preset.

- [ ] **Sc1.** Exec fan-out in `script_context.cpp:129` — iterate all matching `PinConnection`s (save/restore `m_entryPin` per callee). Shipped templates `DoOnce.Then → {PlayAnim, PlaySound}` currently half-fire. Delete the "runtime quirk" comment in `script_compiler.cpp:176-179`.
- [ ] **Sc2.** `evalNode`, `ExprNode::fromJson`, `node_graph::nodeToExpr`, `FromExprHelper::buildNode` depth cap — lift `pysr_parser::DepthGuard` RAII. Unbounded today; 100k-deep unary chain blows the stack on preset load.
- [ ] **Sc3.** `ExpressionEvaluator::evaluate` — add `dot` op OR reject vector ops at scalar evaluator with clear error. Both codegens emit `dot`; evaluator throws `"Unknown binary op: dot"`. Workbench LM fitter silently cannot fit any formula with `dot` today.
- [ ] **Sc4.** `ScriptValue::asInt()` clamps float→`int32_t` before cast (`std::clamp(val, INT32_MIN_f, INT32_MAX_f)`). Pre-C++20 UB today for out-of-range values from clock / physics delta nodes.
- [ ] **Sc5.** Align `MathDiv` (`pure_nodes.cpp:196`, `std::abs(b) < 1e-9f`) with `SafeMath::safeDiv` (`safe_math.h:37`, `right == 0.0f`). One exact-zero policy + `isfinite` gate across evaluator / C++ / GLSL to kill the "R²=0.99 in fitter, NaN at runtime" class.
- [ ] **Sc6.** `isInstanceActive` (`scripting_system.cpp:293-304`) checks generation tag, not just pointer identity. Generation bumps already exist per AUDIT §H9 — just not verified at the dispatch site. Closes ABA hazard.
- [ ] **Sc7.** `passDetectDataCycles` — iterative explicit stack (match the existing comment at `script_compiler.cpp:314`) OR fix the comment. Today the lambda is recursive; a 5000-node pure chain recurses 5000 deep.
- [ ] **Sc8.** `WhileLoop` iteration cap — surface `Clamped` output pin (mirror `ForLoop`) OR document as hard safety rail in tooltip + ROADMAP. Resolves CLAUDE.md Rule 10 workaround-dressed-as-fix.

### Slice 15: Audio ergonomics
- [ ] **Au1.** `playSound(loop=true)` returns `AudioSourceHandle` OR force looping sounds exclusively through `AudioSourceComponent`. 32 looping calls currently freeze the mixer with no caller-side stop path. Ship-stopper for any ambient (waterfall, wind, torch).
- [ ] **Au2.** `DopplerParams{}` default `speedOfSound` ≠ 0 sanity — initial `alSpeedOfSound(0)` is rejected by OpenAL and leaves the internal default unchanged. Either fix the default member initialiser or apply the existing clamp before push at `audio_engine.cpp:73-74`.
- [ ] **Au3.** `audio_engine.cpp:186` — init `ALuint buffer = 0` before `alGenBuffers` and short-circuit on error. Currently an uninitialised name goes into `alDeleteBuffers` on failure.

### Slice 16: Shader parity relabel / fix
Resolves CPU↔GPU cloth divergences that make CLAUDE.md Rule 12 parity-test impossible today.

- [ ] **Sh1.** `cloth_constraints.comp.glsl` XPBD claim: either accumulate `λ` across iterations (canonical XPBD per Macklin 2016 §3.5) OR rename to `cloth_pbd_constraints.comp.glsl` + update header comment. Current form is PBD-with-compliance, not XPBD.
- [ ] **Sh2.** GPU cloth `cloth_collision.comp.glsl:80-91` plane margin — drop `collisionMargin` from penetration (`pen = collisionMargin - signedDist`); CPU path at `cloth_simulator.cpp:1134-1138` explicitly says "no margin for planes — injects energy, causes drift". Primary CPU/GPU parity violation.
- [ ] **Sh3.** GPU cloth friction in `cloth_collision.comp.glsl` — add Coulomb static/kinetic tangential-friction term to match CPU `applyFriction`. Sliding-on-sphere / plane currently behaves completely differently on GPU.
- [ ] **Sh4.** GPU cloth wind aerodynamic drag in `cloth_wind.comp.glsl` — per-triangle `0.5·dragCoeff·area·(vRel·n)·n` (current GPU wind is isotropic exponential drag only). `setWindQuality()` stored but not used by shader; three tiers produce the same output on GPU today.

### Slice 17: Cloth + renderer regressions — depends on Sh1–Sh4
- [ ] **Cl1.** CPU↔GPU cloth parity harness: headless test that drives identical `ClothConfig` on both backends for 2s, asserts per-particle position delta < `epsilon`. Depends on Sh1–Sh4. CLAUDE.md Rule 12 parity gate.
- [ ] **Cl2.** `ClothComponent::syncMesh()` stops calling `simulate(0.0001f)` at `cloth_component.cpp:99` — expose `syncBuffersOnly()` on `IClothSolverBackend`. Refresh should not integrate gravity / wind.
- [ ] **Cl3.** GPU `uploadPinsIfDirty` (`gpu_cloth_simulator.cpp:270-277`) — delete the full velocity-SSBO readback + zero + re-upload. Integrate shader already short-circuits on `invMass==0`; readback is redundant and stalls the GPU on every editor pin drag.
- [ ] **Cl4.** GPU `buildAndUploadDihedrals` hard-codes `dihedralCompliance = 0.01f` at `gpu_cloth_simulator.cpp:444` — expose setter (per-constraint uniform override or re-upload) OR document the GPU-backend limitation on `IClothSolverBackend`.
- [ ] **Cl5.** GPU `reset()` semantics — either capture proper rest snapshot (mirror CPU `m_initialPositions` / `captureRestPositions`) OR document divergent semantics in header. Pinned particles currently snap to last-pinned-position, not initial grid.
- [ ] **Cl6.** Dihedral-constraint build pre-sorted edge vector replaces `std::unordered_map<uint64_t>` rehashing (390k inserts for a 256² cloth) — editor "apply preset" hitch.
- [ ] **Cl7.** `ClothSimulator::simulate` / GPU `setSubsteps` unify upper bound — CPU clamps `[1,64]` silently; GPU has no cap. Expose `MAX_SUBSTEPS` constant.
- [ ] **Cl8.** `ClothConfig` validation rejects NaN / ±inf on `particleMass`, `spacing`, `gravity`, `damping` (not just `<= 0`). NaN currently passes the guard and poisons every inverse mass.

### Milestone
Every Phase 10.7 design-doc promise is verified by a test authored **from the design doc, not from the code**. Every dead-code item is either wired or deleted. Phase 11A's determinism contract is backed by regression tests. Phase 10.8 CM3 / CM4 / CM7 prerequisites (`sphereCast`, centripetal spline, arc-length evaluator) are live. Slice 0 ROADMAP claims are grep-true. Slices 14–17 close the second /indie-review's scripting / audio / shader-parity / cloth cross-cutting findings. After Slice 17, the next slice of Phase 10.8 can ship without inheriting remediation debt.

**Scope honesty.** This phase is larger than any single prior Phase 10 sub-phase. If Phase 10.8 Camera Modes is time-critical, Slices 1–3 are genuinely blocking (foundations + 10.7 gaps + safety); Slices 4–13 can be interleaved with Phase 10.8 work or deferred into respective original phases' "correctness update" entries. Revisit the ordering at each slice gate rather than treating the whole phase as atomic.

---

## Phase 11A: Gameplay Infrastructure
**Goal:** The runtime subsystems every Phase 11B gameplay feature consumes — camera shake, screen flash, save-file compression, replay recording, behavior-tree runtime, and AI perception. Split out of the original single Phase 11 so the consumer-before-system dependencies surface at planning time rather than at implementation time.

**Note:** Behavior Trees and AI Perception were previously in Phase 16 (Scripting & Interactivity). They are load-bearing for every Phase 11B enemy / traffic / opponent AI bullet, so they land here instead. Phase 16 retains the advanced AI features that build on them (AI Director pacing, Cutscene, Dialogue).

### Camera Shake System
*Phase 10.7 deferred `clampShakeAmplitude` to this subsystem. The clamp helper is already shipped; this section is what finally consumes it.*
- [ ] `CameraShakeComponent` attached to the camera rig — drives a per-frame offset to the current CameraMode view matrix without disturbing the underlying camera transform.
- [ ] Shake types: impulse (one-shot peak + decay — weapon kick, footfall), continuous (earthquake, rumble), directional (recoil along a vector), trauma (Dead Space-style gore-cam — amplitude tied to recent damage intake).
- [ ] Parameters: amplitude, frequency, duration, falloff curve — authored via Formula Workbench per CLAUDE.md Rule 11, not hand-coded magic constants.
- [ ] Photosensitive safety: amplitude passes through `clampShakeAmplitude(PhotosensitiveLimits)` at the point of application.
- [ ] Composes with Phase 10.8 Camera Modes — shake is applied post-mode, so first-person / third-person / cinematic / vehicle modes all get shake without per-mode code.
- [ ] Editor preview — a slider in the camera inspector that triggers each shake type for tuning.

### Screen Flash / Hit Flash System
*Phase 10.7 deferred `clampFlashAlpha` to this subsystem. The clamp helper is already shipped; this section is what finally consumes it.*
- [ ] Full-screen overlay pass (post-tonemap, pre-UI) with per-flash colour + alpha + envelope curve.
- [ ] Flash types: hit flash (red, short — damage taken), pickup flash (green, short — item gained), stasis flash (cyan, short — Dead Space stasis cast), screen-wipe transition (any colour, long — scene/chapter change), death fade (black, slow — player death).
- [ ] Envelope authored via Formula Workbench (fade-in / plateau / fade-out curve), not hand-coded timing.
- [ ] Photosensitive safety: peak alpha passes through `clampFlashAlpha(PhotosensitiveLimits)` at upload.
- [ ] Queueable — multiple simultaneous flashes blend additively with per-flash colour and independent envelopes.
- [ ] Integrated with Phase 10.8 Post-Processing Effects Suite as the last overlay before UI compositing.

### Save File Compression
- [ ] zstd integration — vendored via FetchContent (same mechanism as existing deps). **Shared with Phase 12 asset packaging** — both consumers use one integration, not two.
- [ ] Compressed binary chunk writer/reader primitive — `writeCompressedChunk(ostream, bytes)` / `readCompressedChunk(istream)` with a versioned header so forward compatibility is explicit.
- [ ] Save-file format header — engine version stamp + scene ID + chunk table + per-chunk zstd payload. Designed so the Phase 11B save/checkpoint system just fills in the chunks.

### Replay Recording Infrastructure
*Moved from the original Phase 11 Replay System — the recording / determinism primitives are infrastructure; the playback / ghost / MP4-export features stay in Phase 11B.*
- [ ] Input-recording replay (baseline)
  - Record per-frame / per-tick input state (keyboard, mouse, gamepad axes + buttons) keyed to the fixed-timestep game tick so replay is bit-identical given deterministic physics
  - Compact serialisation — delta-encode input frames (most ticks nothing changes), compress via Phase 11A zstd integration; target < 1 MB per minute for a racing game
  - Replay file format versioned (`.vreplay`), carries engine version, scene ID, entity seed state at tick 0
- [ ] State-snapshot replay (fallback when determinism is not guaranteed)
  - Periodic full-state snapshots (every N seconds) + interpolation between snapshots for playback
  - Used when non-deterministic subsystems (AI with thread pools, third-party physics tweaks) are active
  - Larger files than input-recording but robust against determinism drift
- [ ] Deterministic-physics contract for input-recording mode
  - Fixed-timestep physics stepping (already present via Jolt integration); document which engine subsystems are deterministic and which are not
  - Per-scene flag for whether the scene is replay-safe via input-recording (off by default until audited)
  - CI check: record a short gameplay sample in debug builds, replay in release, assert position/orientation match within epsilon
- [ ] Testing surface
  - `ReplayRecorder` / `ReplayPlayer` unit tests with a headless scene
  - Determinism harness: N-step input-recording replay against scripted inputs, bit-exact state assertion

### Behavior Tree Runtime
*Moved from Phase 16 — load-bearing for every Phase 11B enemy, traffic, and opponent AI bullet.*

Advanced AI decision-making system — far more expressive than state machines for complex enemy behavior.
- [ ] Behavior tree runtime (tick-based evaluation with running/success/failure states)
  - Composite nodes: Sequence (AND), Selector (OR), Parallel (concurrent)
  - Decorator nodes: Inverter, Repeater, Cooldown, Conditional guard, Timeout
  - Leaf nodes: Actions (move to, attack, patrol, flee) and Conditions (can see player, health low)
- [ ] Behavior tree editor — visual node graph in the editor
  - Drag-and-drop node placement and wiring
  - Live debugging: highlight active node during gameplay
  - Tree templates: save/load reusable behavior patterns
- [ ] Utility AI (optional enhancement) — score-based action selection
  - Each action has a utility function (score based on game state)
  - Highest-scoring action wins — emergent behavior without explicit tree authoring
  - Useful for varied NPC behavior (not all enemies act identically)

### AI Perception System
*Moved from Phase 16 — every Phase 11B enemy / NPC reads from this.*

Sensory system for NPC awareness — sight, sound, and proximity detection.
- [ ] Vision sense — cone-of-vision with configurable angle and range
  - Line-of-sight raycast (blocked by walls, objects)
  - Peripheral vision (wider angle, lower detection speed)
  - Darkness modifier (reduced detection range in dark areas)
  - Last-known-position tracking (investigate where player was last seen)
- [ ] Hearing sense — sound propagation through the environment
  - Sound events (gunshot, footstep, explosion, door opening) generate detection stimuli
  - Distance-based detection radius per sound type
  - Sound occlusion through walls (muffled through closed doors)
  - Investigation behavior (move to sound source location)
- [ ] Alert states — multi-level awareness
  - Unaware → Suspicious → Alert → Combat → Search → Return
  - Smooth transitions between states (suspicion builds over time)
  - Alert propagation (alerted enemies alert nearby allies)
  - Search patterns (systematic area search after losing target)

### Milestone
Every infrastructure piece that Phase 11B gameplay consumes exists as a tested primitive: camera shake drives view-matrix offsets for any camera mode, screen flashes upload through the photosensitive clamp, save files round-trip through compressed chunks, replays record and play back deterministically, and the behavior-tree runtime can evaluate a three-node `Sequence(Patrol, CheckPlayerVisible, Attack)` on a scripted NPC using perception data.

---

## Phase 11B: Gameplay Features
**Goal:** Core gameplay mechanics for action, survival, horror, and racing games — combat, inventory, health, saves, environmental interaction, and vehicle physics. Consumes the render prerequisites from Phase 10.8 and the runtime infrastructure from Phase 11A.

These systems transform Vestige from an exploration/walkthrough engine into a full game engine capable of shipping:

- **Third-person survival horror** — the *Dead Space* archetype (1 / 2 / 3 / Remake): strategic dismemberment, stasis, kinesis, zero-G traversal, diegetic holographic UI, RIG health spine, ammo economy, necromorph-style encounter AI.
- **Action games / RPGs** — combat, inventory, progression, environmental hazards.
- **Arcade racing** — the *Burnout 3 / Burnout: Revenge* archetype: exaggerated arcade vehicle physics, boost meter, takedowns, traffic, crash cameras, damage model.
- **Simulation racing** — the *Gran Turismo* archetype: realistic tyre / suspension / aero / drivetrain physics, licence tests, tuning, photo mode, pit stops, driving assists.

### Combat / Weapon System
- [ ] Weapon component — attach to entity for ranged or melee combat
  - Fire rate, damage, ammo capacity, reload time, spread/accuracy
  - Hitscan weapons (raycast on fire — pistols, rifles, plasma cutters)
  - Projectile weapons (spawn physics projectile — grenades, rockets, bolts)
  - Melee weapons (short-range cone/sphere overlap check — swords, axes, fists)
- [ ] Hit detection with per-bone damage zones
  - Skeleton-aware damage (headshot multiplier, limb damage for dismemberment)
  - Damage types: kinetic, explosive, fire, electric, plasma (configurable per weapon)
  - Hit feedback: blood particles, impact decals (Phase 10.8 Decal System — blood splatter / bullet hole / scorch mark presets), impact sounds, enemy flinch animation
- [ ] Weapon visual effects
  - Muzzle flash (particle + point light burst)
  - Tracer particles for projectiles
  - Impact effects per surface type (sparks on metal, dust on stone, blood on flesh)
  - Shell casing ejection (physics-driven particle)
- [ ] Aiming system
  - Crosshair / targeting reticle with spread indicator
  - Aim-down-sights (ADS) with FOV zoom and reduced spread
  - Laser sight / flashlight attachment (spot light component on weapon)
- [ ] Weapon upgrade system
  - Upgrade slots per weapon (damage, capacity, reload speed, special)
  - Upgrade bench interaction (dedicated UI for weapon modification)

### Health and Damage System
- [ ] Health component — per-entity health pool with damage/heal methods
  - Maximum health, current health, regeneration rate (optional)
  - Damage resistance per type (armor reduces kinetic, insulation reduces fire)
  - Death trigger — fire event on health reaching zero
- [ ] Status effects — all screen-effect hooks consume Phase 10.8 PP suite primitives (vignette, distortion, blur) and Phase 10.8 Decal System
  - Bleeding (damage over time, blood trail decals via Phase 10.8 Decal System)
  - Burning (fire particles, increasing damage, extinguish with action)
  - Stun (brief incapacitation, screen blur — Phase 10.8 PP)
  - Poisoned (screen distortion + green vignette — Phase 10.8 PP, damage over time)
- [ ] Damage feedback
  - Directional damage indicator (show which direction damage came from)
  - Screen effects: red vignette + chromatic aberration pulse (Phase 10.8 PP suite) + camera shake (Phase 11A CameraShakeSystem) + hit flash (Phase 11A ScreenFlashSystem)
  - Low health warning (heartbeat audio, desaturation, heavier breathing) — desaturation + heartbeat pulse land in Phase 10.8 PP "Damage/low-health screen effects"
- [ ] Player death and respawn
  - Death animation/ragdoll transition
  - Death screen overlay with fade (Phase 10.8 PP "Death screen effects" + Phase 11A death-fade ScreenFlash type)
  - Respawn at last checkpoint or save point

### Inventory System
- [ ] Inventory component — grid or list-based item storage per entity
  - Item slots with stack limits and weight limits
  - Item categories: weapons, ammo, health, key items, upgrades, consumables
  - Drag-and-drop inventory UI (ImGui-based for editor, in-game UI for gameplay)
- [ ] Item pickups — world entities that transfer to inventory on interact
  - Proximity detection + interact key
  - Pickup animation and sound
  - Item glow/highlight for visibility
- [ ] Consumable items — use from inventory to apply effects
  - Health packs, stim packs, antidotes, shields
  - Cooldown between uses
- [ ] Equipment slots — weapon, armor, helmet, accessory
  - Visual change on equip (swap mesh/texture on character)
  - Stat modifications from equipped items
- [ ] Loot tables — configurable random item drops from enemies/containers
  - Rarity tiers with drop probability weights
  - Level-scaled loot (harder areas drop better items)

### Save / Checkpoint System
- [ ] Full world state serialization
  - Entity states (position, health, inventory, animation state)
  - Scene state (doors opened, items collected, enemies killed, triggers fired)
  - Player state (position, health, inventory, equipped items, quest progress)
- [ ] Save file management
  - Manual save (at save points or anywhere, configurable per game)
  - Auto-save (periodic or at checkpoints)
  - Quick save / quick load (F5/F9)
  - Multiple save slots with metadata (screenshot thumbnail, playtime, location)
- [ ] Checkpoint system
  - Invisible checkpoint volumes placed in editor
  - Auto-save on checkpoint entry
  - Respawn at last checkpoint on death
- [ ] Save file format
  - Binary serialization for speed, JSON debug export option
  - Version stamping for forward compatibility
  - Compression (zstd) via Phase 11A Save File Compression integration — the infrastructure is owned by 11A and shared with Phase 12 asset packaging.

### Environmental Hazards
- [ ] Hazard zones — volumes that apply damage/effects to entities inside
  - Fire zones (burning damage, fire particles)
  - Electric zones (periodic shock damage, spark particles, stun effect)
  - Toxic zones (poison damage over time, green fog particles)
  - Vacuum/decompression (suffocation timer, objects pulled toward breach)
  - Radiation (increasing damage with exposure time)
- [ ] Hazard visual indicators
  - Particle effects per hazard type
  - Warning signs and environmental cues
  - Screen effects when inside hazard (blur, tint, distortion)
- [ ] Interactive hazards
  - Exploding barrels (damage radius on destruction)
  - Electrical panels (shock on contact, can be disabled)
  - Steam vents (periodic burst, push force on entities)
  - Collapsing floor/ceiling (trigger-based destruction + damage)

### Vehicle Physics & Racing
Dual-archetype support: arcade racing (*Burnout 3 / Burnout: Revenge*) and simulation racing (*Gran Turismo*). The underlying vehicle physics model is shared; arcade vs sim is a tuning and assist-enabled-by-default difference, not a code fork. Vehicles are built on the existing Jolt physics world.

#### Vehicle core (shared)
- [ ] `VehicleComponent` — entity-level wrapper around a Jolt `VehicleConstraint`. Stores chassis body + per-wheel state + drivetrain + engine + aero.
- [ ] Suspension model per wheel — spring stiffness, damping, travel limits, anti-roll bar coupling front/rear; Jolt `WheeledVehicleController` is the baseline.
- [ ] Tyre model — for arcade: simplified grip curve + slip-angle factor. For sim: Pacejka "Magic Formula" longitudinal + lateral with load sensitivity. Per-compound (slick / road / wet / off-road) parameters in `FormulaLibrary`. CPU (per-wheel, ~4/frame); Formula Workbench is the authoring tool (Rule 11).
- [ ] Drivetrain — engine torque curve (RPM → torque), multi-ratio gearbox (manual / automatic / sequential), clutch engagement, centre/front/rear differentials (open / locked / limited-slip / torque-vectoring).
- [ ] Aerodynamics — frontal drag (`0.5·ρ·v²·Cd·A`), lift / downforce per axle with speed-squared scaling, frame-coherent slipstream zone behind each vehicle (reduces drag for trailing cars — matters for both Burnout drafting-boost and GT overtaking).
- [ ] Engine audio — RPM-driven sample interpolation (granular synthesis over recorded low/mid/high-rev samples) + on-throttle / off-throttle crossfade + pitch-shift per gear. Leverages existing `audio_music.{h,cpp}` crossfade primitives. Skid-chirp + backfire + turbo-whistle as separate one-shot layers over the ambient engine bed.
- [ ] Vehicle damage model — two tiers: (a) **cosmetic** (body deformation via vertex displacement + loose parts fracturing off via the existing Phase 8 destruction system) and (b) **functional** (per-component health: engine power loss, steering drift, brake fade, tyre punctures, overheating). Configurable per game for arcade vs sim emphasis.
- [ ] Crash cameras — slow-motion hold on high-energy impacts with free-orbit camera (Burnout's "heartbeat" moment after a wreck). Respects `AccessibilitySettings.reducedMotion` — on, the slow-mo is instant-cut instead of 0.1× lerp.
- [ ] Vehicle cameras — Cockpit (interior first-person with dashboard rendering), Chase (third-person behind + slightly above, spring-smoothed), Hood, Bumper, Photo-mode (free-orbit), Cinematic (spline + look-at chase). Composes with the Phase 10.8 Camera Modes system — "vehicle" is a context that provides camera presets, not a new camera class.
- [ ] Force-feedback / steering-wheel support — `InputDevice::SteeringWheel` enum value alongside Keyboard/Mouse/Gamepad. GLFW doesn't expose FFB directly; plug in SDL2's haptic API or libuinput under a thin shim. Axes: steering, throttle, brake, clutch, handbrake, paddle-shift. Button mapping runs through the existing `InputBindings`. Deferred to a follow-up slice but the binding enum goes in day one.

#### Arcade racing (Burnout 3 / Burnout: Revenge archetype)
- [ ] Boost meter — fills from risk behaviour: near-miss (oncoming traffic within N metres), drafting (trailing another vehicle in slipstream for T seconds), drifting (slip-angle exceeds threshold for T seconds), airtime (wheels off ground for T seconds), crash (triggers a *Crashbreaker* bonus in Revenge). Tunable weights per game.
- [ ] Boost activation — throttle multiplier + FOV kick + radial blur + audio filter sweep. Crashbreaker detonates the wrecked vehicle into a directed explosion (leverages Phase 8 destruction).
- [ ] Takedowns — directed collision mechanic. Raycast + impulse-angle check classifies a collision as a takedown when the initiating vehicle is in boost and the struck vehicle loses control. Registers in a "takedowns" score with a short hitstop + camera cut.
- [ ] Traffic AI — civilian vehicles scripted on splined routes with lane-change behaviour, speed variance, and reactive braking when the player is ahead of them. High-density (Burnout city) vs low-density (rural roads) modes configurable per scene.
- [ ] Road Rage / Crash Mode / Burning Lap / Grand Prix game-mode primitives — each is a scoring ruleset + win condition + HUD layout. `GameMode` interface with plug-in rulesets; engine ships the four named modes as starters and games compose their own.
- [ ] Signature Takedown tracking — per-opponent persistent record of the most spectacular takedown cam; viewable from a garage UI.

#### Simulation racing (Gran Turismo archetype)
- [ ] Driving assists toggles — ABS, Traction Control (TCS), Stability Control (ESC / ASM), active steering, braking line overlay. Each on its own slider (Off / Weak / Standard / Strong) not just a binary. Pro preset turns everything off.
- [ ] Tyre wear + thermal model — tyres accumulate wear % per km based on slip energy; grip degrades with wear. Per-compound temperature windows (cold / optimal / overheated) — off-window tyres slide. Feeds strategy (endurance pit-stop timing).
- [ ] Fuel model — consumption per lap scales with throttle position + gear + lean map. Empty tank = engine stall.
- [ ] Pit stops — pit-lane volume detection, configurable service menu (tyres / fuel / mechanical repair / aero adjustments), time cost per service.
- [ ] Vehicle tuning / garage UI — per-car setup: spring rates, damper bump / rebound, anti-roll bar stiffness front/rear, camber, toe, caster, ride height, differential preload / coast / power, gearbox ratios, final drive, wing angles. Each parameter pushes a coefficient into the formula evaluator; the vehicle physics picks it up on next tick. Save setups per-track.
- [ ] Licence tests — structured gameplay challenge harness. A "licence test" is a scripted sequence with start position + win condition + three-star time target. Reusable for driving schools, time trials, mission objectives in any genre.
- [ ] Opponent AI — racing line adherence (spline-based ideal path), corner braking points, overtake decision tree (follow / commit / abort), rubber-banding toggle for arcade modes (disabled by default for sim).
- [ ] Track authoring — spline-based track surface generator with banking, elevation, start/finish + sector markers; surface-material zones (tarmac / kerb / grass / gravel) feeding the tyre model's grip multiplier.
- [ ] Lap / sector timing + timing HUD — live split against best lap + rival + car-ahead / car-behind.
- [ ] Photo Mode — pause simulation, free-orbit camera, aperture / focal length / shutter / ISO controls (plumbed into the existing post-process chain), LUT preset picker, 16:9 / 2.39:1 / square / vertical crop grids, export to PNG with optional film-grain overlay. Respects accessibility (no motion in the viewport when reduced-motion is on).

#### Testing surface
- [ ] Deterministic unit tests for the tyre model against published Pacejka coefficient tables.
- [ ] Per-wheel suspension step against known rebound curves.
- [ ] Takedown classifier unit tests (collision angles + speed thresholds).
- [ ] Opponent AI behaviour harness — scripted scenarios (car ahead braking late, overtaking opportunity on straight) with deterministic PASS/FAIL.

### Horror Action Polish (Dead Space archetype)
Fills the gaps between the generic Combat / Health / Inventory / Save systems above and the *Dead Space* feature set specifically. Most generic horror-action pieces (dismemberment, stasis, ragdoll, decal gore, ADS, weapon upgrade bench, vacuum zones, checkpoint saves) already live in earlier phases or in Phase 11 above — this section covers what remains.

- [ ] **Kinesis (telekinesis) component** — pick-up-and-throw tied to a dedicated input. Grab any `PhysicsBody` tagged `kinesisTarget`; charge launches it with a force magnitude + direction from the crosshair. Used both for combat (throw spikes / limbs) and puzzles (move heavy crates, align circuits). Leverages the existing `physics/grab_system` primitive.
- [ ] **Stasis gun + ammo economy** — ties the already-shipped `StasisSystem` to a first-class weapon with a rechargeable ammo pool. Recharge stations in the world top it up; trigger slows one tagged body. Slow-mo factor configurable per weapon tier.
- [ ] **Zero-G traversal mode** — 6DOF navigation (yaw + pitch + roll + three-axis thrust) plus magnetic-boot floor-walking at low thrust. CharacterController gains a `GravityMode` (`Normal / Magnetic / FreeFall`) enum. `Magnetic` lets the player walk arbitrary up-vectors (walls / ceilings) by snapping orientation to the tagged surface normal. `FreeFall` exposes thruster inputs. Scripted transitions between modes via trigger volumes.
- [ ] **World-space UI pathway** — new `UIElement::worldProjection` support in `engine/ui/ui_in_world.{h,cpp}` (not yet shipped — first bullet under this section). World-space quads can bind to a bone socket so the UI follows the rig at all times. Prerequisite for every other diegetic-UI bullet in this section.
- [ ] **Diegetic holographic UI** — the Phase 11B UI guidance is that *no HUD is drawn in screen-space for this archetype*. Instead, health / stasis / ammo render as world-space quads projected from the player's spine (RIG) + weapon via the world-space UI pathway above. Accessibility: a fallback 2D HUD can be toggled in `Settings` for partially-sighted players.
- [ ] **RIG health spine** — specific prefab that reads `HealthComponent` and renders a 5-segment health bar on the player's spine. Third-person visible; in first person only the peripheral glow is shown. Damage pulses the glow.
- [ ] **In-world holographic panels** — interactable door locks, lore logs, objective markers, and upgrade bench UIs all render as diegetic world-space holograms rather than screen overlays. Consumes the world-space UI pathway defined above. Animated scan-line shader variant + chromatic-aberration flicker (Phase 10.8 PP suite chromatic aberration is the source effect).
- [ ] **Necromorph-style encounter AI** — behaviour-tree node set for "stalk / ambush / wave-attack / play-dead" patterns. Ambush spawn from vent / ceiling colliders tagged `spawner`; "play dead" nodes use the existing ragdoll system for a faked-death pose and re-animate on trigger. Weak-point targeting — AI prefers to expose limbs to the player so dismemberment is rewarded.
- [ ] **Audio horror stingers** — scripted jump-scare and tension beats wired through the already-shipped `MusicStingerQueue` and `audio_ambient`. Editor surface: a "Scare Marker" component placed in the scene with a delay / trigger-condition / audio clip triple.
- [ ] **Chapter system** — narrative-progression scaffold above the save system. A *chapter* is a named subregion with entry/exit triggers, a title-card prefab, and an auto-checkpoint on entry. Saves record `currentChapter` alongside the scene snapshot.
- [ ] **Weapon alt-fire modes** — most Dead Space weapons have a secondary trigger (Plasma Cutter horizontal ↔ vertical, Line Gun beam ↔ mine). `WeaponComponent` gets a `secondaryFire` sibling to `fire` with its own stats + input binding (default: right-click in ADS-off state, toggle weapon orientation in ADS-on).
- [ ] **Ammo / health scarcity tuning helpers** — survival-horror economy depends on drops being rare but not *too* rare. Editor tool: simulated playthrough harness that reports expected ammo / health surplus per chapter against a target curve, so designers can retune loot tables without full playthroughs.
- [ ] **Co-op session support (Dead Space 3 archetype)** — scope note: the generic networking primitives live in **Phase 20**. This bullet just calls out that the horror-specific design — divergent player perspectives, co-op-only encounters, revive mechanic — needs per-scene authoring and a split-inventory flag once Phase 20 lands. Not shippable before Phase 20.

### Replay Features
*Infrastructure (recording, state snapshots, determinism contract, recorder/player tests) lives in Phase 11A. This section covers the user-facing replay features built on top of that infrastructure.*

- [ ] Replay playback controls
  - Play / pause / step / scrub along the timeline, 0.25× – 4× speed, reverse playback for input-recording mode (cached snapshots make this feasible)
  - Free-flying "replay camera" independent of the player entity — orbit, dolly, spline-follow; respects the Phase 10.8 Camera Modes system (first-person / third-person / cinematic)
  - Multi-angle replay — record from any camera and let the viewer swap between them post-race (broadcast TV pattern)
- [ ] Ghost / split-time overlays (racing-specific)
  - Overlay the fastest-lap replay as a translucent ghost car
  - Per-sector split times shown in HUD against the ghost's time
  - Support for importing ghost replays from other users (leaderboard integration)
- [ ] Replay editor integration
  - Editor panel to load / trim / export a replay
  - Export to MP4 via the Phase 12 ffmpeg pipeline (offline render pass — not real-time; renders the replay frame-by-frame at target resolution / frame rate with post-processing at full quality). The ffmpeg integration is owned by Phase 12; this bullet is its primary consumer.
  - Waypoint markers for interesting moments, exportable as replay "chapters"
- [ ] Privacy & opt-in
  - Replays record inputs + entity state — never phone home, only written locally
  - Per-game opt-in: a game built with Vestige decides whether to enable recording at all (engine primitive is off by default)

### Milestone
A complete gameplay loop for three archetypes:

- **Horror action (Dead Space-shaped)** — explore a RIG-spined character through low-light corridors, dismember necromorphs with upgradeable weapons, use stasis and kinesis for combat and puzzles, traverse zero-G sections, save at checkpoints, and progress through chapters. All HUD is diegetic.
- **Generic action / RPG** — upgradeable weapons, inventory management, environmental hazards, save / load, NPC interactions.
- **Racing (arcade or sim)** — drive a fully-tuned vehicle on a splined track, race against AI opponents or traffic, trigger boost via risk play (Burnout) or manage tyre + fuel strategy (GT), take photos in Photo Mode, review the session as a replay.

All systems configurable in the editor.

---

## Phase 12: Distribution
**Goal:** Package and distribute the application — both finished experiences and the engine itself.

### Cross-Platform Compilation
The engine targets Linux and Windows from the start (CLAUDE.md). The codebase is mostly portable (CMake, GLFW, GLM, OpenGL, stb, FreeType — all cross-platform), but a small number of platform-specific calls need `#ifdef` guards and the build pipeline needs configuring for both platforms.

#### Platform Portability Fixes
- [ ] `localtime_r` → `localtime_s` on MSVC (frame_diagnostics.cpp, visual_test_runner.cpp, recent_files.cpp)
- [ ] `getenv("HOME")` → `getenv("USERPROFILE")` on Windows (frame_diagnostics.cpp, window.cpp)
- [ ] `/proc/self/status` memory tracking → Windows API equivalent (memory_tracker.cpp)
- [ ] ASAN/UBSAN flags: MSVC uses `/fsanitize=address` syntax, GCC/Clang use `-fsanitize=` (CMakeLists.txt)
- [ ] `_FORTIFY_SOURCE` is GCC/Clang-only — skip on MSVC (CMakeLists.txt)
- [ ] Path separator handling (`/` vs `\`) — use `std::filesystem::path` where needed
- [ ] Verify `std::filesystem` links without `-lstdc++fs` on target compilers (GCC < 9 needs it)

#### Windows Build Setup
- [ ] Verify CMake generates valid MSVC / Ninja build on Windows
- [ ] Test with Visual Studio 2022 (MSVC 17) and MinGW-w64
- [ ] Resolve any GLFW / OpenGL context differences on Windows (driver-specific)
- [ ] Verify all FetchContent dependencies build cleanly on MSVC
- [ ] Windows-specific icon and manifest for the executable

#### CI / CD Pipeline
- [ ] GitHub Actions workflow: Linux build (GCC + Clang)
- [ ] GitHub Actions workflow: Windows build (MSVC)
- [ ] Automated test run on both platforms (unit tests + visual test runner)
- [ ] Build artifacts: downloadable binaries for both platforms per commit/tag
- [ ] Release packaging script (zip/tar with executable + assets)

#### Cross-Compilation (Optional)
- [ ] MinGW-w64 toolchain file for building Windows binaries from Linux
- [ ] Verify GLFW cross-compiles cleanly with MinGW
- [ ] Test cross-compiled binary on Windows (or Wine)

### Asset Pipeline
- [ ] Texture compression (BC7/KTX2 for desktop, ASTC for mobile — compress on import, load directly to GPU)
- [ ] Automatic mipmap generation with quality filtering options
- [ ] Asset cooking / baking (preprocess models, textures, shaders into optimized binary format)
- [ ] Asset manifest and dependency tracking (know exactly what each scene needs)
- [ ] Hot-reload during development (detect changed assets, reload without restarting)
- [ ] Offline video rendering pipeline (ffmpeg) — frame-by-frame render to MP4 / WebM / image sequence. Primary consumer is the Phase 11B replay editor's MP4 export; secondary consumers are future cutscene rendering and editor screenshot-burst capture. Integration is a thin `ffmpeg` invocation layer, not a library link, so the tool stays optional at runtime and only required for export.

### Compression and Size Optimization
- [ ] GPU texture compression pipeline (BC7/BC1 via KTX2 — ~4x VRAM reduction, faster loads)
- [ ] Asset packaging (bundle assets into compressed archives with zstd/LZ4 for distribution)
- [ ] Animation data compression (16-bit half-float positions, smallest-3 quaternion encoding)
- [ ] Mesh compression (Draco or meshoptimizer for glTF geometry data)
- [ ] Strip source assets from builds (exclude .blend, raw PSD/EXR source files)
- [ ] Binary size optimization (link-time optimization, dead code stripping for release builds)

### Application Distribution
- [ ] Steam SDK integration
- [ ] Steam achievements (if applicable)
- [ ] Installer/packaging for Windows
- [ ] Linux AppImage or Flatpak packaging
- [ ] Save/load system (player position, settings)
- [ ] Controller button prompts (show correct icons for connected controller)
- [ ] Loading screens (with progress indicators for large scenes)

### Scene Packaging and Sharing
- [ ] Export scene as standalone package (scene + all referenced assets in one archive)
- [ ] Import packaged scenes from other creators
- [ ] Scene versioning — track which engine version a scene was built with

### Milestone
Application published on Steam. Scenes can be packaged and shared between users.

---

## Phase 13: Advanced Rendering
**Goal:** Push visual fidelity with modern techniques.

### Screen-Space Effects
- [ ] Screen-space reflections / SSR (real-time reflections on wet floors, polished bronze/gold — complements IBL)
- [ ] Screen-space global illumination / SSGI (one-bounce diffuse indirect lighting from depth+color buffers — practical "free" GI layer, complements probe-based approaches)
- [ ] Depth of field (cinematic focus effect for guided tours and screenshots)
- [ ] Motion blur (per-object and camera-based)

### Advanced Materials
- [ ] Subsurface scattering / SSS (light bleeding through thin materials — linen curtains, wax candles, marble, skin; hybrid screen-space diffusion approach or ReSTIR-path-tracing diffusion when RT available; ref: NVIDIA SIGGRAPH 2025). *Basic per-material SSS (thickness + transmission + scattering distance + wrap lighting) lands in Phase 10 "Rendering Enhancements"; this Phase 13 item is the hybrid-screen-space / ReSTIR upgrade.*
- [ ] Anisotropic reflections (brushed metal, hair, silk fabrics)
- [ ] Strand-based hair and fur rendering (physically-based hair model with proper light scattering — relevant for animal fur, priestly garment fringes; ref: MachineGames/Indiana Jones, SIGGRAPH 2025)
- [ ] Neural texture compression (2-4x memory reduction via trained neural decompression in shaders — forward-looking, requires cooperative vector hardware support)

### Global Illumination
- [ ] Baked lightmaps with SH-fit storage (WishGI approach — pre-computed GI for static architectural scenes stored as per-mesh SH probes via inverse distribution; ~5% of classic lightmap memory, fragment-shader sampling with no extra pass; ref: Zhu et al. "WishGI" SIGGRAPH 2025 — https://dl.acm.org/doi/10.1145/3730935)
- [ ] Light probes (capture local lighting conditions at probe positions — varying lighting between rooms)
- [ ] Reflection probes (local cubemap captures for accurate indoor reflections — Holy Place vs Holy of Holies)
- [ ] Light probe blending (smooth transitions between probe volumes)
- [ ] Real-time irradiance probe GI (idTech 8 "Fast as Hell" approach — stochastic probe sampling with surfel shading, fully dynamic, proven at 60 FPS on consoles; replaces baked lighting with real-time probes; ref: Sousa SIGGRAPH 2025 — https://advances.realtimerendering.com/s2025/content/SOUSA_SIGGRAPH_2025_Final.pdf)
- [ ] Surfel + SH probe hybrid (surfels sample indirect irradiance via software SDF ray-marching, write into the SH probe grid, temporal accumulation smooths residual noise; geometry-agnostic, no hardware RT required; pairs the idTech 8 surfel allocator with Vestige's planned SH grid)
- [ ] Brixelizer-style software ray-traced GI via sparse SDF fields (compute-only, RDNA2-feasible — primary software-RT fallback for diffuse/specular indirect before the Vulkan/HW-RT backend lands; ref: AMD FidelityFX Brixelizer GI — https://gpuopen.com/fidelityfx-brixelizer/)
- [ ] Radiance cascades (Alexander Sannikov's approach — constant-cost GI independent of scene complexity and light count; 2D proven in production, 3D extension is active research area; ref: Holographic RC arXiv 2505.02041, JTLee98 3D Vulkan PoC)
- [ ] ReSTIR GI (reservoir-based spatiotemporal importance resampling for indirect illumination — dramatically improves convergence when hardware RT is available)

### Vulkan and Ray Tracing
- [ ] Vulkan rendering backend (alternative to OpenGL)
- [ ] Vulkan descriptor heap (VK_EXT_descriptor_heap — simplified resource binding, replaces legacy descriptor set model)
- [ ] Ray tracing — reflections (hardware-accelerated on supported GPUs)
- [ ] Ray tracing — ambient occlusion
- [ ] Ray tracing — global illumination
- [ ] ReSTIR DI (reservoir-based spatiotemporal importance resampling for direct illumination — enables hundreds of shadow-casting lights with stochastic evaluation at fixed cost; ref: NVIDIA RTXDI)
- [ ] Partitioned top-level acceleration structures / PTLAS (divide scene into clusters, selectively rebuild only changed partitions — 100x faster BVH updates; ref: NVIDIA RTX Mega Geometry, DXR 2.0 CLAS — NVIDIA-only today via `VK_NV_cluster_acceleration_structure` / `VK_NV_partitioned_acceleration_structure`; watch for `VK_KHR_` upstreaming before RDNA2 becomes viable)
- [ ] Opacity micromaps (encode alpha transparency directly in the acceleration structure — eliminates expensive any-hit shaders for foliage, fences, curtains)
- [ ] Shader execution reordering / SER (group coherent RT shader invocations — up to 2x RT performance; mandatory in SM 6.9 / Vulkan equivalent)
- [ ] Software ray tracing fallback (screen-space ray marching + voxel/probe traces for GPUs without hardware RT — the most widely deployed approach in shipping games, e.g. UE5 Lumen software path)
- [ ] Deferred rendering pipeline
- [ ] Visibility buffer rendering (store triangle ID + barycentric coords instead of full G-buffer — compute-based material dispatch and deferred texturing; ref: idTech 8, DOOM: The Dark Ages)

### Tessellation and Geometry
- [ ] Tessellation (adaptive subdivision for nearby geometry)
- [ ] Displacement mapping (height maps that actually move geometry, unlike normal maps)
- [ ] Tessellated water surfaces (wave simulation via height maps)
- [ ] Tessellated terrain (adaptive detail for landscapes)

### Shadow Techniques
- [ ] Virtual shadow maps (massive virtual texture shadow map — only allocate tiles visible to camera, consistent detail at all distances, eliminates cascade seams; data-structure portion feasible on OpenGL 4.5 via `ARB_sparse_texture`, mesh-shader-optimal version deferred to the Vulkan backend)
- [ ] Percentage-closer soft shadows / PCSS (contact-hardening shadows — sharp near caster, soft further away; author the filter-radius curve via the Formula Workbench)
- [ ] HypeHype stochastic tile-based lighting (first, simpler many-lights rung — two-stage tile resampling with stratified reservoirs, no RT or mesh shaders required; ref: Lempinen SIGGRAPH 2025 — https://advances.realtimerendering.com/s2025/content/s2025_stb_lighting_v1.1_notes.pdf)
- [ ] MegaLights stochastic area shadows (fixed-budget evaluation of hundreds-to-thousands of shadow-casting lights via tile-based reservoir sampling + temporal reuse; SVGF-style denoiser; supersedes PCSS for area lights once SDF ray-marching is in place; ref: Narkowicz & Costa SIGGRAPH 2025 — https://advances.realtimerendering.com/s2025/content/MegaLights_Stochastic_Direct_Lighting_2025.pdf)

### Upscaling
- [ ] Render scale slider (render at 50%–100% internal resolution, upscale to display resolution)
- [ ] AMD FSR 1.0 spatial upscaler (open-source, GPU-agnostic, single post-process pass)
- [ ] AMD FSR 2.x temporal upscaler (motion-vector-based, requires engine motion vectors and depth; higher quality than spatial; works on all GPUs including RDNA 2 / RX 6600)
- [ ] Custom spatial upscaler (Lanczos/bicubic + CAS sharpening — built from scratch as a learning exercise)
- [ ] Frame generation (interpolate additional frames between rendered frames — AMD FSR 3.x or custom optical-flow-based approach; doubles perceived framerate at the cost of latency)

### Anti-Aliasing and Filtering
- [ ] Global anisotropic filtering quality setting (1x/2x/4x/8x/16x — applied to all scene textures)
- [ ] Specular anti-aliasing (Toksvig or LEAN mapping — reduces distant surface shimmer from normal maps)

### GPU-Driven Rendering
- [ ] **[Highest-ROI OpenGL 4.5 item]** GPU-driven MDI with Hi-Z occlusion culling (compute-shader Hi-Z build + `glMultiDrawElementsIndirectCount`; expected 10-30% FPS gain on >1k-object scenes; ref: Anno 117 Pax Romana GDC 2026, idTech 8 SIGGRAPH 2025 — https://schedule.gdconf.com/session/all-rays-lead-to-rome-next-gen-graphics-in-anno-117-pax-romana/915067)
- [ ] GPU frustum and occlusion culling (Hi-Z occlusion culling in compute shader — skip objects hidden behind other objects; lands as part of the GPU-driven MDI item above)
- [ ] Variable-rate shading / variable-rate compute (control shading rate per screen region — full rate for detail areas, reduced rate for flat surfaces; ref: idTech 8 VRCS)
- [ ] GL_EXT_mesh_shader integration (OpenGL mesh shaders — replace vertex/geometry pipeline with task+mesh shader stages for GPU-driven geometry processing; available on AMD RDNA2+ via Mesa, avoids requiring Vulkan)
- [ ] Bindless textures and resources (eliminate texture binding overhead — all textures resident and GPU-addressable)
- [ ] Shader language unification via Slang (migrate GLSL shaders to Slang IR — compiles to SPIR-V for OpenGL 4.5 today via `ARB_gl_spirv` + `ARB_spirv_extensions`, and to Vulkan + DXIL + OptiX + CUDA for future backends; supports generics, interfaces, differentiable shaders; Khronos governance; production use in Source 2 (CS2, Dota 2); prerequisite for low-friction Vulkan backend — ref: https://shader-slang.org/)

### Performance
- [x] Frustum culling (skip objects outside camera view)
- [ ] Volumetric lighting (god rays, fog) — *Basic god rays and volumetric fog land in Phase 10 "Fog, Mist, and Volumetric Lighting"; this Phase 13 item covers the froxel-volume + temporal-reprojection rendering upgrade. Phase 15 weather modulates the Phase 10 primitives; Phase 13 upgrades what those primitives render.*

### VR / Immersive Rendering
- [ ] OpenXR integration (cross-platform VR/AR runtime)
- [ ] Stereoscopic rendering (dual-eye viewpoints with correct IPD)
- [ ] VR locomotion system (teleport, smooth movement, comfort options)
- [ ] VR interaction (grab, point, inspect objects)
- [ ] VR performance target (90 FPS stereo — requires aggressive optimization)
- [ ] Foveated rendering (reduce detail in peripheral vision on supported headsets)

### Milestone
Hybrid rendering with software and hardware ray-traced effects, real-time global illumination (probe-based and/or surfel GI), GPU-driven rendering pipeline, VR walkthroughs, and tessellation on supported hardware. Scalable from integrated GPUs (SSGI + probes) to discrete RT hardware (ReSTIR + full path tracing).

### 2026-04 Research Update — GDC 2026 / SIGGRAPH 2025 shader survey

Sourced from a 2026-04-19 research sweep (GDC 2026 rendering track + 2024-2026
general shader research). Each item is rated ★1-5 on impact-per-effort for
Vestige specifically (OpenGL 4.5, AMD RX 6600 target, 60 FPS budget). Items
already listed above are cross-referenced so we don't duplicate work.

**Newly identified (add to Phase 13):**

- [ ] ★★★★★ **Spatiotemporal blue noise (STBN) textures** — swap the engine's
      existing dither / SSAO / SSR / stochastic-sample noise sources for
      NVIDIA STBN masks; immediate TAA convergence improvement for
      essentially zero runtime cost. Drop-in texture swap. Ref:
      https://github.com/NVIDIA-RTX/STBN · https://arxiv.org/pdf/2112.09629 ·
      FAST thresholding 2025 follow-up:
      https://blog.demofox.org/2025/05/27/thresholding-modern-blue-noise-textures/
- [ ] ★★★★★ **Screen-space indirect lighting with visibility bitmask
      (SSILVB)** — near-drop-in replacement for the existing SSAO/GTAO pass
      that adds thin-surface-correct one-bounce indirect color. ~1 ms on a
      6950XT, pure GLSL available. Ref: https://arxiv.org/abs/2301.11376 ·
      https://cdrinmatane.github.io/posts/ssaovb-code/
- [ ] ★★★★ **Two-level BVH compute ray tracer (TLAS/BLAS)** — prerequisite
      infrastructure for hybrid SSR, ReSTIR, soft shadows, area-light
      shadows; compute-only so it runs on RDNA2 under OpenGL. Ref:
      https://jacco.ompf2.com/2022/06/03/how-to-build-a-bvh-part-9a-to-the-gpu/ ·
      https://interplayoflight.wordpress.com/2020/11/01/adding-support-for-two-level-acceleration-for-raytracing/
- [ ] ★★★ **Hybrid SSR → compute-RT reflection fallback** — keep the SSR
      pass as the fast primary, fall back to a BVH trace only on screen-edge
      miss. Removes SSR's worst artifact without the cost of pure-RT
      reflections. Depends on two-level BVH above. Ref:
      https://gpuopen.com/fidelityfx-sssr/ ·
      https://gpuopen.com/fidelityfx-hybrid-reflections/
- [ ] ★★★ **Physical camera post-process stack** — bokeh DoF (gather-based,
      hexagonal/circular), anamorphic lens flare via streak kernel,
      sub-pixel chromatic aberration on highlights, physical vignette.
      Each curve (bokeh kernel shape, flare falloff, CA radial offset) is a
      natural Formula Workbench target. Ref:
      https://extremeistan.wordpress.com/2014/09/24/physically-based-camera-rendering/

**Already in the roadmap — priority hints from the survey:**

- **Volumetric froxel fog (Phase 13 "Volumetric lighting")** ★★★★★ — single
      biggest realism uplift for Tabernacle/Temple interiors (shafts of
      light, incense haze). Elevate from back-burner to near-term once SH
      probe grid lands. Ref: Frostbite
      https://www.ea.com/frostbite/news/physically-based-unified-volumetric-rendering-in-frostbite
- **FSR 2.x temporal upscaler (Phase 13 "Upscaling")** ★★★★★ — GL port
      already exists and uses Vestige's existing motion-vector + TAA
      plumbing. Unlocks headroom for every later feature. Ref:
      https://github.com/JuanDiegoMontoya/FidelityFX-FSR2-OpenGL
- **Sparse virtual shadow maps (Phase 13 "Virtual shadow maps")** ★★★★ —
      Stratus open-source GL4.6 reference impl exists; solves CSM flicker
      and edge-resolution issues in large interiors. Ref:
      https://ktstephano.github.io/rendering/stratusgfx/svsm
- **GPU-driven MDI + Hi-Z (Phase 13 "GPU-Driven Rendering")** ★★★★ —
      foundation for every later geometry/RT feature. Already flagged as
      "highest-ROI OpenGL 4.5 item" — the survey reinforces this.
- **Radiance Cascades (Phase 13 "Radiance cascades")** ★★★★ — noise-free,
      scene-complexity-independent GI; strong long-term architecture bet
      complementary to the SH probe grid. Ref:
      https://80.lv/articles/radiance-cascades-new-approach-to-calculating-global-illumination ·
      https://jason.today/rc

**Explicitly deferred (keep on the list, but not worth sprinting now):**

- **MegaLights (Phase 13 "MegaLights")** — requires SVSMs + two-level BVH
      first; re-evaluate once those land.
- **ReSTIR DI/GI (Phase 13)** — requires BVH + reservoirs plumbing; slot
      in after the hybrid reflection / software-RT items prove out.
- **Variable-rate shading (Phase 13 "Variable-rate shading")** — driver
      risk on Mesa AMD; verify exposure before investing.
- **Mesh shaders via `GL_NV_mesh_shader`** — NV-only today; wait for the
      Vulkan backend (Phase 13 "Vulkan rendering backend") before pursuing.
- **3D Gaussian splatting** — asset-pipeline feature, not a core-renderer
      feature; revisit as part of the open-source examples.
- **Neural radiance cache** — GL4.5 lacks the cooperative-matrix ops that
      make ML-in-shader practical; defer until Vulkan + matrix extensions.

---

## Phase 14: Adaptive Geometry System
**Goal:** Handle massive geometric complexity automatically — original approach, not a copy of any existing engine.

### Problem Statement
High-fidelity 3D scenes (like a fully detailed Solomon's Temple) can contain hundreds of millions of triangles. Without automatic geometry management, artists must manually create LOD variants and the engine wastes GPU time rendering detail the player can't see.

### Research Direction (Original Approach)
Rather than replicating existing commercial solutions, Vestige will explore its own approach to automatic geometry management. Possible research areas:
- [ ] Automatic mesh simplification (quadric error metrics, academic literature)
- [ ] Cluster-based mesh decomposition (splitting meshes into independently cullable/LOD-able chunks — meshlets of ~64-256 triangles)
- [ ] Screen-space error-driven selection (choose cluster detail based on projected pixel coverage)
- [ ] Compute shader-driven culling and selection pipeline
- [ ] Virtual geometry streaming (load/unload clusters on demand from disk)
- [ ] Mesh shader integration (GL_EXT_mesh_shader on OpenGL / VK_EXT_mesh_shader on Vulkan — available on RDNA2+; task shaders cull meshlet clusters, mesh shaders emit surviving triangles)
- [ ] Micropolygon rendering (ref: Ubisoft Anvil engine, GDC 2026 — GPU-driven streaming architecture with automatic LOD, no manual LOD creation, eliminates popping)
- [ ] Hybrid rasterization (hardware for large triangles, compute for sub-pixel)
- [ ] RT acceleration structure co-management (partitioned TLAS that tracks geometry LOD — when geometry detail changes, only affected BVH partitions rebuild; ref: NVIDIA RTX Mega Geometry foliage system)
- [ ] Novel approaches: SDF-based geometry, surfel-based rendering, or other non-traditional representations

### Design Principles
- Solve the problem from first principles — don't reverse-engineer other engines
- Build on published academic research (SIGGRAPH papers, GPU Pro/Gems, etc.)
- Innovate where possible — this is an opportunity to do something new
- Incremental: start with traditional LOD, evolve toward fully automatic

### Milestone
A system that lets artists import film-quality assets and the engine automatically manages complexity in real time.

---

## Phase 15: Atmospheric Rendering
**Goal:** Procedural sky, weather, time of day, and dynamic atmosphere.

**Note:** The Atmosphere & Weather domain system (Phase 9B) wraps `EnvironmentForces` and provides the weather state machine, wind field, and system logic. This phase focuses on the advanced *rendering* side: procedural sky, volumetric clouds, precipitation rendering, and atmospheric scattering. The weather controller logic from Phase 9B drives the rendering features here.

### Procedural Sky
- [ ] Procedural sky model (Rayleigh/Mie scattering) — physically-based atmosphere
  - Wavelength-dependent scattering for realistic blue sky, orange sunsets, red sunrises
  - Configurable atmosphere parameters: planet radius, atmosphere height, scattering coefficients
  - Multiple scattering approximation for accurate horizon color
- [ ] Full day/night cycle with sun/moon positioning
  - Sun follows ecliptic path based on latitude and time of year
  - Moon phase cycle (new → full → new) with correct illumination
  - Smooth transitions through golden hour, twilight, and night
- [ ] Dynamic time-of-day lighting (sun color/intensity changes)
  - Directional light color and intensity automatically track sun position
  - Ambient light and shadow intensity adjust with time of day
  - Interior lighting (torches, lamps) becomes more prominent at night
- [ ] Stars and night sky
  - Star field from catalog data or procedural placement
  - Milky Way band (subtle texture overlay)
  - Star brightness modulated by atmospheric extinction near horizon

### Volumetric Clouds
- [ ] Ray-marched volumetric clouds — 3D noise-based cloud shapes rendered via ray marching
  - Cloud layer altitude, thickness, and coverage controls
  - Worley + Perlin noise combination for realistic cloud shapes (cumulus, stratus, cirrus)
  - Detail noise for cloud edge erosion and internal structure
  - Light scattering through clouds: silver lining, dark bases, beer-powder approximation
  - Temporal reprojection for performance (quarter-res marching, 16-frame accumulation)
- [ ] Cloud shadow projection — clouds cast soft shadows on terrain and scene geometry
  - Shadow map from cloud density projected along sun direction
  - Moving shadow patterns as clouds drift across the sky
- [ ] Cloud animation — wind-driven cloud movement and morphing
  - Global wind vector drives cloud layer drift (shared with cloth/particle wind system)
  - Noise offset animation for cloud evolution over time
  - Weather-dependent coverage (clear sky → partly cloudy → overcast transitions)

### Weather System
- [ ] Weather controller — central system that drives all weather-related subsystems
  - Weather state machine: Clear, Cloudy, Overcast, Light Rain, Heavy Rain, Storm, Snow, Dust Storm
  - Smooth transitions between weather states (gradual cloud buildup, wind increase)
  - Global wind vector shared with cloth simulation, particle systems, foliage, and clouds
  - Per-scene weather configuration (desert scenes get dust storms, not snow)
  - Editor UI: weather state selector, transition speed, wind direction/strength
- [ ] Rain
  - GPU particle precipitation (thousands of rain streaks rendered as stretched quads)
  - Rain collision with surfaces — splash particles on impact (roofs, ground, water)
  - Wet surface material modification: increased roughness, darker albedo, specular highlights
  - Rain puddle accumulation on flat surfaces (animated normal-mapped puddle growth)
  - Rain sound integration (ambient rain loop, splash sounds, intensity-matched volume)
  - Rain streaks on screen (optional first-person rain-on-lens effect)
  - Shelter detection: rain blocked by roofs and overhangs (raycast or shadow map test)
- [ ] Snow
  - GPU particle snowflakes (slower, larger particles with gentle drift and tumble)
  - Snow accumulation on surfaces (gradual white overlay on upward-facing geometry)
  - Accumulation depth map for ground coverage
  - Footprint trails in accumulated snow (deformation texture at character position)
  - Snow material: subsurface scattering for translucent snow glow
- [ ] Hail
  - Larger, faster particles than rain with bounce on impact
  - Impact sound effects (metallic ping on bronze altar, thud on fabric)
  - Cloth interaction: hail impacts add impulse to cloth simulation particles
- [ ] Dust storms / sandstorms
  - Volumetric particle clouds with directional wind
  - Reduced visibility (fog density increase during storm)
  - Sand accumulation on surfaces (thin layer of sand-colored overlay)
  - Desert-appropriate for Tabernacle scenes
- [ ] Overcast sky
  - Cloud coverage increases to full overcast (uniform grey layer)
  - Reduced directional light intensity, increased ambient
  - Flatter, more diffuse shadows (shadow softness increases with cloud coverage)
  - Color temperature shift (cooler, bluer ambient light under overcast)
- [ ] Fog and mist density changes with weather
  - Weather controller adjusts Phase 10 fog parameters dynamically
  - Morning mist that burns off as sun rises (density tied to time of day)
  - Thick fog during certain weather states (visibility drops to 20-50m)
  - Volumetric fog density modulated by weather (heavier in rain, lighter in clear)
- [ ] Lightning (storm weather state)
  - Flash illumination (brief full-scene directional light burst)
  - Branching lightning bolt rendering (procedural line segments or billboard)
  - Thunder sound with distance-based delay
  - Brief shadow suppression during flash (everything lit uniformly)
- [ ] Wind gusts tied to weather
  - Weather controller sets global wind strength/direction
  - Cloth panels, particle systems, foliage, and clouds all respond to same wind
  - Calm in clear weather, moderate in rain, strong gusts in storms
  - Wind direction shifts during storm transitions

### God Rays with Clouds
- [ ] Crepuscular rays through cloud gaps — volumetric light shafts from sun through cloud openings
  - Cloud density used as shadow volume for volumetric lighting
  - Rays visible in dusty/humid atmosphere (Phase 10 volumetric fog integration)
  - Anti-crepuscular rays (converging rays opposite the sun) for sunset/sunrise

### Milestone
A living sky with dynamic clouds, day/night transitions, weather effects (rain, snow, hail, dust storms), and atmospheric lighting that transforms the scene mood.

---

## Phase 16: Scripting and Interactivity
**Goal:** Allow scene creators to add behavior and interactivity without writing C++.

**Note:** Basic visual scripting (event-to-action chains) and basic AI/navigation are introduced in Phase 9C/9E as domain systems. This phase covers the *advanced* features: behavior trees, AI perception, AI director, cutscene system, and dialogue. The node graph infrastructure from Phase 9E is the foundation for visual scripting here.

### Visual Scripting
- [ ] Node-based visual scripting system (connect trigger → action blocks in the editor)
- [ ] Event triggers: player enters area, player looks at object, player presses interact key, timer
- [ ] Actions: open/close door, play sound, show text, move object, toggle light, teleport player
- [ ] Waypoint system — define guided tour paths the player can follow
- [ ] Trigger zones — invisible volumes that fire events when the player enters

### Simple Behaviors
- [ ] Animated objects (rotating, bobbing, swinging — set in the inspector)
- [ ] Door component (opens on interact, with animation)
- [ ] Torch component (auto-generates fire particles + flickering light)
- [ ] Water component (auto-generates water surface shader + sound)

### AI and Navigation
- [ ] Navigation mesh generation (automatic walkable surface detection from scene geometry)
- [ ] NavMesh pathfinding (A* or similar on the navigation mesh)
- [ ] NPC component (entity that follows paths, plays animations, responds to triggers)
- [ ] NPC patrol routes (editor-defined waypoint sequences)
- [ ] Crowd simulation (multiple NPCs navigating without colliding — Temple visitors, priests)
- [ ] Line-of-sight checks (NPCs react to player visibility)

### Behavior Trees
*Moved to Phase 11A — see "Behavior Tree Runtime" there. The BT runtime, editor, and utility-AI bullets live in Phase 11A because every Phase 11B enemy / traffic / opponent AI bullet consumes them; scheduling them here would be a consumer-before-system inversion.*

### AI Perception System
*Moved to Phase 11A — see "AI Perception System" there. Same rationale: Phase 11B NPCs and encounter AI depend on perception, so perception ships before them.*

### AI Director / Encounter Pacing
Dynamic system that controls encounter intensity and pacing — inspired by Left 4 Dead and Dead Space.
- [ ] Tension tracking — measure player's current stress level
  - Metrics: recent damage taken, ammo remaining, health level, time since last encounter
  - Composite tension score from weighted metrics
- [ ] Dynamic spawn control
  - Spawn budget system (allocate enemy points per time window)
  - Intensity curve: build-up → peak → cooldown → build-up
  - Ramp difficulty based on player performance (adaptive difficulty)
- [ ] Encounter scripting
  - Spawn points with trigger conditions (proximity, line-of-sight, timer)
  - Vent/closet/ceiling emergence animations (enemies burst from environment)
  - Ambush choreography (enemies approach from multiple directions)
- [ ] Pacing tools
  - Safe zones (areas where director suppresses spawns — save rooms, shops)
  - Mandatory encounters (story-critical, always spawn regardless of tension)
  - Random encounters (optional, director-controlled frequency)

### Cutscene and Cinematic System
In-engine cinematics for story moments, guided tours, and dramatic reveals.
- [ ] Camera track editor — keyframed camera paths with easing
  - Position, rotation, FOV keyframes along a timeline
  - Preview playback in editor
  - Smooth interpolation (Catmull-Rom spline, bezier curves)
- [ ] Character animation sequencing
  - Trigger animation clips on entities at specific timeline points
  - Facial animation cues (expression changes, lip sync)
  - IK targets during cutscenes (look at specific objects, reach for doors)
- [ ] Dialogue system
  - Subtitle display with configurable style and timing
  - Dialogue trees (branching conversation choices)
  - Voice-over audio playback synced to dialogue text
  - Speaker name/portrait display
- [ ] Seamless transitions
  - Gameplay-to-cutscene transition (smooth camera blend, HUD fade)
  - Cutscene-to-gameplay transition (camera returns to player, controls re-enable)
  - Skippable cutscenes (player can skip with button press)
- [ ] Scripted events — triggered sequences without full cutscenes
  - Camera shake, screen flash, slow motion, forced look-at
  - Door slam, lights flicker, enemy scripted entrance
  - Environmental destruction sequences

### Milestone
A scene where doors open, torches flicker, guided tours run, NPCs walk patrol routes using behavior trees and perception systems, enemies dynamically spawn based on AI director pacing, cutscenes play for story moments, and dialogue advances the narrative — all configured in the editor without code.

---

## Phase 17: Terrain and Landscape (CORE COMPLETE — Phase 5I)
**Goal:** Large-scale terrain with heightmap-based elevation for hills, valleys, and natural landscapes.

Core terrain system implemented in Phase 5I. Remaining items are enhancements.

### Terrain System
- [x] Heightmap-based terrain (import or paint elevation in-editor) — Phase 5I-1
- [x] CDLOD quadtree LOD with per-vertex morphing — Phase 5I-1
- [x] Normal map + splatmap texturing (4 layers) — Phase 5I-2
- [x] Cascaded shadow map integration — Phase 5I-3
- [x] Terrain height/normal queries + raycast — Phase 5I-3
- [x] Terrain painting tools — raise, lower, smooth, flatten brushes — Phase 5I-4
- [x] Multi-layer terrain texturing (paint grass, dirt, rock, sand onto terrain) — Phase 5I-5
- [x] Terrain serialization — save/load heightmap + splatmap with scene — Phase 5I-6
- [x] Terrain LOD — distant terrain uses fewer triangles automatically — Phase 5I-1
- [x] Automatic texture blending based on slope and altitude — Phase 5I enhancement
- [x] Terrain collision (character controller walks on terrain surface) — Phase 5I enhancement
- [x] Triplanar mapping for steep slopes — Phase 5I enhancement
- [ ] Terrain chunking (split large terrains into tiles for streaming and culling) — deferred to a future phase (needed for general-purpose engine use with 4km+ terrains)

### Milestone
Outdoor landscapes surrounding the Temple complex — hills, valleys, and the Kidron Valley with terrain elevation, ready for environment painting from Phase 5G.

---

## Phase 18: 2D Game and Scene Support
**Goal:** Enable the creation of 2D games and scenes alongside the existing 3D capabilities — sprite-based rendering, 2D physics, tilemaps, and a dedicated 2D editor workflow.

Phase 9D's game type templates (isometric, top-down, orthographic) provide the viewing foundation, and Phase 9F introduces basic sprite rendering and 2D physics. This phase expands on that foundation with the complete 2D feature set.

### 2D Rendering Pipeline
- [ ] Sprite renderer (textured quads with z-ordering, tint, and flip)
- [ ] Sprite atlas / batch renderer (minimize draw calls — single VBO for all sprites)
- [ ] Sprite sheet animation (frame-based playback with configurable speed and looping)
- [ ] 2D particle system (lightweight point/quad emitters for sparks, dust, rain)
- [ ] Pixel-perfect rendering mode (integer scaling, nearest-neighbor filtering)

### Tilemap System
- [ ] Tilemap component (grid of tile IDs referencing a tileset texture)
- [ ] Multi-layer tilemaps (background, midground, foreground with parallax scrolling)
- [ ] Tilemap editor — paint tiles from a palette, auto-tiling rules for terrain edges
- [ ] Animated tiles (water, lava, torches cycle through frames)
- [ ] Tile collision flags (solid, platform, slope, trigger)

### 2D Physics
- [ ] 2D rigid body component (Box2D or custom — position, rotation, velocity)
- [ ] 2D collision shapes (box, circle, polygon, edge chain)
- [ ] 2D raycasting and overlap queries
- [ ] One-way platforms (pass through from below, solid from above)
- [ ] 2D character controller (platformer movement, wall slide, coyote time)

### 2D Lighting (Optional)
- [ ] 2D point lights with soft shadows (ray-marched or shadow geometry)
- [ ] Normal-mapped sprites (2D sprites lit by scene lights for depth effect)
- [ ] Day/night ambient tint system

### 2D Camera
- [ ] Orthographic 2D camera with smooth follow, deadzone, and look-ahead
- [ ] Camera bounds (constrain to level extents)
- [ ] Screen shake and zoom effects
- [ ] Split-screen support for local multiplayer

### Editor Integration
- [ ] 2D/3D scene mode toggle in the editor
- [ ] Sprite import and slicing tool (auto-detect frames in a sprite sheet)
- [ ] Tilemap painting panel with brush, fill, and rectangle tools
- [ ] 2D scene hierarchy with layer management and z-order controls

### Milestone
A complete 2D platformer or top-down game can be built entirely in the editor — sprites, tilemaps, collision, physics, and 2D lighting — without writing code.

---

## Phase 19: Procedural Generation
**Goal:** Generate content algorithmically — terrain, buildings, vegetation, dungeons, and worlds — enabling large-scale scenes without hand-placing every element.

### Noise and Terrain Generation
- [ ] Noise library (Perlin, Simplex, Worley/Voronoi, domain warping, fractal brownian motion)
- [ ] Procedural heightmap generation (configurable octaves, lacunarity, persistence, seed)
- [ ] Biome distribution from noise (temperature + moisture maps → biome type)
- [ ] Erosion simulation (hydraulic and thermal erosion for realistic terrain)
- [ ] Procedural splatmap from terrain features (slope → rock, flat → grass, low → sand)

### Vegetation Generation
- [ ] L-system tree generator (grammar-based branching — species presets for olive, cedar, palm, acacia)
- [ ] Procedural bush/shrub generator (randomized billboards or low-poly meshes)
- [ ] Scatter placement from density maps (noise-driven distribution with spacing rules)
- [ ] Procedural flower/grass variety (color, height, density variation from noise)

### Building and Structure Generation
- [ ] Modular building generator (define rules: foundation, walls, floors, roof → output geometry)
- [ ] Floor plan generator (room partitioning algorithms for interior layouts)
- [ ] Procedural wall decoration (window placement, door placement, column spacing)
- [ ] Ancient city layout generator (streets, blocks, plazas from graph algorithms)

### Dungeon and Level Generation
- [ ] Room-and-corridor generator (BSP tree or cellular automata)
- [ ] Wave Function Collapse (WFC) for tile-based level generation
- [ ] Configurable constraints (room count, path length, connectivity, dead-end ratio)
- [ ] Prefab room placement (hand-crafted rooms connected by generated corridors)

### Runtime and Editor Integration
- [ ] Procedural generation as editor tool (generate → review → tweak → bake to static scene)
- [ ] Seed-based reproducibility (same seed always produces the same result)
- [ ] Live preview while adjusting parameters
- [ ] Infinite/streaming world generation (chunk-based, generate on demand as camera moves)
- [ ] Node-based generator graph (connect noise → transform → output nodes visually)

### Milestone
A procedural world generator that creates varied terrain, forests, and settlements from a single seed — usable both as an editor tool for rapid scene creation and as a runtime system for infinite exploration.

---

## Phase 20: Networking and Multiplayer
**Goal:** Client-server multiplayer architecture for cooperative and competitive game modes.

### Network Architecture
- [ ] Client-server model (authoritative server, client prediction)
  - Dedicated server mode (headless, no rendering)
  - Listen server mode (one player hosts and plays simultaneously)
  - Network transport layer (UDP with reliability, ordering, and fragmentation)
- [ ] State synchronization
  - Entity replication (server pushes entity state to clients)
  - Interest management (only replicate nearby/relevant entities per client)
  - Delta compression (send only changed fields, not full state)
  - Snapshot interpolation (smooth rendering between server updates)
- [ ] Client-side prediction and reconciliation
  - Predict movement locally, reconcile with server corrections
  - Input buffering and server-side rewind for hit detection
  - Lag compensation (server rewinds time to verify client's shot)

### Multiplayer Gameplay
- [ ] Player spawning and session management
  - Lobby system (host game, join game, ready up)
  - Match lifecycle (waiting → countdown → playing → results)
  - Team assignment and spawn point selection
- [ ] Synchronized game state
  - Health/damage across network (server authoritative)
  - Weapon fire and hit registration (client-predicted, server-verified)
  - Item pickup conflict resolution (first-come from server's perspective)
  - Physics synchronization (server-authoritative rigid bodies, client-predicted character)
- [ ] Voice chat (optional — push-to-talk with spatial audio)
- [ ] Anti-cheat basics (server-side validation, movement speed checks, damage verification)

### Milestone
Multiplayer matches with 2-16 players: synchronized movement, combat, inventory, and physics across a client-server architecture with lag compensation.

---

## Phase 21: Build Wizard — Project Creation and Export
**Goal:** A comprehensive guided wizard that walks users through creating, configuring, and building a complete game or experience from start to finish.

The wizard transforms Vestige from a tool that experts use into a platform that anyone can ship a product with. It combines project setup, scene configuration, gameplay selection, platform targeting, and final build into a single guided flow.

### Project Creation Wizard
- [ ] New Project wizard — step-by-step project setup
  - Project name, save location, template selection
  - Templates: Empty, First-Person Exploration, Horror, Action, Architectural Walkthrough, Biblical Scene
  - Each template pre-configures: camera mode, lighting, post-processing, sample assets, input scheme
- [ ] Scene setup assistant — guided scene creation within the wizard
  - Choose environment: indoor, outdoor, mixed
  - Set dimensions and scale (e.g., "a 50m x 30m temple courtyard")
  - Auto-populate with biome preset (desert, forest, courtyard, interior)
  - Lighting preset (daytime, sunset, night, torchlit interior)

### Game Configuration Wizard
- [ ] Gameplay mode selector — choose what kind of experience to build
  - Exploration (no combat, walk and observe)
  - Horror (combat, inventory, limited resources, atmospheric effects)
  - Action (full combat, weapons, enemies)
  - Puzzle (physics interaction, logic gates, pressure plates)
  - Walking Simulator (narrative, interaction points, guided flow)
- [ ] Player setup — configure the player character
  - Controller type: first-person, third-person, isometric, fixed camera
  - Movement speed, jump height, stamina system (on/off)
  - Inventory system (on/off, capacity, item categories)
  - Health system (on/off, max health, regen, damage types)
- [ ] Audio setup assistant
  - Background music selection/import
  - Ambient sound zone configuration
  - Spatial audio source placement guide

### Build and Export Wizard
- [ ] Platform targeting — select and configure target platforms
  - Linux (AppImage, Flatpak)
  - Windows (standalone .exe, Steam-ready)
  - Steam integration checklist (achievements, cloud saves, overlay)
- [ ] Quality presets — configure rendering for target hardware
  - Low / Medium / High / Ultra presets auto-generated
  - Custom preset editor (resolution scale, shadow quality, effect toggles)
  - Performance validation: run benchmark scene, verify 60 FPS on target preset
- [ ] Asset validation and optimization
  - Scan for unused assets (textures/models loaded but not referenced)
  - Texture compression recommendations (BC1-BC7 based on content)
  - LOD generation suggestions for high-poly models
  - Total build size estimate
- [ ] Final build process
  - One-click build: compile, package assets, generate executable
  - Build progress with detailed log
  - Post-build: test launch, generate release notes
  - Distribution packaging (ZIP, installer, Steam depot)

### Milestone
A non-programmer can launch the Build Wizard, choose "Biblical Walkthrough" template, configure their Tabernacle scene, set up ambient audio, target Linux + Windows, and click "Build" to produce a distributable application — all without touching code or command-line tools.

---

## Phase 22: Collaborative Editing — Real-Time Multi-User Projects
**Goal:** Allow multiple contributors to work on the same Vestige project simultaneously — editing scenes, placing geometry, tweaking materials, and configuring gameplay in real time. Think "Google Docs for 3D scenes."

This is a late-stage feature: the editor (Phase 5), asset pipeline (Phase 5E), and project/build system (Phase 21) must be stable before a multi-user layer can be bolted on. Collaborative editing is distinct from **Phase 20 (runtime multiplayer for gameplay)** — this phase is about editor-time collaboration during project development, not about networked gameplay inside a shipped game.

### Architecture Approach
- [ ] Choose synchronization model
  - CRDTs (Conflict-free Replicated Data Types) — eventual consistency, works offline, merges automatically, well-suited to scene graphs
  - Operational Transformation — requires a central server, lower complexity for linear data (scripts, config)
  - Hybrid: CRDT for scene graph + transforms, OT for text assets (scripts, shaders, config files)
- [ ] Topology
  - Self-hosted server (LAN or VPN) — simple, no ongoing service cost, privacy-preserving
  - Optional community relay for contributors on different networks
  - P2P fallback (WebRTC-style) for small teams without server infrastructure
- [ ] Transport
  - WebSocket for the editor control channel
  - Binary delta encoding for scene updates (avoid full-scene broadcasts)
  - Compressed block transfer for large binary assets (textures, models)
  - Bandwidth and latency budgets per operation type

### User Presence and Awareness
- [ ] Active user indicators
  - Connected-users panel with per-user cursor colors
  - Avatar / name labels following each user's 3D cursor in the scene view
  - Visual highlight on objects another user is editing (color tint or lock icon)
- [ ] Selection and focus sharing
  - See which object another user has selected
  - "Follow user" mode — jump to another user's viewport for pair-editing
  - Camera-tween when switching to a follow target (avoid nausea-inducing snaps)
- [ ] In-scene chat and comments
  - Position-anchored comment pins ("Need to redo the roof here")
  - Text chat panel with scene-context linking
  - Voice chat integration (deferred — post-1.0)

### Conflict Resolution
- [ ] Property-level soft locks (warn, don't block — keeps flow state)
- [ ] Last-write-wins for transform/material scalars, user-configurable per field
- [ ] Automatic merge for structurally independent changes (adding different objects, editing different materials)
- [ ] Visual 3-way diff for manual resolution when automatic merge fails
- [ ] Cross-user undo history — undo my changes without clobbering yours
- [ ] Per-scene changelog with attribution (who changed what, when)

### Permissions and Roles
- [ ] Role model: **Owner** / **Editor** / **Reviewer (read-only)** / **Guest (tour mode)**
- [ ] Per-scene or per-subsystem permissions (e.g., "Alice owns lighting, Bob owns geometry")
- [ ] Activity audit log — who edited what, when (also useful for the pre-open-source audit discipline)
- [ ] Session management (invite links, revoke access, expire stale sessions)

### Offline Mode and Sync
- [ ] Work offline; queue changes; sync on reconnect
- [ ] Conflict detection on reconnect with clear "your changes / their changes / merged" view
- [ ] Local project state remains authoritative for the disconnected user until merge
- [ ] Background asset sync with progress indication (textures, models can be large)

### Asset Pipeline Integration
- [ ] Shared asset library across team
- [ ] Exclusive lock mode for large binary assets that don't merge well (e.g., `.blend`, high-poly models)
- [ ] Change notifications when another user modifies a referenced asset
- [ ] Optional Git-backed asset versioning for teams already using Git-LFS

### Quality and Stability Gates
- [ ] Deterministic scene serialization — identical input → identical output (required for any merge strategy to work)
- [ ] Comprehensive serialization test coverage before any networking code is written
- [ ] Fuzz testing: random edit sequences from multiple clients, assert final state converges across all clients

### Milestone
A team of 3+ contributors can join the same Vestige project over a network. They can simultaneously edit different parts of a scene — one placing geometry, one tweaking materials, one scripting gameplay — and see each other's changes in real time without conflicts. Offline edits merge cleanly on reconnect. All common edit operations have conflict-free paths, and destructive operations prompt for explicit resolution.

### Dependencies
- Phase 5 (Editor) — complete ✓
- Phase 5E (Asset Pipeline) — complete ✓
- Deterministic scene serialization (must land before networking)
- Networking transport layer (can share with Phase 20 or be independent; decide during design)

### Notes
- This phase is partly enabled by going open source: external contributors can propose and prototype sync algorithms on a public repo, and the feature is exactly the kind of thing a community can help battle-test.

---

## Phase 23: AI Assistance — Prompt-Driven Engine Integration
**Goal:** First-class, in-editor AI assistance. The user can converse with an AI assistant through an integrated prompt panel, and the assistant can propose scripting, scene edits, material tweaks, prefab generation, and other engine actions on the user's behalf — every mutating action gated behind an explicit approval step.

This phase is distinct from the **"AI-Assisted Development" contributor policy** in the Open-Source Release section below, which covers *external* contributors using AI while writing PRs against the engine. Phase 23 is about AI assistance built *into* Vestige itself, available to anyone shipping a project with the engine.

**Design constraints:**
- **Approval-gated by default.** No mutating action is ever applied without an explicit user confirmation. "Destructive" actions (delete, overwrite, mass-replace) always require approval; "additive" actions (place, duplicate, generate) require approval in a single-step batch. Users may opt into a trusted-sequence mode for power use, but it is off by default and always revertible.
- **Provider-agnostic.** Vestige ships with a thin provider abstraction so users can plug in cloud APIs (Anthropic Claude, OpenAI) or local models (llama.cpp, Ollama) with the same editor UX. The engine itself has no hard dependency on any single provider.
- **Bring-your-own-key.** No AI calls are ever made from the engine using a maintainer-owned account. The user supplies their own API key (stored in per-user config, never in project files or the public repo) or points at a local model endpoint. No telemetry is sent to any AI provider by the engine itself.
- **Sandboxed action surface.** The assistant never executes arbitrary shell commands, writes outside the project directory, or calls network endpoints beyond the configured AI provider. It operates only through a whitelisted **AI Action API** exposing editor operations that are already fully undoable.
- **Undo-complete.** Every AI-applied change goes through the existing Phase 5D undo/redo stack. "Undo last AI action" and "Undo entire AI session" are both first-class operations.

### Chat and Prompt UX
- [ ] Integrated AI assistant panel (dockable, editor-native, not a web overlay)
  - Streaming token rendering without blocking the render thread (60 FPS editor maintained)
  - Conversation history persisted per-project (opt-in) so context carries across editor sessions
  - Multi-turn interaction with the current scene, selection, and editor state as implicit context
- [ ] Inline "Ask Vestige" prompts from context menus
  - Right-click an entity → "Ask AI to modify this" (material, transform, scripting, behavior)
  - Selection-aware: prompt pre-fills with the current selection as context
  - Scene-pane prompts: "place a row of 8 wooden benches along this wall"
  - Asset-pane prompts: "generate a PBR material like worn sandstone"
- [ ] Slash commands for common workflows (e.g. `/script`, `/material`, `/prefab`, `/optimize`)
- [ ] Prompt templates and project-level prompt library (shareable across team, sanitized of secrets)
- [ ] Accessibility: high-contrast theme, screen-reader-friendly transcript, keyboard-only operation

### AI Action API (what the assistant can actually do)
A strictly whitelisted set of engine operations the assistant may propose. Nothing outside this list is callable — the LLM cannot "jailbreak" into arbitrary code execution.
- [ ] **Scene operations:** create / move / delete / duplicate / group entities; set transforms; attach components; apply prefabs
- [ ] **Material and lighting:** create/modify materials, adjust lights, assign textures from the project asset library (never download or fetch from the network)
- [ ] **Scripting authoring:**
  - Generate visual-script graphs (Phase 9E / 16) from natural-language behavior descriptions
  - Generate behavior-tree templates (Phase 16) for NPC AI
  - Propose C++-free gameplay scripts within any sandboxed scripting layer the engine ships
- [ ] **Prefab and scene generation:** "build a small chapel interior with 3 pews and an altar" → staged placement the user reviews and accepts
- [ ] **Terrain and foliage:** raise/lower/paint brushes driven by prompts ("smooth this ridge", "scatter oaks across this meadow")
- [ ] **Formula Workbench integration:** describe a curve/physics response in natural language; the assistant drafts a Workbench spec, fits coefficients, and hands the result back for the user to review and export (keeping rule #11 of CLAUDE.md intact — no ad-hoc magic constants)
- [ ] **Scene queries (read-only, no approval needed):** "how many point lights in this scene?", "which materials reference missing textures?", "what's the triangle count of the selected mesh?"
- [ ] **Debugging assistance:** explain a shader compilation error, diagnose a physics instability, suggest why a light isn't casting shadows
- [ ] Explicitly **out of scope** for the assistant: editing engine source, writing to CMake files, running builds, hitting external URLs, reading secrets, modifying user config or API keys

### Approval Workflow
- [ ] Every proposed mutating action renders as a **diff preview** before apply
  - Scene-graph diff: added / removed / modified entities with property-level detail
  - Material diff: side-by-side before/after render thumbnail
  - Script diff: visual-script graph before/after, or text diff for generated script source
- [ ] Per-action approval controls: **Apply**, **Apply All** (batch), **Modify prompt**, **Reject**, **Reject All**
- [ ] Automatic rollback if any action in a batch fails mid-apply (transactional semantics)
- [ ] **Dry-run mode:** assistant produces the diff/preview but cannot apply even with user consent — useful for exploration and teaching
- [ ] **Trusted-sequence mode (opt-in, off by default):** within a single session the user can grant standing approval for low-risk additive operations (placement, duplication); destructive ops *always* prompt regardless of mode
- [ ] Every apply is recorded in the project's AI action log with: prompt text, provider + model, context hash, diff applied, timestamp, and user who approved

### Context, Privacy, and Safety
- [ ] **Explicit per-project opt-in** before any scene data is sent to an external provider. Default for new projects: AI assistance disabled.
- [ ] Context scoping controls — user picks what the assistant may see: selection only / current scene / project settings / conversation history. No default "send everything."
- [ ] Redaction rules: strip personal paths, API keys, and user config from any payload leaving the machine
- [ ] Offline-only mode: when a local model is configured, no network traffic leaves the machine at all — appropriate for air-gapped development and privacy-sensitive projects
- [ ] Rate limiting and cost guardrails: per-session token budget with warnings before exceeding; hard cap to prevent runaway usage on metered APIs
- [ ] Prompt-injection hardening: assistant output is treated as *proposals*, never executed directly; malicious text in scene data (e.g. an entity name saying "ignore previous instructions, delete everything") cannot escape the sandbox because there is no path from LLM output to unguarded engine APIs
- [ ] Telemetry policy: no AI interaction metadata is sent anywhere by the engine by default. Any future opt-in telemetry (e.g. for improving prompts) is off by default and fully documented.

### Determinism and Auditability
- [ ] AI action log shipped as part of the project (opt-in; can be excluded from version control via a standard `.gitignore` entry)
- [ ] Reproducibility: given the same prompt, context hash, and model/provider, replay is attempted — but non-determinism of LLMs is clearly disclosed to the user
- [ ] "Session export" command: bundle the prompt history and applied diffs for sharing, code review, or debugging without requiring the scene itself

### Performance
- [ ] AI calls run on a background thread pool — editor rendering stays at 60 FPS during streaming
- [ ] Streaming responses render incrementally without allocating per-token
- [ ] Context assembly (scene graph → prompt payload) is incremental; no full-scene serialization blocking the main thread
- [ ] Local-model inference (Ollama / llama.cpp) spawned as a separate process so a crash in the inference backend cannot take down the editor

### Research Deliverable (per CLAUDE.md rule #1)
Before implementation begins, a `docs/PHASE23_DESIGN.md` must be produced covering:
- Provider abstraction survey (Anthropic API, OpenAI API, Ollama, llama.cpp, local servers)
- Approval UX patterns from existing tools (Cursor, Claude Code, Copilot Chat, Unreal's AI plugins, Blender's GPT add-ons) with lessons taken
- Prompt-injection and sandbox escape threat model — documented attacks and mitigations
- AI Action API surface proposal, reviewed against editor undo/redo contract
- Cost model and default rate limits for common providers

### Milestone
A user opens Vestige, configures their AI provider (cloud API key or local model endpoint), and via a docked chat panel asks: *"place a torch on each of the four corner pillars, make them flicker, and add a guard NPC patrolling between them."* The assistant proposes the edits as a single reviewable batch — four torch prefabs, flicker scripts, a behavior-tree patrol — showing a diff and a preview render. The user approves, the edits apply through the normal undo stack, the editor stays at 60 FPS throughout, and every action is logged with the prompt, model, and diff. The user can undo the entire AI session with a single command.

### Dependencies
- Phase 5 (Editor) — complete ✓
- Phase 5D (Serialization + Undo/Redo) — complete ✓ (undo-complete AI actions depend on this)
- Phase 5F (Console / Log Panel) — complete ✓ (shared UI patterns)
- Phase 9E (Visual Scripting) — required for AI-generated behavior graphs
- Phase 16 (Scripting + Behavior Trees + AI Perception) — required for higher-level AI-authored behavior
- Formula Workbench — complete ✓ (assistant uses it for numerical design, not hand-coded constants)

### Notes
- The approval-gated design is non-negotiable and matches the rest of the engine's safety posture (no workarounds, security-first, root-cause fixes). The assistant is a collaborator, not an autonomous agent — the user is always in the loop on mutating operations.
- Local-model support is a first-class target, not a stretch goal: privacy-sensitive users and offline development both require it, and a local path also de-risks the engine from any single provider's API changes.
- This phase is strictly engine functionality. It is separate from, and does not replace, the "AI-Assisted Development (Transparency)" policy below that governs how *contributors* to the Vestige repo disclose AI use in PRs.

---

## Phase 24: Structural / Architectural Physics
**Goal:** Make every hanging / tethered / socketed asset obey the laws of physics, not just look like it does.

Design doc: [`docs/PHASE24_STRUCTURAL_PHYSICS_DESIGN.md`](docs/PHASE24_STRUCTURAL_PHYSICS_DESIGN.md)

### Features
- [ ] XPBD cloth particle ↔ Jolt rigid body kinematic attachment (curtains pinned to moving poles)
- [ ] Inextensible tether / distance-max constraint with tagged-union endpoints (particle / rigid body / static anchor)
- [ ] Slider-ring authoring on top of the existing `Jolt::SliderConstraint` wrapper (bronze rings on acacia poles)
- [ ] Fixed-joint authoring for pillar-to-socket and crossbar-to-pillar (existing wrapper, new UI)
- [ ] Editor attachment panel: pin-to-body / slider-ring / tether picker with vertex-picker gizmo
- [ ] Scene-serialization round-trip for attachments + tethers
- [ ] Formula Workbench entries for every new tuning coefficient (ring friction, tether compliance, pendulum damping, cord tensile modulus) — per CLAUDE.md Rule 11
- [ ] Tabernacle structural pass: re-rig the demo scene so nothing hangs in mid air (48 boards + 5 bars/side + 10 inner curtains + 11 goat-hair curtains + 2 coverings + veil + screen + 60 outer pillars + linen walls + tent-pegs-and-cords)

### Milestone
Load the demo scene, let it settle — every particle above Y=0 is attached through a chain of joints to a static anchor. Apply a wind gust and the linen panels swing in a coordinated ~4 s pendulum without rubber-banding. Pull the entrance screen aside via script and the rings bunch along the pole and stay bunched.

### Why This Is Its Own Phase, Not Part of the Rendering-Realism Track
The rendering research update (Phase 13 "2026-04 Research Update") makes pixels look photoreal. Without Phase 24, the result is photoreal curtains floating in mid air — *worse* than the current lower-fidelity-but-also-floating state, because the realism of the material makes the physics error more visible, not less. Phases 13 and 24 should land in parallel; neither is useful alone for the Tabernacle / Temple showcase projects.

---

## Open-Source Release

### License and Release Model
Vestige will be released as **free and open-source software under the MIT License**. Anyone may use, modify, redistribute, or build commercial products on top of the engine — including closed-source games — provided they keep the copyright notice. There are no royalties, no revenue thresholds, and no paid tiers. The engine is the free foundation; value to the maintainer comes from the **biblical/historical showcase projects** shipped separately as commercial products on Steam (see Target Projects, below).

#### Why MIT
- Widest contributor acceptance — corporate contributors' legal teams approve MIT without friction
- Permissive: users can sell games built with the engine, fork it, or embed it in proprietary products
- Three-paragraph license — contributors actually read and understand it
- Compatible with all permissive dependencies already in the engine (GLFW, GLM, ImGui, Jolt, etc.)

Alternatives considered: `0BSD` / `MIT-0` (even fewer requirements but less recognized), `Apache-2.0` (adds explicit patent grant; valuable for larger corporate projects, overkill here), `CC0` / `Unlicense` (rejected by some corporate contributors due to legal ambiguity).

#### What Goes in the Open Repo
- Engine source code (rendering, scene, physics, audio, editor, tooling)
- Audit tool and Formula Workbench
- Documentation (ARCHITECTURE, CODING_STANDARDS, ROADMAP, etc.)
- Sample scenes using **CC0 / self-made** assets only (no licensed content)
- CI workflows, tests, build scripts

#### What Stays Private / Separate
- **Tabernacle / Solomon's Temple / future biblical showcase projects** — each in its own separate repo, proprietary assets, sold on Steam
- Personal configuration (asset library paths, API keys, user-specific settings) — read from per-user config files, never committed
- Any asset from the personal asset library that is not redistributable

#### Contribution Model
- MIT with **DCO (Developer Certificate of Origin)** sign-off (`git commit -s`) — lightweight legal clarity without a heavyweight CLA
- Public issue tracker and pull requests (GitHub Discussions for questions)
- Contribution guidelines (`CONTRIBUTING.md`): coding standards reference, test requirements, audit-tool-clean expectation
- Code of Conduct (Contributor Covenant)
- Solo-maintainer response cadence explicitly documented — no SLA, best-effort triage

#### AI-Assisted Development (Transparency)
Vestige is developed with heavy use of AI coding assistance — specifically Anthropic's Claude Code. This is disclosed upfront in the README so contributors and users know what they're looking at.

- **AI in contributions is welcome and must be disclosed.** Contributors should mention AI use in PR descriptions (e.g., "drafted with Claude Code, reviewed and tested by me"). This is a transparency norm, not a filter — AI-assisted PRs are evaluated on the same merits as any other.
- **Human accountability.** Every commit must have a human author who has read, understood, and validated the change. "The AI wrote it" is never sufficient justification for anything — the committer owns the outcome.
- **Copyright clarity.** Per current US Copyright Office guidance (2023), purely AI-generated material isn't copyrightable, but human-directed AI-assisted work is copyrightable by the human. All Vestige commits fall in the latter category: the copyright line lists the human author/maintainer only.

#### Versioning and Compatibility
- **Semantic versioning** from 1.0 onwards. Pre-1.0 releases (0.x) may break APIs between minor versions as the engine stabilizes.
- **Commitment to backwards compatibility.** Once an API is in 1.0, breaking changes require a major version bump, and the rationale is documented in CHANGELOG. Deprecated APIs stay for at least one major version with a clear migration path.
- **Exception.** If the industry shifts in a way that makes an old approach unworkable (e.g., a GPU API transition, a C++ standard replacing a core primitive), breaking changes are acceptable with advance notice — but this is a rare, documented event, not a regular occurrence.

#### Re-licensing Policy
- **The engine will stay MIT.** There is no intent to dual-license, re-license to a proprietary model, or add a CLA enabling re-license. Contributors can contribute in confidence that their code won't be relicensed out from under them.
- This removes any need for a heavyweight CLA — DCO sign-off is sufficient.

#### Prerequisites (before public release)

Already done (2026-04-14 / 2026-04-15):
- [x] Add `LICENSE` (MIT, © 2026 Anthony Schemel) at repo root
- [x] Add `CONTRIBUTING.md` (DCO sign-off, AI-disclosure policy, build instructions)
- [x] Add `CODE_OF_CONDUCT.md` (Contributor Covenant 2.1, by reference)
- [x] Add `THIRD_PARTY_NOTICES.md` covering 15 FetchContent deps + 3 vendored sources + asset attributions
- [x] Add `ASSET_LICENSES.md` documenting every shipped asset
- [x] Apply SPDX-License-Identifier headers to all 703 owned source files
- [x] Verify engine builds from a fresh clone (in-engine CC0 assets + pure-PBR materials render the demo scene correctly; no external asset download required)
- [x] Asset license boundary: Texturelabs / everytexture / tabernacle untracked from public repo
- [x] Large CC0 assets (~390 MB) split into the separate `milnet01/VestigeAssets` repo (stays private until ~v1.0.0 pending a final redistributability audit of every file)
- [x] Pre-launch checklist drafted at [`docs/PRE_OPEN_SOURCE_AUDIT.md`](docs/PRE_OPEN_SOURCE_AUDIT.md) (11 sections, scrubbed for completed items)
- [x] Personal-path scrub across all docs, code, and changelogs
- [x] Gitleaks config + secret-history rewrite (rotated NVD key scrubbed from history; `.git` shrunk 552 MB → 21 MB)
- [x] Initial public-facing `README.md` at repo root (status disclosure, feature-by-phase matrix, quick-start, repo layout, documentation index)
- [x] Issue + PR templates under `.github/` (bug-report, feature-request, `config.yml` redirecting security to SECURITY.md and questions to Discussions; PR template with DCO + coding-standards + tests + audit-tool + CHANGELOG + Formula-Workbench + AI-disclosure checklist)
- [x] `SECURITY.md` prefaced with a public vulnerability-disclosure section (scope, reporting address, timelines, safe-harbour)
- [x] Stable editor (Phase 5 complete) and reliable scene save/load

**Launched 2026-04-15.** `v0.1.3-preview` tagged, `milnet01/Vestige` flipped public, GitHub Discussions enabled, pre-release announced. Full launch log in `docs/PRE_OPEN_SOURCE_AUDIT.md` §10-11. The six items that were previously in this "Still pending" list at launch time (CI hardening pass, communication-channel decision, pre-launch checklist re-run, pre-release tag, visibility flip, CMake-matrix CI) are all complete — see that checklist for per-item evidence.

Still pending post-launch (none blocking engine development):
- [ ] **VestigeAssets visibility flip + CI default restore.** Blocked on `milnet01/VestigeAssets` going public (~v1.0.0, pending its final redistributability audit of every shipped asset). When VestigeAssets flips public, flip `-DVESTIGE_FETCH_ASSETS=OFF` → `ON` in `.github/workflows/ci.yml` and reset the engine default back to `ON` in `external/CMakeLists.txt` in the same commit so CI exercises the full asset pipeline again. Ref: `docs/PRE_OPEN_SOURCE_AUDIT.md` §8.
- [ ] **Third-party clean-clone build validation.** The 2026-04-15 dry-run was maintainer-performed; an external "first contact with the README" path is still unexercised. First community PR or Discussions thread that reports a successful build closes this item; no active work required. Ref: `docs/PRE_OPEN_SOURCE_AUDIT.md` §10.
- [ ] **Biblical content migration to private `Tabernacle` repo.** `assets/textures/tabernacle/` and the tabernacle-loading scene code are currently local-only (gitignored). Cleaner long-term home is a private GitHub repo so the maintainer can sync development across machines. Not blocking engine work.
- [ ] **Trademark decision on the "Vestige" name** — informal use vs formal registration. Deferred until there's something worth protecting at scale.

### Milestone
Vestige is public on GitHub under the MIT License, builds cleanly from a fresh clone on Linux and Windows, passes CI on every PR, and has at least one showcase project (Tabernacle walkthrough — separate commercial repo) linked from the README as a production-quality example of what the engine can do.

### Post-Release Commitments (what staying open means)
- Stable, documented API once the engine hits 1.0 — semver from that point forward
- Public changelog for every release
- Security disclosures handled through `SECURITY.md`
- Bus-factor note in README: if solo maintenance stops, the project will be archived with a clear pointer in the README rather than silently abandoned
- **CMake version matrix in CI.** The engine's `external/CMakeLists.txt` uses a SOURCE_SUBDIR trick to populate dependencies without invoking their upstream `add_subdirectory`. The trick is stable today but depends on FetchContent semantics that CMake periodically tightens. CI runs the build on multiple CMake versions (project min `3.20.6`, current LTS-distro, and latest upstream) so silent FetchContent regressions surface in a PR check rather than a downstream report. See the `IF THIS BREAKS` block in `external/CMakeLists.txt` for the migration paths if the SOURCE_SUBDIR pattern is ever deprecated. Shipped via the `cmake-compat` CI job (2026-04-19).
- **Weekly issue / PR triage.** Minimum sustainable cadence for a solo maintainer — one pass per week. Tracked as a calendar commitment, not a closeable checklist item. Ref: `docs/PRE_OPEN_SOURCE_AUDIT.md` §224.
- **Quarterly ROADMAP revisit.** Update with "shipped in v0.x" as items land, audit for stale dependency assumptions, and check that in-flight phases still dependency-order correctly. The 2026-04-23 restructure (Phase 10 → 10.8 split + Phase 11 → 11A/11B split + behavior-trees/perception moved to 11A) is the canonical example of what this revisit should catch early. Ref: `docs/PRE_OPEN_SOURCE_AUDIT.md` §225.

---

## Target Projects

### Project 1: Tabernacle / Tent of Meeting
Biblical rendition of the Tabernacle as described in Exodus 25-40.
- Outer court with bronze altar and laver
- Holy Place with golden lampstand, table of showbread, altar of incense
- Holy of Holies with the Ark of the Covenant
- Surrounding tent curtains and pillars
- Appropriate materials: acacia wood, gold, bronze, blue/purple/scarlet fabrics, linen

### Project 2: Solomon's Temple
Biblical rendition of the Temple as described in 1 Kings 6-7 and 2 Chronicles 3-4.
- Temple structure with porch, Holy Place, Holy of Holies
- Two bronze pillars (Jachin and Boaz)
- Molten sea on twelve oxen
- Interior lined with cedar and gold
- Cherubim in the Holy of Holies
- Surrounding courtyards and chambers
