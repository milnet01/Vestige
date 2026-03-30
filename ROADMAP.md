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
- [ ] OBJ model loading (simple format, good for starting)
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
- [ ] Camera component (currently camera is standalone — works for now)
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

## Phase 4: Visual Quality (IN PROGRESS)
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

## Phase 5: Scene Editor — The Creator's Toolkit
**Goal:** Build a full graphical editor so scenes, materials, effects, and gameplay can be created entirely without writing code. This is the primary tool the user will use day-to-day.

The editor is not just a debug tool — it IS the way scenes get built. Every engine feature must ultimately be accessible through the editor. This phase is broken into sub-phases that can be developed incrementally.

---

### Phase 5A: Editor Foundation
**Goal:** Get the basic editor shell running — panels, viewport, and the ability to interact with the scene visually.

#### GUI Framework
- [ ] Dear ImGui integration (immediate-mode GUI, renders on top of the 3D viewport)
- [ ] ImGui GLFW + OpenGL backend setup
- [ ] Docking system (drag panels to arrange your workspace — imgui docking branch)
- [ ] Editor theme (dark theme, readable fonts, consistent styling)
- [ ] Editor/Play mode toggle (Escape or a toolbar button switches between editing and first-person walkthrough)

#### Metric Scale System
- [ ] Define 1 engine unit = 1 meter (enforce everywhere)
- [ ] 3D grid overlay on the ground plane (1m lines, 10m bold lines)
- [ ] Grid snapping — objects snap to 0.25m / 0.5m / 1m increments (configurable)
- [ ] Ruler/measurement tool — click two points to see distance in meters
- [ ] Dimension display — selected objects show their width/height/depth in meters
- [ ] Room dimension input — type "10m x 20m x 2.5m" to create a room of that size

#### Editor Camera
- [ ] Orbit camera (Alt+drag to orbit around a point, scroll to zoom)
- [ ] Pan camera (middle-mouse drag to pan)
- [ ] Focus on selection (F key snaps camera to look at the selected entity)
- [ ] Top/Front/Side orthographic views (numpad shortcuts)
- [ ] Smooth camera transitions between views

#### Selection System
- [ ] Mouse picking — click on an object in the viewport to select it
- [ ] Selection highlighting (outline or tint on selected object)
- [ ] Multi-selection (Shift+click to add, Ctrl+click to toggle)
- [ ] Selection rectangle (drag to box-select multiple objects)

#### Transform Gizmos
- [ ] Translate gizmo (RGB arrows for X/Y/Z, click-drag to move)
- [ ] Rotate gizmo (rings around the object, drag to rotate)
- [ ] Scale gizmo (cubes on axes, drag to scale)
- [ ] W/E/R hotkeys to switch between translate/rotate/scale
- [ ] Local vs World space toggle for gizmos
- [ ] Snap-to-grid for gizmos (hold Ctrl to snap while dragging)

---

### Phase 5B: Scene Construction
**Goal:** Place and arrange objects to build rooms, walls, floors, and architectural structures.

#### Primitive Placement
- [ ] Toolbar palette for placing basic shapes: cube, plane, cylinder, sphere, wedge/ramp
- [ ] Click-to-place — click in the viewport or grid to drop a shape at that position
- [ ] Shapes spawn at metric sizes (e.g., cube = 1m x 1m x 1m by default)
- [ ] Dimension handles — drag edges/faces of a placed shape to resize it

#### Room/Wall Builder
- [ ] Wall tool — click two points on the grid to draw a wall segment (specify height + thickness)
- [ ] Room tool — click four corners (or specify dimensions) to generate a room with walls, floor, and ceiling
- [ ] Wall thickness setting (default 0.2m for interior, 0.3m for exterior)
- [ ] Door cutout tool — click on a wall to cut a door-sized opening
- [ ] Window cutout tool — click on a wall to cut a window opening
- [ ] Roof tool — select walls and choose roof type (flat, gabled, hipped)
- [ ] Stair/ramp tool — specify start height, end height, and width

