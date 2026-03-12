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
