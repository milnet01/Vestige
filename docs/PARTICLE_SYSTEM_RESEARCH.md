# Particle System Editor Research

**Date:** 2026-03-21
**Engine:** Vestige (C++17, OpenGL 4.5, ImGui, GLM)
**Scope:** Research for adding a particle system with an in-editor emitter editor

---

## Table of Contents

1. [How Particle Emitter Editors Work in Major Engines](#1-how-particle-emitter-editors-work-in-major-engines)
2. [GPU vs CPU Particle Systems](#2-gpu-vs-cpu-particle-systems)
3. [Real-Time Particle Preview in Editor Panels](#3-real-time-particle-preview-in-editor-panels)
4. [Common Particle System Parameters](#4-common-particle-system-parameters)
5. [Billboard Rendering for Particles in OpenGL](#5-billboard-rendering-for-particles-in-opengl)
6. [Particle System Data Structures and Memory Management](#6-particle-system-data-structures-and-memory-management)
7. [Integration with Undo/Redo System](#7-integration-with-undoredo-system)
8. [Particle Serialization Formats](#8-particle-serialization-formats)
9. [Recommendations for Vestige](#9-recommendations-for-vestige)
10. [All Sources](#10-all-sources)

---

## 1. How Particle Emitter Editors Work in Major Engines

### Unity: Shuriken Particle System

Unity's particle system uses a **module-based architecture**. Each particle system is a component attached to a GameObject, and its behavior is configured through independent modules that can be enabled/disabled:

- **Main Module**: Duration, looping, start lifetime, start speed, start size, start rotation, start color, gravity modifier, simulation space, max particles.
- **Emission Module**: Rate over time, rate over distance, burst emissions at specific times.
- **Shape Module**: Emitter shape (sphere, hemisphere, cone, box, mesh, edge, circle) controlling spawn position and initial direction.
- **Over-Lifetime Modules**: Velocity over Lifetime, Color over Lifetime, Size over Lifetime, Rotation over Lifetime, Force over Lifetime, Noise.
- **By-Speed Modules**: Color by Speed, Size by Speed, Rotation by Speed.
- **Collision Module**: World/planes collision with bounce, lifetime loss, damping.
- **Sub-Emitters**: Spawn child particle systems on birth, death, or collision events.
- **Texture Sheet Animation**: Flipbook-style sprite animation.
- **Trails, Lights, Renderer**: Visual output configuration.

Key UI concept: Unity uses a **Curve Editor** for any parameter that changes over time. Parameters can be set as constants, random between two constants, curves, or random between two curves. Color parameters use a **Gradient Editor** with draggable color stops.

### Unreal Engine: Niagara

Niagara uses a **data-driven, stack-based architecture** that is more programmable than Unity's approach:

- **System**: Contains one or more Emitters.
- **Emitter**: Organized into four execution groups:
  - **Emitter Spawn**: One-time setup when the emitter starts (e.g., Spawn Rate, Spawn Burst Instantaneous).
  - **Emitter Update**: Per-frame emitter-level logic.
  - **Particle Spawn**: Per-particle initialization (e.g., Initialize Particle, Shape Location, Add Velocity).
  - **Particle Update**: Per-particle per-frame update (e.g., Gravity Force, Scale Color, Solve Forces and Velocity, Update Age).
- **Renderer**: Sprite Renderer, Mesh Renderer, Ribbon Renderer, etc.

Each group is a stack of **modules** that can be rearranged, added, or removed. Modules read and write to a shared **parameter namespace**. Niagara provides a **Curve Editor** and the ability to sample curves as data interfaces.

Key takeaway: Niagara's modular stack approach provides great flexibility but is complex. For a small engine, the Unity-style fixed-module approach is more practical.

### Godot: GPUParticles3D

Godot separates the concept cleanly:

- **GPUParticles3D node**: Controls amount, lifetime, one_shot, preprocess, speed_scale, explosiveness, randomness, visibility_aabb, local_coords, draw_order, fixed_fps, interpolate.
- **ParticleProcessMaterial**: A dedicated material resource that controls all particle behavior — direction, spread, gravity, initial velocity, angular velocity, orbit velocity, linear/radial acceleration, damping, scale, color, hue variation, turbulence, sub-emitters.
- **Emission shape**: Point, sphere, sphere surface, box, ring, or custom mesh positions.

Godot uses **CurveTexture** resources for over-lifetime parameters — a 1D texture that encodes a curve, sampled on GPU. Color gradients use **GradientTexture1D**.

Key takeaway: Godot's approach of encoding curves as textures for GPU sampling is elegant and efficient.

### O3DE (Open 3D Engine)

O3DE lacks a native particle system — it relies on **PopcornFX**, a third-party middleware. PopcornFX uses a node-based visual scripting editor for particle logic, which is too complex for a small custom engine. The integration is plugin-based, with the PopcornFX editor as a standalone tool.

**Sources:**
- [Unity Particle System Modules](https://docs.unity3d.com/Manual/ParticleSystemModules.html)
- [Unity Shape Module](https://docs.unity3d.com/Manual/PartSysShapeModule.html)
- [Unity Main Module](https://docs.unity.cn/2021.1/Documentation/Manual/PartSysMainModule.html)
- [Niagara Emitter Reference (UE4)](https://docs.unrealengine.com/4.27/en-US/RenderingAndGraphics/Niagara/EmitterReference)
- [Niagara Particle Update Group](https://dev.epicgames.com/documentation/en-us/unreal-engine/particle-update-group-reference-for-niagara-effects-in-unreal-engine)
- [Creating Particle Systems in Unreal vs Unity](https://dev.epicgames.com/community/learning/tutorials/dae5/unreal-engine-creating-particle-systems-in-unreal-vs-unity)
- [Godot GPUParticles3D](https://docs.godotengine.org/en/stable/classes/class_gpuparticles3d.html)
- [Godot Particle Systems 3D](https://docs.godotengine.org/en/stable/tutorials/3d/particles/index.html)
- [Godot Particle System State & Future](https://godotengine.org/article/progress-report-state-of-particles/)
- [PopcornFX O3DE Integration](https://www.popcornfx.com/popcornfx-o3de-integration/)
- [Mastering Particle Systems in Unreal Engine](https://sdlccorp.com/post/mastering-particle-systems-in-unreal-engine/)

---

## 2. GPU vs CPU Particle Systems

### CPU-Based Particle Systems

**Architecture:** Particles are stored in CPU memory. Each frame, the CPU iterates all particles, updates positions/velocities/lifetimes, kills dead particles, and uploads position+color data to a VBO via `glBufferSubData()`. The GPU only handles rendering.

**Performance numbers (from C++ Stories benchmarks on AMD HD 5500):**
- 500,000 particles: 70-80 FPS
- 1,000,000 particles: 30-45 FPS

**Pros:**
- Simple to implement and debug.
- Easy to integrate with editor UI (parameters live in CPU memory, trivially readable).
- Straightforward undo/redo — all state is in CPU-accessible structs.
- No compute shader dependency (works on older OpenGL).
- Easy to add complex logic (collision with scene, conditional spawning, etc.).

**Cons:**
- CPU-GPU synchronization bottleneck when uploading VBOs each frame.
- CPU iteration becomes expensive at high particle counts.
- Less parallelism than GPU.

### GPU-Based Particle Systems

**Architecture:** Particle data lives entirely in VRAM as SSBOs (Shader Storage Buffer Objects) or transform feedback buffers. Compute shaders or vertex shaders update particles. The CPU only sets uniforms (emission parameters, forces) and issues dispatch/draw commands.

**Two main approaches for OpenGL:**

**A. Compute Shader + SSBO (OpenGL 4.3+):**
- Particle data stored in SSBOs (position, velocity, lifetime, color, size).
- A compute shader dispatches one thread per particle for simulation.
- Dead/alive lists managed via atomic counters.
- Indirect draw commands avoid CPU readback of particle count.
- Wicked Engine achieves millions of particles with bitonic sorting for transparency.

**B. Transform Feedback (OpenGL 3.0+):**
- Particle data stored as vertex attributes in VBOs.
- A vertex/geometry shader reads from VBO A, writes updated particles to VBO B (ping-pong).
- `GL_RASTERIZER_DISCARD` disables rendering during the update pass.
- Query objects track how many particles survived for the draw call.
- Simpler than compute shaders but less flexible (no random access, harder dead/alive management).

**Performance numbers:**
- GPU compute shader: 2 million+ textured particles at 60 FPS (from Crisspl/GPU-particle-system on GitHub).
- Transform feedback: Comparable to compute for simple physics, but harder to scale.

**Pros:**
- Massively parallel — handles millions of particles easily.
- No CPU-GPU transfer bottleneck (data stays in VRAM).
- Frees CPU for gameplay logic.

**Cons:**
- Harder to debug (GPU data requires staging buffers and sync points to read back).
- Complex to integrate with editor undo/redo (reading back GPU state is slow).
- Compute shaders require OpenGL 4.3+ (Vestige targets 4.5, so this is fine).
- More complex spawning logic (atomic counters, indirect dispatch).

### Recommendation for Vestige

**Start with a CPU-based system, design the architecture to allow a GPU backend later.**

Rationale:
- For an editor, parameters must be readable/writable from CPU for ImGui widgets and undo/redo.
- The target use case (architectural walkthroughs) does not need millions of particles — dust motes, torch flames, incense smoke, rain need at most 10,000-50,000 particles.
- CPU-based is far simpler to debug and iterate on.
- The interface can be designed so that a GPU compute backend can replace the CPU update loop later without changing the editor or serialization code.

**Sources:**
- [Flexible Particle System - OpenGL Renderer (C++ Stories)](https://www.cppstories.com/2014/07/flexible-particle-system-opengl-renderer/)
- [Flexible Particle System - Renderer Optimization (C++ Stories)](https://www.cppstories.com/2015/03/flexible-particle-system-renderer/)
- [GPU-Based Particle Simulation (Wicked Engine)](https://wickedengine.net/2017/11/gpu-based-particle-simulation/)
- [OpenGL Particle Systems (Vercidium)](https://vercidium.com/blog/opengl-particle-systems/)
- [GPU Particle System with 2M Particles (GitHub)](https://github.com/Crisspl/GPU-particle-system)
- [GPU Particles - DirectX 11 (GitHub)](https://github.com/Brian-Jiang/GPUParticles)
- [Compute Particles Sample (NVIDIA)](https://docs.nvidia.com/gameworks/content/gameworkslibrary/graphicssamples/opengl_samples/computeparticlessample.htm)
- [OpenGL 4.3 SSBO Introduction (Geeks3D)](https://www.geeks3d.com/20140704/tutorial-introduction-to-opengl-4-3-shader-storage-buffers-objects-ssbo-demo/)
- [Compute Shaders + SSBO Particles Demo (Geeks3D HackLab)](https://www.geeks3d.com/hacklab/20200117/demo-opengl-4-3-compute-shaders-particles-ssbo/)
- [GPU Particle System with OpenGL Compute Shader (GitHub)](https://github.com/Jax922/gpu-particle-system)
- [OpenGL Particle Acceleration on GPU (GitHub)](https://github.com/MauriceGit/Partikel_accelleration_on_GPU)
- [NVIDIA Feedback Particles Sample](https://archive.docs.nvidia.com/gameworks/content/gameworkslibrary/graphicssamples/opengl_samples/feedbackparticlessample.htm)

---

## 3. Real-Time Particle Preview in Editor Panels

### Approach A: Inline Scene Preview (Recommended)

The simplest approach is to render particle systems directly in the main 3D viewport, just like any other entity. The editor would show particles playing in real-time as the user adjusts parameters. This is what Unity and Godot do.

**Implementation for Vestige:**
- The particle system component updates every frame in the scene's update loop.
- The renderer draws particles as part of the normal render pass.
- ImGui inspector panel shows the emitter parameters; changes take effect immediately.
- A play/pause/restart button in the inspector allows resetting the particle simulation.

### Approach B: Isolated Preview Panel (Like Material Preview)

Vestige already has a `MaterialPreview` class that renders a sphere to an offscreen FBO and displays it via `ImGui::Image()`. The same pattern can render a particle system in isolation.

**Implementation:**
- Create a `ParticlePreview` class similar to `MaterialPreview`.
- Use a dedicated FBO (e.g., 256x256).
- Render the particle emitter against a dark/neutral background.
- Display the FBO texture in an ImGui panel.
- The preview re-renders every frame (unlike material preview, which only re-renders on changes) because particles are animated.

**Consideration:** For Vestige, Approach A (inline scene preview) is better because particles interact with the scene (lighting, depth, fog). A separate preview panel could supplement this for quick effect design.

### Existing Open-Source Examples

- **Im-Rises/ParticleSystem**: C++ + OpenGL + GLFW + ImGui. Real-time particle preview with ImGui parameter sliders. Demonstrates the "inline render + ImGui panel" pattern.
- **GemParticles**: OpenGL particle engine with ImGui for parameter editing.
- **nicolas-risberg VFX Editor**: ImGui-based VFX editor with color picker, curve editing, and real-time preview. Emitter parameters opened in separate ImGui windows.
- **ParticleSimulator**: SDL + OpenGL particle simulator with ImGui widgets.

**Sources:**
- [ParticleSystem - C++ OpenGL ImGui (GitHub)](https://github.com/Im-Rises/ParticleSystem)
- [GemParticles - OpenGL Particle Engine (GitHub)](https://github.com/frtru/GemParticles)
- [VFX Editor using ImGui (Nicolas Risberg)](https://nicolas-risberg.github.io/2021-03-30/vfx-editor.html)
- [ParticleSimulator - SDL + OpenGL + ImGui (GitHub)](https://github.com/alexandra-zaharia/ParticleSimulator)
- [ImGui Particles (GitHub)](https://github.com/SandFoxy/imgui_particles)
- [Off-Screen Particle Rendering (Unity, GitHub)](https://github.com/slipster216/OffScreenParticleRendering)

---

## 4. Common Particle System Parameters

Based on analysis of Unity, Unreal/Niagara, Godot, and multiple open-source implementations, here is the comprehensive parameter list organized by category:

### 4.1 Emitter Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| Max Particles | int | Maximum alive particles at any time (pool size) |
| Duration | float | How long the emitter runs (seconds) |
| Looping | bool | Whether emitter repeats after duration |
| Prewarm | bool | Simulate N seconds on first frame for instant effects |
| Simulation Space | enum | Local (moves with parent) or World (stays in place) |

### 4.2 Emission Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| Rate over Time | float | Particles emitted per second |
| Rate over Distance | float | Particles per unit of emitter movement |
| Bursts | array | Timed bursts: {time, count, cycles, interval} |

### 4.3 Emission Shape

| Shape | Parameters | Description |
|-------|-----------|-------------|
| Point | — | All particles spawn at emitter origin |
| Sphere | radius, emit from shell/volume | Spawn within or on surface of sphere |
| Hemisphere | radius | Half-sphere emission |
| Cone | angle, radius, length, emit from base/volume | Conical emission (good for trails, fountains) |
| Box | size (x,y,z) | Rectangular volume (good for rain, dust) |
| Circle/Ring | radius, arc | 2D circular emission |
| Edge | length | Line segment emission |
| Mesh | mesh reference | Emit from vertices/surface of arbitrary mesh |

### 4.4 Initial Particle Properties (At Spawn)

| Parameter | Type | Description |
|-----------|------|-------------|
| Start Lifetime | float or range | How long the particle lives (seconds) |
| Start Speed | float or range | Initial velocity magnitude |
| Start Size | float or range (or vec3 for 3D) | Initial visual size |
| Start Rotation | float or range | Initial rotation angle (for billboard orientation) |
| Start Color | color or gradient | Initial RGBA color |
| Flip Rotation | float (0-1) | Chance of flipping rotation direction |
| Gravity Modifier | float | Multiplier for gravity applied to particles |

### 4.5 Over-Lifetime Modifiers (Per-Particle Per-Frame)

| Parameter | Representation | Description |
|-----------|---------------|-------------|
| Velocity over Lifetime | curve or vec3 | Additional velocity added over time |
| Color over Lifetime | gradient (RGBA) | Color change from birth to death |
| Size over Lifetime | curve | Size multiplier from birth to death |
| Rotation over Lifetime | curve | Angular velocity over time |
| Force over Lifetime | vec3 | Constant force (wind, etc.) |
| Drag/Damping | float or curve | Velocity reduction over time |
| Noise/Turbulence | frequency, amplitude, scroll speed | Procedural movement perturbation |

### 4.6 Rendering Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| Render Mode | enum | Billboard, Stretched Billboard, Mesh, Trail |
| Texture | texture ref | Sprite/atlas texture |
| Blend Mode | enum | Additive, Alpha Blend, Premultiplied Alpha |
| Sort Mode | enum | None, By Distance, Oldest First, Youngest First |
| Billboard Alignment | enum | Face Camera, Face Camera (Y-locked), Custom Axis |
| Texture Sheet | rows, cols, frame count | Sprite sheet animation parameters |
| Stretch | speed stretch, length stretch | For stretched billboards |
| Soft Particles | bool + distance | Fade near depth buffer intersections |

### 4.7 Parameter Value Types

Parameters in particle editors typically support multiple value modes:
- **Constant**: A single fixed value.
- **Random Between Two Constants**: Random value in [min, max] range at spawn.
- **Curve**: Value determined by a curve sampled over normalized lifetime (0 to 1).
- **Random Between Two Curves**: Random interpolation between two curves.
- **Gradient**: For color parameters, a gradient with draggable color stops.

This is a critical UI concept — each parameter needs a mode selector and the appropriate widget (slider, range, curve editor, gradient editor).

**Sources:**
- [Unity Particle System Main Module](https://docs.unity.cn/2021.1/Documentation/Manual/PartSysMainModule.html)
- [Unity Shape Module Reference](https://docs.unity3d.com/Manual/PartSysShapeModule.html)
- [Unity Particle Emissions and Emitters](https://docs.unity3d.com/6000.3/Documentation/Manual/particle-emissions-emitters.html)
- [Unity Modifying Gravity, Color, Size & Lifetime](https://learn.unity.com/tutorial/modifying-gravity-color-size-lifetime-of-particle-systems)
- [Godot GPUParticles3D](https://docs.godotengine.org/en/stable/classes/class_gpuparticles3d.html)
- [Roblox Particle Emitter Shapes](https://github.com/Roblox/creator-docs/blob/main/content/en-us/effects/particle-emitters.md)
- [Cocos Creator ShapeModule](https://docs.cocos.com/creator/3.8/manual/en/particle-system/emitter.html)
- [Babylon.js Shape Emitters](https://doc.babylonjs.com/features/featuresDeepDive/particles/particle_system/shape_emitters)
- [Cesium Particle Systems Introduction](https://cesium.com/learn/cesiumjs-learn/cesiumjs-particle-systems/)

---

## 5. Billboard Rendering for Particles in OpenGL

### The Core Technique

A billboard is a quad that always faces the camera. For particles, each particle becomes a camera-facing quad textured with a sprite.

**Camera-facing billboard math (vertex shader approach):**

Given:
- `CameraRight_worldspace` = column 0 of the view matrix (vec3)
- `CameraUp_worldspace` = column 1 of the view matrix (vec3)
- `particleCenter_worldspace` = particle position (vec3)
- `particleSize` = particle size (float)
- `squareVertices` = unit quad corners: (-0.5,-0.5), (0.5,-0.5), (-0.5,0.5), (0.5,0.5)

The world-space vertex position is:
```
vertexPosition_worldspace =
    particleCenter_worldspace
    + CameraRight_worldspace * squareVertices.x * particleSize
    + CameraUp_worldspace * squareVertices.y * particleSize;
```

This expands each particle center into a quad without needing a geometry shader.

### Instanced Rendering (Recommended Approach)

Instead of drawing each particle as a separate quad, use **instanced rendering**:

1. **Static quad VBO**: 4 vertices defining a unit quad (GL_STATIC_DRAW).
2. **Per-instance position+size buffer**: 4 floats per particle (x, y, z, size), updated every frame (GL_STREAM_DRAW).
3. **Per-instance color buffer**: 4 bytes per particle (RGBA), updated every frame (GL_STREAM_DRAW).

```
glVertexAttribDivisor(0, 0);  // Quad vertices - reused per instance
glVertexAttribDivisor(1, 1);  // Position+size - one per instance
glVertexAttribDivisor(2, 1);  // Color - one per instance

glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, aliveParticleCount);
```

This results in a single draw call for all particles, with roughly 4x bandwidth savings over rebuilding full quad geometry per particle.

### Buffer Update Strategy

**Buffer orphaning** (recommended for streaming data):
```cpp
glBufferData(GL_ARRAY_BUFFER, maxSize, nullptr, GL_STREAM_DRAW);  // Orphan
glBufferSubData(GL_ARRAY_BUFFER, 0, aliveCount * stride, data);   // Upload
```

**Persistent mapped buffers** (more advanced, OpenGL 4.4+):
- Map the buffer once with `GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT`.
- Use triple-buffering (3 buffer regions) to avoid GPU stalls.
- Write directly to mapped memory each frame.

### Transparency and Sorting

Particles with alpha blending require **back-to-front sorting** by camera distance. Sort the CPU-side particle array by distance before uploading to the VBO.

Additive blending (`GL_SRC_ALPHA, GL_ONE`) does **not** require sorting — order-independent. This is preferred for fire, sparks, magic effects.

### Soft Particles

To avoid hard intersections where particles clip into geometry:
1. Render scene depth to a texture (already available in Vestige's depth buffer).
2. In the particle fragment shader, compare fragment depth with scene depth.
3. Fade alpha when the difference is small:
```glsl
float sceneDepth = linearize(texture(depthTex, screenUV).r);
float particleDepth = linearize(gl_FragCoord.z);
float fade = clamp((sceneDepth - particleDepth) / softDistance, 0.0, 1.0);
fragColor.a *= fade;
```

Note: Depth must be linearized because the Z buffer is non-linear.

### Blend Mode Setup

| Mode | glBlendFunc | Sorting Required | Use Case |
|------|------------|-----------------|----------|
| Additive | `GL_SRC_ALPHA, GL_ONE` | No | Fire, sparks, glow |
| Alpha Blend | `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA` | Yes | Smoke, dust, clouds |
| Premultiplied | `GL_ONE, GL_ONE_MINUS_SRC_ALPHA` | Yes | Pre-multiplied alpha textures |

**Important:** Disable depth writing (`glDepthMask(GL_FALSE)`) when rendering particles, but keep depth testing enabled so particles are occluded by solid geometry.

**Sources:**
- [OpenGL Tutorial - Billboards](http://www.opengl-tutorial.org/intermediate-tutorials/billboards-particles/billboards/)
- [OpenGL Tutorial - Particles/Instancing](http://www.opengl-tutorial.org/intermediate-tutorials/billboards-particles/particles-instancing/)
- [LearnOpenGL - Particles](https://learnopengl.com/In-Practice/2D-Game/Particles)
- [Megabyte Softworks - Particle System (OpenGL 3)](https://www.mbsoftworks.sk/tutorials/opengl3/23-particle-system/)
- [Megabyte Softworks - Transform Feedback Particles (OpenGL 4)](https://www.mbsoftworks.sk/tutorials/opengl4/025-transform-feedback-particle-system/)
- [OGLdev Tutorial 28 - Transform Feedback Particles](https://www.ogldev.org/www/tutorial28/tutorial28.html)
- [NVIDIA Soft Particles Whitepaper (PDF)](https://developer.download.nvidia.com/whitepapers/2007/SDK10/SoftParticles_hi.pdf)
- [Implementing Soft Particles in WebGL/OpenGL ES](https://dev.to/keaukraine/implementing-soft-particles-in-webgl-and-opengl-es-3l6e)
- [OpenGL Soft Particles (Alessandro Ribeiro)](http://alessandroribeiro.thegeneralsolution.com/en/2021/01/09/openglstarter-soft-particles/)
- [High-Speed Off-Screen Particles (GPU Gems 3)](https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-23-high-speed-screen-particles)
- [Khronos Forums - Best Blending Mode for Particles](https://community.khronos.org/t/best-blending-mode-for-particles/14987)
- [Khronos Forums - Particle System Depth and Blend](https://community.khronos.org/t/particle-system-depth-and-blend/19564)

---

## 6. Particle System Data Structures and Memory Management

### Structure of Arrays (SoA) vs Array of Structures (AoS)

**AoS (traditional):**
```cpp
struct Particle {
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec4 color;
    float lifetime;
    float age;
    float size;
};
std::vector<Particle> particles;
```

**SoA (cache-optimized):**
```cpp
struct ParticleData {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> velocities;
    std::vector<glm::vec4> colors;
    std::vector<float> lifetimes;
    std::vector<float> ages;
    std::vector<float> sizes;
    size_t aliveCount = 0;
};
```

**Performance difference:** SoA can be 2-25x faster than AoS depending on the access pattern. When updating only positions, SoA avoids loading velocity/color/lifetime into cache lines. The C++ Stories particle system series demonstrates this approach.

**Recommendation for Vestige:** Use SoA. The update loop iterates one property at a time (update all positions, then all colors, etc.), which is the ideal access pattern for SoA.

### Pre-allocated Pool with Swap-on-Death

The standard pattern for particle memory management:

1. **Pre-allocate** arrays to `maxParticles` size at system creation. No runtime allocation.
2. **Track alive count** with a single integer (`m_aliveCount`).
3. **Kill particles** by swapping the dead particle with the last alive particle and decrementing `m_aliveCount`:
   ```
   // Kill particle at index i:
   std::swap(positions[i], positions[aliveCount - 1]);
   std::swap(velocities[i], velocities[aliveCount - 1]);
   // ... swap all arrays ...
   aliveCount--;
   ```
4. **Spawn particles** by initializing data at `positions[aliveCount]` and incrementing `aliveCount`.

This gives O(1) spawn and kill, zero fragmentation, and all alive particles are contiguous (ideal for VBO upload and cache access).

### Object Pool Pattern

From Game Programming Patterns: the Object Pool uses a **free list embedded in unused memory**. When a particle is dead, its memory stores a pointer to the next free slot. This works well for AoS but is unnecessary for the SoA swap-on-death approach, which is simpler and more cache-friendly.

### Generators and Updaters (C++ Stories Architecture)

The C++ Stories "Flexible Particle System" series proposes a clean separation:

- **ParticleData**: The SoA container. Just data, no logic.
- **ParticleGenerator**: Creates new particles, sets initial properties. Examples: `BoxGenerator`, `SphereGenerator`, `ColorGenerator`, `VelocityGenerator`.
- **ParticleUpdater**: Updates alive particles each frame. Examples: `GravityUpdater`, `ColorOverLifeUpdater`, `EulerUpdater`, `TimeUpdater`.
- **ParticleEmitter**: Owns a list of generators, controls emission rate. On emit, calls each generator to initialize a newly spawned particle.
- **ParticleSystem**: Owns ParticleData, a list of emitters, and a list of updaters. Each frame: emit new particles, then run updaters, then compact dead particles.

This is an excellent, modular architecture for a small engine.

**Sources:**
- [Flexible Particle System - The Container (C++ Stories)](https://www.cppstories.com/2014/04/flexible-particle-system-container/)
- [Flexible Particle System - Start (C++ Stories)](https://www.cppstories.com/2014/04/flexible-particle-system-start/)
- [Object Pool Pattern (Game Programming Patterns)](https://gameprogrammingpatterns.com/object-pool.html)
- [Pool Allocator for Games (Gamedeveloper.com)](https://www.gamedeveloper.com/programming/designing-and-implementing-a-pool-allocator-data-structure-for-memory-management-in-games)
- [Particle System Memory Allocation (Gillius)](https://gillius.org/articles/partmem.htm)
- [Building an Advanced Particle System (Gamedeveloper.com)](https://www.gamedeveloper.com/programming/building-an-advanced-particle-system)
- [SoA vs AoS Deep Dive (Azad)](https://azad2171.github.io/soa_vs_aos_memory_layout_cpp/)
- [SoA vs AoS (Medium)](https://medium.com/@azad217/structure-of-arrays-soa-vs-array-of-structures-aos-in-c-a-deep-dive-into-cache-optimized-13847588232e)
- [C++ Cache Locality for Game Developers 2025](https://markaicode.com/cpp-cache-locality-optimization-game-developers-2025/)
- [Particle System Data Structure (GameDev.net)](https://www.gamedev.net/forums/topic/117884-particle-system-data-structure/)
- [Memory Management for C++ Game Engines](https://palospublishing.com/memory-management-techniques-for-real-time-game-engines-in-c/)
- [2D Particle System (GitHub)](https://github.com/nintervik/2D-Particle-System)

---

## 7. Integration with Undo/Redo System

### The Challenge

Particle systems have two categories of state:
1. **Emitter configuration** (parameters like emission rate, lifetime, gravity, etc.) — this is what the user edits and what should be undoable.
2. **Runtime simulation state** (current particle positions, velocities, ages) — this is transient and should NOT be part of undo/redo.

This is a clean separation: undo/redo only needs to track changes to the emitter configuration, not the live particle simulation.

### Approach: Property-Level Commands (Recommended for Vestige)

Vestige already has `EditorCommand` with `execute()`, `undo()`, `getDescription()`, `canMergeWith()`, and `mergeWith()`. The existing `EntityPropertyCommand` pattern can be extended:

```cpp
class ParticlePropertyCommand : public EditorCommand
{
    Entity* m_entity;
    std::string m_propertyName;
    ParticlePropertyValue m_oldValue;
    ParticlePropertyValue m_newValue;

    void execute() override { setProperty(m_entity, m_propertyName, m_newValue); }
    void undo() override { setProperty(m_entity, m_propertyName, m_oldValue); }
};
```

**Mergeable commands** are important for slider drags — when the user drags a slider for "emission rate", dozens of intermediate values are generated. Using `canMergeWith()`, consecutive changes to the same property merge into a single undo step. Vestige already supports this pattern.

### Curve/Gradient Undo

Curve and gradient edits are more complex:
- **Curve edits**: Store the entire curve state (all keyframes) as old/new values. Curves are small (typically 4-20 keyframes), so copying the whole curve is cheap.
- **Gradient edits**: Store the entire gradient state (all color stops) as old/new values. Same rationale.

### What Other Engines Do

- **Unity**: Uses a transaction-based system where operations are grouped into single undoable units. `Undo.RecordObject()` snapshots the object state before modification.
- **Unreal Engine 5**: Transaction-based system grouping multiple operations into one undoable unit. Properties are tracked at the UObject level.
- **Valve Particle Editor**: Commands include new, delete, copy, deep copy, deep delete, and modify property — each as a separate command type.
- **Wolfire Games (Overgrowth)**: Stores full scene snapshots for undo — simple but memory-expensive. Not recommended for particle systems with large configurations.

### Recommendation for Vestige

Use property-level commands consistent with the existing `EntityPropertyCommand` pattern:
- One command type per property value type (float, vec3, color, curve, gradient, enum).
- Merge consecutive edits to the same property during slider drags.
- Store old+new value pairs, not full snapshots.
- Curve and gradient changes store the complete curve/gradient as the old and new values.

**Sources:**
- [Valve Particle Editor (Valve Developer Community)](https://developer.valvesoftware.com/wiki/Particle_Editor)
- [Custom Editor Undo/Redo System (GameDev.net)](https://gamedev.net/forums/topic/678496-custom-editor-undoredo-system/)
- [Command Pattern for Undo/Redo (Medium)](https://medium.com/@xainulabideen600/undo-redo-in-game-programming-using-command-pattern-d49eba152ca3)
- [How We Implement Undo (Wolfire Games)](http://blog.wolfire.com/2009/02/how-we-implement-undo/)
- [Mastering Undo/Redo in Level Design (Wayline)](https://www.wayline.io/blog/undo-redo-level-design)
- [Command Plugin for UE Undo/Redo (GitHub)](https://github.com/DaneIsAlive/CommandPlugin)
- [Undo Feature in Game Editor (Playgama)](https://playgama.com/blog/unity/how-can-i-implement-an-undo-feature-in-my-games-level-editor-similar-to-the-ctrl-z-function-in-software-applications/)
- [Command Pattern in Game Engines (MomentsLog)](https://www.momentslog.com/development/design-pattern/command-pattern-in-game-engines-input-handling)

---

## 8. Particle Serialization Formats

### Vestige Context

Vestige already uses **nlohmann/json** for entity serialization (see `entity_serializer.cpp`). The particle system should follow the same pattern for consistency.

### What to Serialize

Only the **emitter configuration** is serialized — not the runtime simulation state. When a scene is loaded, the particle system starts fresh.

### Proposed JSON Structure (Based on Research)

Drawing from the Simulant Engine's `.kglp` format and common patterns across engines:

```json
{
    "type": "ParticleEmitterComponent",
    "emitter": {
        "maxParticles": 1000,
        "duration": 5.0,
        "looping": true,
        "simulationSpace": "local",
        "emission": {
            "rateOverTime": 50.0,
            "bursts": [
                { "time": 0.0, "count": 20, "cycles": 1 }
            ]
        },
        "shape": {
            "type": "cone",
            "angle": 25.0,
            "radius": 0.5
        },
        "startLifetime": { "mode": "randomBetween", "min": 1.0, "max": 3.0 },
        "startSpeed": { "mode": "constant", "value": 5.0 },
        "startSize": { "mode": "constant", "value": 0.2 },
        "startColor": { "mode": "constant", "value": [1.0, 0.8, 0.3, 1.0] },
        "gravityModifier": -9.81
    },
    "modifiers": {
        "colorOverLifetime": {
            "enabled": true,
            "gradient": {
                "stops": [
                    { "position": 0.0, "color": [1.0, 1.0, 0.5, 1.0] },
                    { "position": 0.5, "color": [1.0, 0.3, 0.0, 0.8] },
                    { "position": 1.0, "color": [0.2, 0.0, 0.0, 0.0] }
                ]
            }
        },
        "sizeOverLifetime": {
            "enabled": true,
            "curve": {
                "keyframes": [
                    { "time": 0.0, "value": 0.5 },
                    { "time": 0.3, "value": 1.0 },
                    { "time": 1.0, "value": 0.0 }
                ]
            }
        },
        "velocityOverLifetime": {
            "enabled": false,
            "value": [0.0, 0.0, 0.0]
        }
    },
    "renderer": {
        "blendMode": "additive",
        "texture": "assets/textures/particles/smoke.png",
        "sortMode": "byDistance",
        "softParticles": true,
        "softDistance": 0.5
    }
}
```

### Key Design Decisions

1. **Parameter modes** (`mode` field): "constant", "randomBetween", "curve", "randomBetweenCurves". This mirrors Unity's approach and allows the same parameter to be simple or complex.

2. **Curves as keyframe arrays**: Simple linear interpolation between keyframes. Bezier handles can be added later with `inTangent`/`outTangent` fields per keyframe.

3. **Gradients as color-stop arrays**: Position [0,1] with RGBA color values. Linear interpolation between stops.

4. **Relative paths for textures**: Consistent with Vestige's existing resource system.

### Other Format Approaches

- **Simulant Engine (.kglp)**: JSON with quota, particle_width/height, emitters array (type, direction, velocity, angle, ttl_min/max, emission_rate, duration, repeat_delay), and manipulators array (type + type-specific params).
- **Godot (.tres resource)**: Text-based key=value format with embedded sub-resources for curves and gradients.
- **Binary formats**: Some engines serialize to binary for faster loading. For Vestige, JSON is fine — particle configs are small (a few KB at most). A binary format can be added as an optimization later if needed.

**Sources:**
- [Simulant Particle System Format (.kglp)](https://simulant-engine.appspot.com/docs/particle_system_format.md)
- [Serialization For Games (Gabriel's Virtual Tavern)](https://jorenjoestar.github.io/post/serialization_for_games/)
- [Serialization with JSON (Build a Game Engine)](https://buildagameengine.com/serialization/serialization-with-json)
- [Demystifying Game Persistence with Serialization](https://michaelbitzos.com/devblog/demystifying-game-persistence)
- [Scene Serialization and Deserialization (GameDev.net)](https://www.gamedev.net/forums/topic/713564-scene-serialization-and-deserialization/)

---

## 9. Recommendations for Vestige

Based on all research, here is the recommended approach for Vestige's particle system, tailored to the existing architecture:

### Architecture

1. **Component-based**: Create a `ParticleEmitterComponent` that attaches to entities, consistent with the existing `MeshRenderer`, `LightComponent` pattern.

2. **SoA data layout** with pre-allocated arrays and swap-on-death compaction. Pre-allocate to `maxParticles` on component creation.

3. **Generator/Updater pattern** (from C++ Stories): Separate concerns into small, composable units. Each "over lifetime" module becomes an updater. Each emission shape becomes a generator.

4. **CPU-based simulation** for the first implementation. The update loop iterates SoA arrays. Design the interface so a GPU compute backend could replace the CPU updaters later.

### Rendering

5. **Instanced billboard rendering** with a single draw call per particle system. Use a static quad VBO + per-instance position/size/color buffers with `glDrawArraysInstanced()`.

6. **Buffer orphaning** (`glBufferData` + `glBufferSubData`) for streaming particle data to GPU each frame.

7. **Additive blending** as the default (no sorting needed). Support alpha blending with camera-distance sorting as an option.

8. **Soft particles** using the existing depth buffer — fade alpha near depth intersections.

9. **Disable depth writes** (`glDepthMask(GL_FALSE)`) during particle rendering; keep depth testing on.

### Editor UI (ImGui)

10. **Module-based inspector** like Unity: collapsible sections for Emission, Shape, Start Properties, Color Over Lifetime, Size Over Lifetime, Force Over Lifetime, Renderer.

11. **Curve editor widget**: Use the ImGui Bezier widget (public domain, from ocornut/imgui#786) for curves. Start with a simple multi-keyframe linear interpolation editor.

12. **Gradient editor widget**: Use the `ImGradient` widget (from galloscript gist) for color-over-lifetime gradients. Supports add/remove/drag color stops with a color picker.

13. **Play/Pause/Restart** controls in the inspector panel for previewing particle effects.

14. **Real-time inline preview** in the main viewport. No separate preview FBO needed for particles (unlike materials, which benefit from isolated preview).

### Undo/Redo

15. **Property-level commands** extending the existing `EditorCommand` pattern. One command per property change.

16. **Merge consecutive slider drags** using `canMergeWith()` — already supported.

17. **Curve/gradient changes** store full old/new state (they are small data structures).

### Serialization

18. **JSON via nlohmann/json**, consistent with existing `EntitySerializer`. Serialize emitter configuration only — not runtime state.

19. **Parameter mode encoding**: Each parameter stores its mode ("constant", "randomBetween", "curve") alongside its value.

### Phased Implementation Suggestion

- **Phase 1**: ParticleData (SoA container), ParticleEmitterComponent, basic CPU emitter (point shape, constant rate), billboard renderer with instancing, additive blending.
- **Phase 2**: Emission shapes (sphere, cone, box), start property ranges, gravity.
- **Phase 3**: Over-lifetime modifiers (color gradient, size curve), curve/gradient ImGui widgets.
- **Phase 4**: Editor integration (inspector panel, undo/redo, serialization, play/pause/restart).
- **Phase 5**: Polish (soft particles, alpha blend + sorting, texture atlas animation, sub-emitters).

### ImGui Widget Resources

- **Bezier/Curve Editor**: [ImGui Bezier Widget (ocornut/imgui#786)](https://github.com/ocornut/imgui/issues/786) — public domain, single-function widget.
- **Multi-keyframe Curve Editor**: [ImGui Bezier Widget repo (TuTheWeeb)](https://github.com/TuTheWeeb/ImGui-Bezier-Widget)
- **Color Gradient Editor**: [ImGradient (galloscript gist)](https://gist.github.com/galloscript/8a5d179e432e062550972afcd1ecf112) — add/remove/drag color stops.
- **Extended Gradient Library**: [imgui_gradient (Coollab-Art)](https://github.com/Coollab-Art/imgui_gradient) — more feature-rich gradient widget.
- **Node Graph (future)**: [ImGui Node Graph (Guillaume Boisse)](https://gboisse.github.io/posts/node-graph/) — for potential future node-based VFX editing.
- **ImGui Useful Extensions Wiki**: [ocornut/imgui Wiki](https://github.com/ocornut/imgui/wiki/Useful-Extensions) — comprehensive list of community widgets.

---

## 10. All Sources

### Major Engine Documentation
- [Unity Particle System Modules](https://docs.unity3d.com/Manual/ParticleSystemModules.html)
- [Unity Main Module](https://docs.unity.cn/2021.1/Documentation/Manual/PartSysMainModule.html)
- [Unity Shape Module](https://docs.unity3d.com/Manual/PartSysShapeModule.html)
- [Unity Emissions and Emitters](https://docs.unity3d.com/6000.3/Documentation/Manual/particle-emissions-emitters.html)
- [Unity Curve Editor](https://docs.unity3d.com/410/Documentation/Manual/ParticleSystemCurveEditor.html)
- [Niagara Emitter Reference (UE4)](https://docs.unrealengine.com/4.27/en-US/RenderingAndGraphics/Niagara/EmitterReference)
- [Niagara Particle Update Group (UE5)](https://dev.epicgames.com/documentation/en-us/unreal-engine/particle-update-group-reference-for-niagara-effects-in-unreal-engine)
- [Niagara Editor UI Reference](https://docs.unrealengine.com/4.26/en-US/RenderingAndGraphics/Niagara/EmitterEditorReference)
- [Creating Particle Systems: Unreal vs Unity](https://dev.epicgames.com/community/learning/tutorials/dae5/unreal-engine-creating-particle-systems-in-unreal-vs-unity)
- [Mastering Particle Systems in UE](https://sdlccorp.com/post/mastering-particle-systems-in-unreal-engine/)
- [Godot GPUParticles3D](https://docs.godotengine.org/en/stable/classes/class_gpuparticles3d.html)
- [Godot Particle Systems 3D](https://docs.godotengine.org/en/stable/tutorials/3d/particles/index.html)
- [Godot State of Particles](https://godotengine.org/article/progress-report-state-of-particles/)
- [PopcornFX O3DE Integration](https://www.popcornfx.com/popcornfx-o3de-integration/)
- [PopcornFX O3DE Plugin (GitHub)](https://github.com/PopcornFX/O3DEPopcornFXPlugin)
- [Valve Particle Editor](https://developer.valvesoftware.com/wiki/Particle_Editor)

### OpenGL Particle Rendering
- [OpenGL Tutorial - Billboards](http://www.opengl-tutorial.org/intermediate-tutorials/billboards-particles/billboards/)
- [OpenGL Tutorial - Particles/Instancing](http://www.opengl-tutorial.org/intermediate-tutorials/billboards-particles/particles-instancing/)
- [LearnOpenGL - Particles](https://learnopengl.com/In-Practice/2D-Game/Particles)
- [Megabyte Softworks - Particle System (OpenGL 3)](https://www.mbsoftworks.sk/tutorials/opengl3/23-particle-system/)
- [Megabyte Softworks - Transform Feedback Particles (OpenGL 4)](https://www.mbsoftworks.sk/tutorials/opengl4/025-transform-feedback-particle-system/)
- [OGLdev Tutorial 28 - Transform Feedback](https://www.ogldev.org/www/tutorial28/tutorial28.html)
- [Open.gl - Transform Feedback](https://open.gl/feedback)
- [Transform Feedback Demo (GitHub)](https://github.com/zhangxiaomu01/TransformFeedback_openGL)

### GPU Particle Systems
- [GPU-Based Particle Simulation (Wicked Engine)](https://wickedengine.net/2017/11/gpu-based-particle-simulation/)
- [OpenGL Particle Systems (Vercidium)](https://vercidium.com/blog/opengl-particle-systems/)
- [2M GPU Particles (GitHub)](https://github.com/Crisspl/GPU-particle-system)
- [GPU Particles DirectX 11 (GitHub)](https://github.com/Brian-Jiang/GPUParticles)
- [Compute Particles Sample (NVIDIA)](https://docs.nvidia.com/gameworks/content/gameworkslibrary/graphicssamples/opengl_samples/computeparticlessample.htm)
- [NVIDIA Feedback Particles](https://archive.docs.nvidia.com/gameworks/content/gameworkslibrary/graphicssamples/opengl_samples/feedbackparticlessample.htm)
- [OpenGL SSBO Introduction (Geeks3D)](https://www.geeks3d.com/20140704/tutorial-introduction-to-opengl-4-3-shader-storage-buffers-objects-ssbo-demo/)
- [Compute Shaders + SSBO Particles Demo (Geeks3D)](https://www.geeks3d.com/hacklab/20200117/demo-opengl-4-3-compute-shaders-particles-ssbo/)
- [GPU Particle System OpenGL (GitHub)](https://github.com/Jax922/gpu-particle-system)
- [OpenGL GPU Particle Acceleration (GitHub)](https://github.com/MauriceGit/Partikel_accelleration_on_GPU)
- [OpenGL Rain - Compute + Geometry Shader (Codeberg)](https://codeberg.org/matiaslavik/OpenGLRain)
- [Little Grasshopper - OpenGL Particles](https://prideout.net/blog/old/blog/index.html@tag=opengl-particles.html)

### C++ Particle System Architecture
- [Flexible Particle System - Start (C++ Stories)](https://www.cppstories.com/2014/04/flexible-particle-system-start/)
- [Flexible Particle System - Container (C++ Stories)](https://www.cppstories.com/2014/04/flexible-particle-system-container/)
- [Flexible Particle System - OpenGL Renderer (C++ Stories)](https://www.cppstories.com/2014/07/flexible-particle-system-opengl-renderer/)
- [Flexible Particle System - Renderer Optimization (C++ Stories)](https://www.cppstories.com/2015/03/flexible-particle-system-renderer/)
- [Building an Advanced Particle System (Gamedeveloper.com)](https://www.gamedeveloper.com/programming/building-an-advanced-particle-system)
- [Designing an Extensible Particle System (GameDev.net Archive)](https://archive.gamedev.net/archive/reference/programming/features/extpart/index.html)
- [Particle Systems From the Ground Up](http://buildnewgames.com/particle-systems/)
- [2D Particle System C++ SDL (GitHub)](https://github.com/nintervik/2D-Particle-System)

### Data Structures and Memory
- [Object Pool Pattern (Game Programming Patterns)](https://gameprogrammingpatterns.com/object-pool.html)
- [Pool Allocator for Games (Gamedeveloper.com)](https://www.gamedeveloper.com/programming/designing-and-implementing-a-pool-allocator-data-structure-for-memory-management-in-games)
- [Pool Allocator (Medium)](https://medium.com/@mateusgondimlima/designing-and-implementing-a-pool-allocator-data-structure-for-memory-management-in-games-c78ed0902b69)
- [Particle System Memory Allocation (Gillius)](https://gillius.org/articles/partmem.htm)
- [SoA vs AoS Deep Dive](https://azad2171.github.io/soa_vs_aos_memory_layout_cpp/)
- [SoA vs AoS (Medium)](https://medium.com/@azad217/structure-of-arrays-soa-vs-array-of-structures-aos-in-c-a-deep-dive-into-cache-optimized-13847588232e)
- [C++ Cache Locality for Game Developers 2025](https://markaicode.com/cpp-cache-locality-optimization-game-developers-2025/)
- [Particle System Data Structure (GameDev.net)](https://www.gamedev.net/forums/topic/117884-particle-system-data-structure/)
- [Memory Management for C++ Game Engines](https://palospublishing.com/memory-management-techniques-for-real-time-game-engines-in-c/)

### Transparency and Blending
- [NVIDIA Soft Particles Whitepaper](https://developer.download.nvidia.com/whitepapers/2007/SDK10/SoftParticles_hi.pdf)
- [Soft Particles in WebGL/OpenGL ES](https://dev.to/keaukraine/implementing-soft-particles-in-webgl-and-opengl-es-3l6e)
- [OpenGL Soft Particles (Alessandro Ribeiro)](http://alessandroribeiro.thegeneralsolution.com/en/2021/01/09/openglstarter-soft-particles/)
- [High-Speed Off-Screen Particles (GPU Gems 3)](https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-23-high-speed-screen-particles)
- [Best Blending Mode for Particles (Khronos)](https://community.khronos.org/t/best-blending-mode-for-particles/14987)
- [Particle Depth and Blend (Khronos)](https://community.khronos.org/t/particle-system-depth-and-blend/19564)
- [Soft Particles in OpenMW (GitLab)](https://gitlab.com/OpenMW/openmw/-/merge_requests/980)
- [Particle Shading (Jordan Stevens)](https://www.jordanstevenstechart.com/particle-shading)

### Undo/Redo
- [Custom Editor Undo/Redo (GameDev.net)](https://gamedev.net/forums/topic/678496-custom-editor-undoredo-system/)
- [Command Pattern for Undo/Redo (Medium)](https://medium.com/@xainulabideen600/undo-redo-in-game-programming-using-command-pattern-d49eba152ca3)
- [How We Implement Undo (Wolfire Games)](http://blog.wolfire.com/2009/02/how-we-implement-undo/)
- [Undo/Redo in Level Design (Wayline)](https://www.wayline.io/blog/undo-redo-level-design)
- [UE Command Plugin (GitHub)](https://github.com/DaneIsAlive/CommandPlugin)
- [Undo in Game Editor (Playgama)](https://playgama.com/blog/unity/how-can-i-implement-an-undo-feature-in-my-games-level-editor-similar-to-the-ctrl-z-function-in-software-applications/)

### Serialization
- [Simulant Particle System Format](https://simulant-engine.appspot.com/docs/particle_system_format.md)
- [Serialization For Games](https://jorenjoestar.github.io/post/serialization_for_games/)
- [Serialization with JSON (Build a Game Engine)](https://buildagameengine.com/serialization/serialization-with-json)
- [Demystifying Game Persistence](https://michaelbitzos.com/devblog/demystifying-game-persistence)
- [Scene Serialization (GameDev.net)](https://www.gamedev.net/forums/topic/713564-scene-serialization-and-deserialization/)

### ImGui Widgets for Particle Editors
- [ImGui Bezier Widget (Issue #786)](https://github.com/ocornut/imgui/issues/786)
- [ImGui Bezier Widget Repo](https://github.com/TuTheWeeb/ImGui-Bezier-Widget)
- [ImGradient Color Editor (Gist)](https://gist.github.com/galloscript/8a5d179e432e062550972afcd1ecf112)
- [imgui_gradient Library (Coollab-Art)](https://github.com/Coollab-Art/imgui_gradient)
- [ImGui Node Graph (Guillaume Boisse)](https://gboisse.github.io/posts/node-graph/)
- [ImGui Useful Extensions Wiki](https://github.com/ocornut/imgui/wiki/Useful-Extensions)
- [ImGui Interpolation Widget (Issue #55)](https://github.com/ocornut/imgui/issues/55)

### ECS and Particle Systems
- [ECS Architecture Guide](https://columbaengine.org/blog/ecs-architecture-with-ecs/)
- [Understanding ECS in C++ (Medium)](https://medium.com/respawn-point/understanding-modern-game-engine-architecture-with-ecs-7b3051dbccdb)
- [Simple ECS in C++ (Austin Morlan)](https://austinmorlan.com/posts/entity_component_system/)
- [ECS FAQ (GitHub)](https://github.com/SanderMertens/ecs-faq)

### Complete Example Projects
- [ParticleSystem C++ OpenGL ImGui (GitHub)](https://github.com/Im-Rises/ParticleSystem)
- [ParticleSimulator SDL+OpenGL+ImGui (GitHub)](https://github.com/alexandra-zaharia/ParticleSimulator)
- [ImGui Particles (GitHub)](https://github.com/SandFoxy/imgui_particles)
- [Basic Particle System C++ ImGui SFML (GitHub)](https://github.com/terroo/particle-system)
- [GemParticles OpenGL (GitHub)](https://github.com/frtru/GemParticles)
- [VFX Editor using ImGui (Nicolas Risberg)](https://nicolas-risberg.github.io/2021-03-30/vfx-editor.html)
