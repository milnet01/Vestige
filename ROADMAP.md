# Vestige Engine Roadmap

This document outlines the phased development plan for the Vestige 3D Engine.

---

## Phase 1: Foundation
**Goal:** Open a window, render a 3D shape, move a camera around it.

This phase establishes the project skeleton, build system, and core engine loop.

### Features
- [ ] Project setup (CMake, dependency fetching, .gitignore)
- [ ] GLFW window creation and OpenGL 4.5 context
- [ ] glad OpenGL function loader
- [ ] Basic engine loop (init → loop → shutdown)
- [ ] Timer (delta time, FPS tracking)
- [ ] Logger (console output with severity levels)
- [ ] Shader loading and compilation (vertex + fragment)
- [ ] Render a colored triangle (hello world of 3D graphics)
- [ ] Render a 3D cube with perspective projection
- [ ] Basic camera (position, look direction, perspective)
- [ ] Keyboard input (WASD movement, mouse look)
- [ ] Event Bus (basic publish/subscribe)
- [ ] Window resize handling
- [ ] Basic unit test infrastructure (Google Test)

### Milestone
A window showing a colored 3D cube that the user can fly around using keyboard and mouse.

---

## Phase 2: 3D World
**Goal:** Load 3D models, apply textures, and light the scene.

### Features
- [ ] Mesh class (vertex buffer, index buffer, vertex array)
- [ ] OBJ model loading (simple format, good for starting)
- [ ] Texture loading (PNG/JPG via stb_image)
- [ ] UV mapping and texture coordinates
- [ ] Basic materials (diffuse color, specular, shininess)
- [ ] Blinn-Phong lighting model
- [ ] Directional light (sun)
- [ ] Point lights (lamps, candles)
- [ ] Spot lights (focused beams)
- [ ] Multiple objects in a scene
- [ ] Depth testing and face culling
- [ ] Wireframe debug rendering mode

### Milestone
A lit scene with multiple textured 3D models (e.g., a simple room with objects).

---

## Phase 3: Scene Management
**Goal:** Organize the world into a scene graph with entities and components.

### Features
- [ ] Entity class with component system
- [ ] Transform component (position, rotation, scale, parent-child hierarchy)
- [ ] MeshRenderer component
- [ ] Camera component
- [ ] Light components (Directional, Point, Spot)
- [ ] Scene class (holds entity hierarchy)
- [ ] SceneManager (load, unload, switch scenes)
- [ ] Resource manager (caching loaded assets)
- [ ] Gamepad/controller input (Xbox, PlayStation via GLFW)
- [ ] First-person character controller (walking, looking, collision with ground)
- [ ] Basic bounding-box collision detection (prevent walking through walls)

### Milestone
Walk through a multi-room environment in first person with controller support.

---

## Phase 4: Visual Quality
**Goal:** Make it look good — shadows, better materials, post-processing.

### Features
- [ ] Shadow mapping (directional light shadows)
- [ ] Point light shadows (omnidirectional shadow maps)
- [ ] Normal mapping (surface detail without extra geometry)
- [ ] PBR materials (Physically Based Rendering — metallic/roughness workflow)
- [ ] Environment mapping / skybox
- [ ] HDR rendering and tone mapping
- [ ] Bloom post-processing effect
- [ ] MSAA anti-aliasing
- [ ] glTF model loading (modern, full-featured format)
- [ ] Ambient occlusion (SSAO)

### Milestone
A visually polished scene with realistic lighting, shadows, and materials.

---

## Phase 5: Polish and Features
**Goal:** Complete the experience — UI, audio, and usability.

### Features
- [ ] UI system (menus, HUD, information panels/plaques)
- [ ] Text rendering (TrueType fonts)
- [ ] Audio system (background music, ambient sounds)
- [ ] Spatial audio (3D positioned sound sources)
- [ ] Particle effects (dust motes, torch flames, smoke)
- [ ] Scene/level configuration files (define scenes in data, not code)
- [ ] Screenshot capture
- [ ] Performance profiler (frame time breakdown)
- [ ] Settings system (resolution, quality, keybindings)

### Milestone
A complete walkthrough experience with UI, audio, and visual effects.

---

## Phase 6: Distribution
**Goal:** Package and distribute the application.

### Features
- [ ] Steam SDK integration
- [ ] Steam achievements (if applicable)
- [ ] Installer/packaging for Windows
- [ ] Linux AppImage or Flatpak packaging
- [ ] Loading screens
- [ ] Save/load system (player position, settings)
- [ ] Controller button prompts (show correct icons for connected controller)

### Milestone
Application published on Steam.

---

## Phase 7: Advanced Rendering
**Goal:** Push visual fidelity with modern techniques.

### Features
- [ ] Vulkan rendering backend (alternative to OpenGL)
- [ ] Ray tracing — reflections (hardware-accelerated on supported GPUs)
- [ ] Ray tracing — ambient occlusion
- [ ] Ray tracing — global illumination
- [ ] Deferred rendering pipeline
- [ ] Volumetric lighting (god rays, fog)
- [ ] Frustum culling and occlusion culling

### Milestone
Hybrid rendering with ray-traced effects on supported hardware.

---

## Phase 8: Adaptive Geometry System
**Goal:** Handle massive geometric complexity automatically — original approach, not a copy of any existing engine.

### Problem Statement
High-fidelity 3D scenes (like a fully detailed Solomon's Temple) can contain hundreds of millions of triangles. Without automatic geometry management, artists must manually create LOD variants and the engine wastes GPU time rendering detail the player can't see.

### Research Direction (Original Approach)
Rather than replicating existing commercial solutions, Vestige will explore its own approach to automatic geometry management. Possible research areas:
- [ ] Automatic mesh simplification (quadric error metrics, academic literature)
- [ ] Cluster-based mesh decomposition (splitting meshes into independently cullable/LOD-able chunks)
- [ ] Screen-space error-driven selection (choose cluster detail based on projected pixel coverage)
- [ ] Compute shader-driven culling and selection pipeline
- [ ] Virtual geometry streaming (load/unload clusters on demand from disk)
- [ ] Mesh shader integration (via Vulkan mesh shader extensions on RDNA2+)
- [ ] Hybrid rasterization (hardware for large triangles, compute for sub-pixel)
- [ ] Novel approaches: SDF-based geometry, surfel-based rendering, or other non-traditional representations

### Design Principles
- Solve the problem from first principles — don't reverse-engineer other engines
- Build on published academic research (SIGGRAPH papers, GPU Pro/Gems, etc.)
- Innovate where possible — this is an opportunity to do something new
- Incremental: start with traditional LOD, evolve toward fully automatic

### Milestone
A system that lets artists import film-quality assets and the engine automatically manages complexity in real time.

---

## Phase 9: Atmospheric Rendering
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