#### Model Import
- [ ] glTF / OBJ import dialog — browse and import 3D models into the asset library
- [ ] Import preview — see the model before committing to import
- [ ] Automatic material/texture extraction from imported models
- [ ] Place imported models into the scene from the asset browser
- [ ] Scale-on-import — ensure imported models match metric scale (1 unit = 1 meter)

#### Object Management
- [ ] Duplicate objects (Ctrl+D)
- [ ] Delete objects (Delete key)
- [ ] Group objects (select multiple, group into a named parent entity)
- [ ] Lock objects (prevent accidental selection/modification)
- [ ] Hide/show objects (eye icon in hierarchy — hidden objects don't render)
- [ ] Copy/Paste transforms between objects
- [ ] Align tools (align selected objects by edge, center, or distribute evenly)

#### Prefab System
- [ ] Save any entity (or group) as a reusable prefab asset (e.g., "Golden Lampstand")
- [ ] Place prefab instances in any scene — drag from the asset browser
- [ ] Edit the master prefab and have all instances update automatically
- [ ] Override individual instance properties (e.g., different scale on one copy)
- [ ] Prefab library browser in the asset panel

#### Scene Hierarchy Panel
- [ ] Tree view of all entities in the scene
- [ ] Drag to reparent entities (make one a child of another)
- [ ] Right-click context menu (rename, duplicate, delete, add child)
- [ ] Search/filter entities by name
- [ ] Icons to distinguish entity types (mesh, light, camera, empty)

---

### Phase 5C: Materials, Textures, and Lighting
**Goal:** Assign and tweak the look of every surface, and place lights, all through the editor.

#### Material Editor Panel
- [ ] Visual material editor — sliders for diffuse color, specular color, shininess
- [ ] Texture slot assignment — drag a texture from the asset browser onto a material slot
- [ ] Live preview — changes update in the viewport immediately
- [ ] Material library — save/load reusable materials (e.g., "Gold", "Acacia Wood", "Linen")
- [ ] UV tiling controls — adjust how many times a texture repeats per meter
- [ ] POM height scale slider (when height maps are assigned)
- [ ] PBR material slots (when PBR is implemented): albedo, normal, roughness, metallic, AO, emissive

#### Texture Management
- [ ] Asset browser panel — shows all textures in `assets/textures/` with thumbnail previews
- [ ] Drag-and-drop textures onto objects in the viewport to assign them
- [ ] Texture import — drag external image files into the asset browser to import
- [ ] Texture preview — click a texture to see it full-size with metadata (resolution, format, size)
- [ ] Texture filtering options (nearest, linear, anisotropic) per texture

#### Entity Inspector Panel
- [ ] Shows all components on the selected entity
- [ ] Edit any property — transform, material, light settings, etc.
- [ ] "Add Component" button — attach new components to an entity
- [ ] "Remove Component" button
- [ ] Component-specific sub-panels (material expands to show all material properties, etc.)

#### Light Placement and Editing
- [ ] Place lights from the toolbar (directional, point, spot)
- [ ] Visual light indicators in the viewport (icons, range spheres, cone wireframes)
- [ ] Light property editing in the inspector (color picker, intensity, range, shadow toggle)
- [ ] Directional light visualized as an arrow showing direction
- [ ] Point light visualized as a sphere showing range
- [ ] Spot light visualized as a cone showing angle and range

---

### Phase 5D: Scene Persistence
**Goal:** Save and load scenes so work persists between sessions. This is essential — without it, every scene is lost when the editor closes.

#### Scene Serialization
- [ ] Save scene to JSON file (human-readable, version-controlled)
- [ ] Load scene from JSON file
- [ ] Auto-save (periodic backup while editing)
- [ ] Scene metadata (name, author, description, creation date)
- [ ] All entity data serialized: transforms, components, material references, light settings
- [ ] Asset references by path (textures, meshes — not embedded in scene file)

#### Undo/Redo System
- [ ] Command pattern — every editor action is a reversible command
- [ ] Ctrl+Z to undo, Ctrl+Shift+Z (or Ctrl+Y) to redo
- [ ] Undo history panel showing recent actions
- [ ] Essential for a non-destructive workflow — mistakes can be reverted

#### Project Management
- [ ] Project file that tracks all scenes, assets, and settings
- [ ] "New Scene" / "Open Scene" / "Save Scene" / "Save As" from the File menu
- [ ] Recent files list
- [ ] Asset directory monitoring — detect when new textures/models are added to `assets/`

---

### Phase 5E: Effects Editors
**Goal:** Configure particle effects (fire, smoke, water) through the editor, not code.

#### Particle System Editor
- [ ] Place particle emitter entities in the scene
- [ ] Visual particle editor — adjust emission rate, lifetime, velocity, size, color over time
- [ ] Presets for common effects: torch fire, candle flame, smoke, dust, sparks, embers
- [ ] Real-time preview in the viewport while editing parameters
- [ ] Save/load particle presets as reusable assets

#### Water Body Editor
- [ ] Place water surface entities (rectangular water plane)
- [ ] Set water dimensions and elevation in meters
- [ ] Water property editor: color tint, opacity, reflection strength, wave speed/amplitude
- [ ] Presets: still bath, gentle pool, flowing stream

---

### Phase 5F: Editor Utilities
**Goal:** Quality-of-life tools that make the editor efficient and informative.

#### Performance Overlay
- [ ] FPS counter (always visible in corner)
- [ ] Frame time graph (rolling chart of frame times)
- [ ] Draw call count, triangle count, texture memory usage
- [ ] Per-object stats (vertex count on selected object)

#### Console / Log Panel
- [ ] In-editor log viewer (scrollable, filterable by severity)
- [ ] Color-coded messages (info=white, warning=yellow, error=red)
- [ ] Search/filter log messages

#### Additional Tools
- [ ] Screenshot capture (F12 or menu — saves to a screenshots folder)
- [ ] Fullscreen viewport toggle (hide all panels for a clean view)
- [ ] Keyboard shortcut reference panel (searchable list of all hotkeys)
- [ ] Welcome screen / getting started tutorial on first launch
- [ ] Scene statistics panel (total entities, meshes, textures, lights, memory usage)
- [ ] Validation warnings (e.g., "Light has no shadow map", "Texture missing", "Object outside scene bounds")

---

### Phase 5G: Environment Painting
**Goal:** Paint natural environments directly onto surfaces — grass, gravel, trees, paths, streams — using an intuitive brush-based system. This is the primary tool for building outdoor scenes around the Temple complex.

#### Foliage Brush
- [ ] Grass rendering system (instanced billboards or geometry shader, leveraging existing instanced rendering)
- [ ] Foliage brush tool — paint grass/flowers/low plants onto any surface with adjustable radius and density
- [ ] Density falloff (full density at brush center, tapering at edges for natural blending)
- [ ] Random rotation, scale, and tint variation per instance (configurable ranges)
- [ ] Wind animation (vertex shader sway — amplitude, frequency, direction as brush parameters)
- [ ] Foliage presets: short grass, tall grass, wildflowers, reeds, desert scrub
- [ ] Density map layer — a per-surface texture controlling where foliage can grow (paintable in-editor)

#### Scatter Brush
- [ ] Scatter brush tool — paint rocks, gravel, debris, fallen leaves onto surfaces
- [ ] Object palette — select which meshes to scatter (small rocks, pebbles, pottery shards)
- [ ] Random placement within brush radius with configurable spacing and overlap rules
- [ ] Surface alignment — scattered objects orient to the surface normal (rocks sit flat on slopes)
- [ ] Scale/rotation randomization ranges per object type
- [ ] Eraser mode — remove scattered objects within brush radius

#### Tree and Large Object Placement
- [ ] Tree brush — paint clusters of trees with species selection and spacing rules
- [ ] Single-place mode — click to place one tree precisely, then adjust with transform gizmos
- [ ] Minimum spacing enforcement (trees don't overlap trunks)
- [ ] Tree species presets: olive, cedar, palm, acacia (with placeholder meshes until real models are imported)
- [ ] LOD system for trees (full mesh up close, billboard at distance — leverages instanced rendering)

#### Path and Road Tool
- [ ] Spline-based path drawing — click waypoints, engine generates a smooth path between them
- [ ] Path width and material selection (gravel, dirt, stone pavers, sand)
- [ ] Terrain texture blending along path edges (smooth transition from path to surrounding ground)
- [ ] Automatic foliage clearing — foliage within the path footprint is removed
- [ ] Path presets: narrow footpath, wide road, stone walkway

#### Water Painting
- [ ] Stream/river spline tool — draw a path, engine generates a flowing water surface mesh
- [ ] Stream width and depth controls per waypoint (narrow creek to wide river)
- [ ] Flow direction and speed (derived from spline direction)
- [ ] Pool/pond tool — draw a closed shape to create a still water body
- [ ] Water material integration (reflective/refractive shader from Phase 6, or basic placeholder)
- [ ] Automatic bank blending — terrain textures transition to wet sand/mud near water edges

#### Biome Presets
- [ ] Biome system — named combinations of ground texture + foliage + scatter + trees
- [ ] Built-in presets: "Garden" (green grass, flowers, olive trees), "Desert" (sand, scrub, rocks), "Temple Courtyard" (stone pavers, sparse grass at edges), "Cedar Forest" (forest floor, ferns, cedar trees)
- [ ] Paint entire areas with a biome brush — applies all layers at once
- [ ] Per-layer override — paint a biome then selectively adjust individual layers
- [ ] Save/load custom biome presets

#### Performance
- [ ] Frustum culling for foliage instances (don't submit off-screen grass to the GPU)
- [ ] Distance-based density fade (reduce foliage density at distance, fade out beyond a threshold)
- [ ] Chunk-based spatial partitioning (group nearby foliage into spatial cells for efficient culling)
- [ ] Target: 60 FPS with dense grass fields covering the Temple courtyard (~100k instances visible)

---

### Phase 5 Milestone
**A complete scene editor where architectural environments can be designed, textured, lit, and saved entirely through the GUI.** The Tabernacle and Solomon's Temple can be built in-editor by a non-programmer. Outdoor areas around the Temple can be painted with grass, trees, paths, and water using the environment painting tools.

---

## Phase 6: Particle and Effects System
**Goal:** Fire, smoke, water, and other real-time visual effects.

These engine features are prerequisites for the editor's effects tools (Phase 5E) but can be developed alongside the early editor phases.

### Fire and Flame
- [ ] GPU particle system (compute shader driven for thousands of particles at 60 FPS)
- [ ] Billboard particle rendering (camera-facing quads with alpha blending)
- [ ] Fire shader (animated color gradient: white core → yellow → orange → red → smoke)
- [ ] Torch fire preset (upward emission with flickering, ember sparks)
- [ ] Candle flame preset (small, gentle, mostly yellow)
- [ ] Campfire preset (larger, with smoke and floating embers)
- [ ] Light coupling — fire emitters automatically create a flickering point light

### Smoke and Atmosphere
- [ ] Smoke particles (grey, slow-rising, expanding, fading)
- [ ] Dust motes (tiny, slow, ambient particles in indoor spaces)
- [ ] Incense smoke (thin, wispy, rising column — perfect for the Tabernacle)

### Water
- [ ] Water surface shader (reflective/refractive with Fresnel effect)
- [ ] Animated water normals (scrolling normal maps for wave appearance)
- [ ] Water bath/pool (contained rectangular body — no terrain needed)
- [ ] Depth-based color (water darkens with depth)
- [ ] Water caustics (animated light patterns projected onto surfaces below water — scrolling noise texture modulates light intensity, simulating refraction through waves)
- [ ] Optional: simple wave vertex displacement

### Milestone
Torches with flickering fire illuminate a room, smoke rises from an altar of incense, and a bronze laver contains reflective water.

---

## Phase 7: Animation
**Goal:** Bring the world to life with skeletal animation and motion.

Animated objects and characters are essential for doors, swinging censers, priestly processions, and any living scene. glTF already carries skeletal animation data — this phase makes the engine able to play it.

### Skeletal Animation System
- [ ] Bone/joint hierarchy (skeleton loaded from glTF)
- [ ] Skinned mesh rendering (vertex skinning with bone weights in the vertex shader)
- [ ] Animation clip playback (keyframe interpolation — position, rotation, scale)
- [ ] Animation blending (cross-fade between clips, layered blending)
- [ ] Animation state machine (idle → walk → run transitions with conditions)
- [ ] Root motion support (animation drives entity movement, not just visual)

### Object Animation
- [ ] Property animation system (animate any numeric property over time — position, rotation, color, intensity)
- [ ] Animation curves (linear, ease-in/out, cubic bezier)
- [ ] Looping, ping-pong, and one-shot playback modes
- [ ] Animation events (trigger callbacks at specific keyframes — play sound, spawn particles)

### Inverse Kinematics (IK)
- [ ] Two-bone IK solver (arms, legs — reach for a target position)
- [ ] Foot IK (plant feet correctly on slopes, stairs, and uneven terrain)
- [ ] Hand IK (reach for door handles, grab objects, brace against walls)
- [ ] Look-at / head IK (characters turn head toward points of interest)
- [ ] IK blending with animation clips (IK layer on top of baked animations)

### glTF Animation Import
- [ ] Import skeletal animations from glTF files
- [ ] Import morph target / blend shape animations
- [ ] Multiple animation clips per model (walk, idle, gesture)

### Milestone
Animated characters walk through the Temple courts, doors swing open on approach, and a golden censer swings rhythmically over the altar of incense.

---

## Phase 8: Physics
**Goal:** Physical simulation for realistic object interaction, cloth, and world dynamics.

Physics enables curtains blowing in the wind, objects responding to gravity, doors with realistic hinges, and the linen fabrics of the Tabernacle draping naturally.

### Rigid Body Dynamics
- [ ] Physics engine integration (Bullet Physics or Jolt — evaluate both)
- [ ] Rigid body component (mass, friction, restitution)
- [ ] Collision shapes (box, sphere, capsule, convex hull, triangle mesh)
- [ ] Gravity and force application
- [ ] Collision detection and response (bouncing, sliding, resting)
- [ ] Kinematic bodies (scripted movement that still collides — doors, platforms)
- [ ] Physics-based character controller (replace current AABB controller)

### Constraints and Joints
- [ ] Hinge joints (doors, gates, lids)
- [ ] Spring/damper constraints (swinging objects, suspension)
- [ ] Fixed joints (weld objects together)
- [ ] Rope/chain constraints (hanging lamps, chains)

### Cloth Simulation
- [ ] Cloth component (mass-spring or position-based dynamics)
- [ ] Wind interaction (cloth responds to wind direction and strength)
- [ ] Collision with rigid bodies (cloth drapes over objects)
- [ ] Pin constraints (attach cloth corners to fixed points — hanging curtains)
- [ ] Presets: linen curtain, tent fabric, priestly garment, banner/flag

### Milestone
The Tabernacle's linen curtains sway gently, the entrance veil drapes realistically from its poles, and doors throughout Solomon's Temple swing on hinged joints.

---

## Phase 9: Polish and Features
**Goal:** Complete the experience — UI, audio, usability, and accessibility.

### Features
- [ ] In-game UI system (menus, HUD, information panels/plaques)
- [ ] Text rendering (TrueType fonts)
- [ ] Audio system (background music, ambient sounds)
- [ ] Spatial audio (3D positioned sound sources — crackling torch, splashing water)
- [ ] Scene/level configuration files (define scenes in data, not code)
- [ ] Settings system (resolution, quality presets, keybindings)
- [ ] Loading screens (for scene transitions)
- [ ] Information plaques — approach an object to see a text description

### Camera Modes
- [ ] Camera mode system (switchable projection and control schemes per scene/game)
- [ ] First-person camera (existing — WASD + mouse look, perspective projection)
- [ ] Third-person camera (follow entity with orbit controls, perspective projection)
- [ ] Isometric camera (fixed-angle orthographic projection, click-to-move input, Diablo-style)
- [ ] Top-down camera (overhead orthographic, suitable for strategy or map views)
- [ ] Cinematic camera (spline-based flythrough for guided tours and cutscenes)

### Localization
- [ ] Multi-language text support (UTF-8, language selection)
- [ ] Translatable string table system (all UI/plaque text referenced by key, not hardcoded)
- [ ] Hebrew, Greek, and Latin text rendering (right-to-left support for Hebrew)
- [ ] Language selection in settings menu

### Accessibility
- [ ] Colorblind modes (protanopia, deuteranopia, tritanopia filters)
- [ ] Subtitle / closed caption system for spatial audio cues
- [ ] Fully remappable controls (keyboard, mouse, gamepad)
- [ ] UI scaling options (for readability at different resolutions/distances)
- [ ] High-contrast mode for UI elements

### Rendering Enhancements
- [ ] Subsurface scattering (SSS) — light transmission through thin/translucent materials
  - Per-material SSS parameters: thickness, transmission color, scattering distance
  - Wrap lighting model for thin surfaces (curtains, fabric, leaves, candle wax)
  - Thickness map support (variable transmission across a surface)
  - The Tabernacle's dyed linen curtains should glow softly when sunlight hits the exterior
  - Fast approximation: pre-integrated skin/fabric BRDF lookup (no ray marching needed)
- [ ] Volumetric fog / god rays (light shafts through the tent entrance, dust in sunbeams)
- [ ] Screen-space global illumination (SSGI) — real-time dynamic indirect light

### Milestone
A complete walkthrough experience with UI, audio, information displays, multi-language support, accessibility options, and advanced material rendering (SSS, volumetric effects).

---

## Phase 10: Distribution
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
- [ ] Texture compression (BC7 for desktop, ASTC for mobile — compress on import, decompress on GPU)
- [ ] Automatic mipmap generation with quality filtering options
- [ ] Asset cooking / baking (preprocess models, textures, shaders into optimized binary format)
- [ ] Asset manifest and dependency tracking (know exactly what each scene needs)
- [ ] Hot-reload during development (detect changed assets, reload without restarting)

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

## Phase 11: Advanced Rendering
**Goal:** Push visual fidelity with modern techniques.

### Screen-Space Effects
- [ ] Screen-space reflections / SSR (real-time reflections on wet floors, polished bronze/gold — complements IBL)
- [ ] Screen-space global illumination / SSGI (one-bounce diffuse indirect lighting from depth+color buffers — practical "free" GI layer, complements probe-based approaches)
- [ ] Depth of field (cinematic focus effect for guided tours and screenshots)
- [ ] Motion blur (per-object and camera-based)

### Advanced Materials
- [ ] Subsurface scattering / SSS (light bleeding through thin materials — linen curtains, wax candles, marble, skin; hybrid screen-space diffusion approach or ReSTIR-path-tracing diffusion when RT available; ref: NVIDIA SIGGRAPH 2025)
- [ ] Anisotropic reflections (brushed metal, hair, silk fabrics)
- [ ] Strand-based hair and fur rendering (physically-based hair model with proper light scattering — relevant for animal fur, priestly garment fringes; ref: MachineGames/Indiana Jones, SIGGRAPH 2025)
- [ ] Neural texture compression (2-4x memory reduction via trained neural decompression in shaders — forward-looking, requires cooperative vector hardware support)

### Global Illumination
- [ ] Baked lightmaps (pre-computed GI for static architectural scenes — ideal for walkthroughs)
- [ ] Light probes (capture local lighting conditions at probe positions — varying lighting between rooms)
- [ ] Reflection probes (local cubemap captures for accurate indoor reflections — Holy Place vs Holy of Holies)
- [ ] Light probe blending (smooth transitions between probe volumes)
- [ ] Real-time irradiance probe GI (idTech 8 "Fast as Hell" approach — stochastic probe sampling with surfel shading, fully dynamic, proven at 60 FPS on consoles; replaces baked lighting with real-time probes)
- [ ] Surfel-based GI (pre-generate surfels per asset at import time, software ray-trace between surfels for indirect lighting — geometry-agnostic, works without hardware RT)
- [ ] Radiance cascades (Alexander Sannikov's approach — constant-cost GI independent of scene complexity and light count; 2D proven in production, 3D extension is active research area)
- [ ] ReSTIR GI (reservoir-based spatiotemporal importance resampling for indirect illumination — dramatically improves convergence when hardware RT is available)

### Vulkan and Ray Tracing
- [ ] Vulkan rendering backend (alternative to OpenGL)
- [ ] Vulkan descriptor heap (VK_EXT_descriptor_heap — simplified resource binding, replaces legacy descriptor set model)
- [ ] Ray tracing — reflections (hardware-accelerated on supported GPUs)
- [ ] Ray tracing — ambient occlusion
- [ ] Ray tracing — global illumination
- [ ] ReSTIR DI (reservoir-based spatiotemporal importance resampling for direct illumination — enables hundreds of shadow-casting lights with stochastic evaluation at fixed cost; ref: NVIDIA RTXDI)
- [ ] Partitioned top-level acceleration structures / PTLAS (divide scene into clusters, selectively rebuild only changed partitions — 100x faster BVH updates; ref: NVIDIA RTX Mega Geometry, DXR 2.0 CLAS)
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
- [ ] Virtual shadow maps (massive virtual texture shadow map — only allocate tiles visible to camera, consistent detail at all distances, eliminates cascade seams)
- [ ] Percentage-closer soft shadows / PCSS (contact-hardening shadows — sharp near caster, soft further away)
- [ ] Stochastic direct lighting (MegaLights-style fixed-budget approach — supports orders of magnitude more shadow-casting lights at constant cost via stochastic evaluation and temporal accumulation; ref: Epic Games SIGGRAPH 2025)

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
- [ ] GPU-driven draw submission (indirect draw calls — GPU decides what to draw, eliminating CPU bottleneck)
- [ ] GPU frustum and occlusion culling (Hi-Z occlusion culling in compute shader — skip objects hidden behind other objects)
- [ ] Variable-rate shading / variable-rate compute (control shading rate per screen region — full rate for detail areas, reduced rate for flat surfaces; ref: idTech 8 VRCS)
- [ ] GL_EXT_mesh_shader integration (OpenGL mesh shaders — replace vertex/geometry pipeline with task+mesh shader stages for GPU-driven geometry processing; available on AMD RDNA2+ via Mesa, avoids requiring Vulkan)
- [ ] Bindless textures and resources (eliminate texture binding overhead — all textures resident and GPU-addressable)

### Performance
- [x] Frustum culling (skip objects outside camera view)
- [ ] Volumetric lighting (god rays, fog)

### VR / Immersive Rendering
- [ ] OpenXR integration (cross-platform VR/AR runtime)
- [ ] Stereoscopic rendering (dual-eye viewpoints with correct IPD)
- [ ] VR locomotion system (teleport, smooth movement, comfort options)
- [ ] VR interaction (grab, point, inspect objects)
- [ ] VR performance target (90 FPS stereo — requires aggressive optimization)
- [ ] Foveated rendering (reduce detail in peripheral vision on supported headsets)

### Milestone
Hybrid rendering with software and hardware ray-traced effects, real-time global illumination (probe-based and/or surfel GI), GPU-driven rendering pipeline, VR walkthroughs, and tessellation on supported hardware. Scalable from integrated GPUs (SSGI + probes) to discrete RT hardware (ReSTIR + full path tracing).

---

## Phase 12: Adaptive Geometry System
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

## Phase 13: Atmospheric Rendering
**Goal:** Procedural sky, weather, and time of day.

### Features
- [ ] Procedural sky model (Rayleigh/Mie scattering)
- [ ] Full day/night cycle with sun/moon positioning
- [ ] Volumetric clouds (ray-marched or noise-based)
- [ ] Dynamic time-of-day lighting (sun color/intensity changes)
- [ ] Stars and night sky
- [ ] Weather effects (rain, dust, fog density changes)
- [ ] God rays / crepuscular rays through clouds

### Milestone
A living sky with dynamic clouds, day/night transitions, and atmospheric lighting.

---

## Phase 14: Scripting and Interactivity
**Goal:** Allow scene creators to add behavior and interactivity without writing C++.

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

### Milestone
A scene where doors open, torches flicker, guided tours run, NPCs walk patrol routes, and information appears — all configured in the editor without code.

---

## Phase 15: Terrain and Landscape (CORE COMPLETE — Phase 5I)
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
- [ ] Terrain chunking (split large terrains into tiles for streaming and culling) — deferred (not needed for 256m-2km scope; scale single heightmap to 2049x2049 instead)

### Milestone
Outdoor landscapes surrounding the Temple complex — hills, valleys, and the Kidron Valley with terrain elevation, ready for environment painting from Phase 5G.

---

## Phase 16: 2D Game and Scene Support
**Goal:** Enable the creation of 2D games and scenes alongside the existing 3D capabilities — sprite-based rendering, 2D physics, tilemaps, and a dedicated 2D editor workflow.

Phase 9's camera modes (isometric, top-down, orthographic) provide the viewing foundation. This phase adds the rendering, physics, and tooling needed to build complete 2D experiences.

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

## Phase 17: Procedural Generation
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

## Commercial Vision

### Engine Licensing
Vestige has the long-term potential to be licensed to other developers as a standalone engine and editor. The goal is to provide a complete, accessible creation tool — particularly suited to architectural visualization, historical reconstruction, and exploration experiences.

#### Licensing Model
- **Free tier:** Free to use for learning, non-commercial projects, and small teams
- **Revenue royalty:** 5% royalty on gross revenue above a threshold (e.g., first $100,000 revenue-free) for commercial products built with Vestige
- **Alternative:** One-time or subscription license options for studios that prefer predictable costs
- **Open development:** Transparent roadmap, community feedback shapes priorities

#### What Makes Vestige Licensable
- Complete editor — non-programmers can build full scenes without writing code
- Metric-scale architectural tools — purpose-built for room/building construction
- Biblical/historical focus — a niche no other engine specifically serves
- Modern rendering — PBR, shadows, particles, water, atmospheric effects
- Cross-platform — Linux and Windows from day one

#### Prerequisites (before licensing is viable)
- [ ] Stable, well-documented editor (Phase 5 complete)
- [ ] Scene save/load working reliably
- [ ] Comprehensive documentation and tutorials
- [ ] Bug tracker and version release process
- [ ] Engine API documentation for advanced users who do want to code
- [ ] License agreement and terms drafted
- [ ] Website and community channels

### Milestone
Vestige available for download with a clear license, documentation, and at least one showcase project (Tabernacle walkthrough) demonstrating its capabilities.

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
