# Phase 9C Design: New Domain Systems (Audio, UI/HUD, AI/Navigation)

## Overview

Phase 9C creates three new domain systems for capabilities that don't yet exist in the engine.
Each system follows the `ISystem` / `SystemRegistry` infrastructure from Phase 9A and the
ownership patterns established in Phase 9B.

**Scope:** This phase builds the foundation — library integration, basic APIs, component types.
Advanced features (reverb zones, audio occlusion, menu system, behavior trees) are deferred
to later phases. Per the roadmap: "This phase implements the Audio domain system wrapper;
Phase 10 details the full feature set."

## Research Summary

### Audio Library: OpenAL Soft

**Chosen: OpenAL Soft.** Mature, open-source spatial audio library with HRTF and effects
extensions. LGPL license is well-established for commercial game distribution.

| Criterion | OpenAL Soft | miniaudio | FMOD |
|-----------|-------------|-----------|------|
| License | LGPL 2.0+ | Public Domain / MIT | Proprietary |
| Spatial Audio | HRTF, EFX effects | Basic positional | Advanced (plugins) |
| Reverb/Occlusion | Yes (AL_EXT_EFX) | Not yet | Yes |
| CMake Support | Native | N/A (single header) | No |
| Format Decoding | External decoder needed | Built-in | Built-in |

**Why OpenAL Soft over miniaudio:** miniaudio is simpler but lacks HRTF and the EFX effects
framework needed for reverb zones and audio occlusion (Phase 10 requirements). OpenAL Soft
provides these natively, avoiding future reimplementation.

**Why not FMOD:** Proprietary license with revenue-based tiers conflicts with the project's
open-source preference. Overkill for current needs.

**Decoding:** dr_libs (dr_wav, dr_mp3, dr_flac) + stb_vorbis for OGG. All single-header,
public domain. Same vendoring pattern as existing stb_image.

**Sources:**
- [OpenAL Soft GitHub](https://github.com/kcat/openal-soft)
- [OpenAL Soft HRTF Extension](https://openal-soft.org/openal-extensions/SOFT_HRTF.txt)
- [miniaudio Official](https://miniaud.io/)
- [FMOD Licensing](https://www.fmod.com/licensing)
- [dr_libs GitHub](https://github.com/mackron/dr_libs)

### UI Framework: Custom OpenGL + Existing TextRenderer

**Chosen: Custom sprite batch renderer.** The engine already has FreeType 2.13.3, Font class,
and TextRenderer with glyph atlasing and both 2D/3D text rendering. Building on this
infrastructure is simpler and more performant than adding an external UI framework.

**Separation from editor:** Dear ImGui remains the editor UI (EDIT mode). The custom UI system
renders in-game overlays (PLAY mode) — HUD elements, menus, floating text.

**Why not RmlUi:** Heavy dependency (HTML/CSS parsing) for something achievable with existing
infrastructure. Better suited as a future upgrade if complex UI theming is needed.

**Why not Nuklear:** Immediate-mode like ImGui, not designed for polished player-facing UI.
Limited skinning and animation support.

**Sources:**
- [LearnOpenGL - Text Rendering](https://learnopengl.com/In-Practice/Text-Rendering)
- [LearnOpenGL - Sprite Rendering](https://learnopengl.com/In-Practice/2D-Game/Rendering-Sprites)
- [Godot UI System](https://docs.godotengine.org/en/stable/tutorials/ui/index.html)
- [RmlUi GitHub](https://github.com/mikke89/RmlUi)

### Navigation: Recast & Detour

**Chosen: Recast & Detour.** Industry-standard navmesh library used by Unreal Engine, Unity,
Godot, and O3DE. ZLib license (permissive, Steam-friendly).

**Components:**
- **Recast** — NavMesh generation: voxelizes triangle meshes, filters walkability, generates
  polygon navigation mesh
- **Detour** — Runtime pathfinding: A* queries on navmesh, nearest-point queries

**Scope:** Static navmesh baked from editor (not runtime). Dynamic navmesh and crowd
simulation deferred to later phases.

**Sources:**
- [Recast Navigation GitHub](https://github.com/recastnavigation/recastnavigation)
- [Recast Navigation Documentation](https://recastnav.com/)
- [O3DE Recast Integration Guide](https://docs.o3de.org/docs/user-guide/interactivity/navigation-and-pathfinding/recast-navigation/)

## Architecture

```
Engine
  |-- SystemRegistry
  |     |-- (9 existing systems from Phase 9B)
  |     |-- AudioSystem --> owns AudioEngine
  |     |-- UISystem --> owns SpriteBatchRenderer, UICanvas
  |     |-- NavigationSystem --> owns NavMeshBuilder, NavMeshQuery
  |
  |-- m_uiSystem* (cached pointer for render loop)
```

## Domain Systems

### Audio System
| Property | Value |
|----------|-------|
| System Name | "Audio" |
| Owns | AudioEngine (OpenAL wrapper) |
| isForceActive | false |
| Components | AudioSourceComponent |
| update() | Syncs listener to camera, manages source lifecycle |

**AudioEngine** wraps OpenAL:
- Device/context initialization with graceful failure (no audio hardware → warning, engine continues)
- Source pool (32 preallocated sources to avoid runtime allocation)
- Buffer cache (loaded AudioClips keyed by path)
- Listener position/orientation synced to camera each frame

**AudioClip** holds decoded PCM data:
- Loaded via dr_libs (WAV/MP3/FLAC) or stb_vorbis (OGG)
- Auto-detects format by file extension
- Stores sample rate, channels, bits per sample

**AudioSourceComponent** (entity component):
- clipPath, volume, pitch, minDistance, maxDistance, loop, autoPlay, spatial
- OpenAL source/buffer handles managed by AudioSystem (not component directly)

### UI/HUD System
| Property | Value |
|----------|-------|
| System Name | "UI" |
| Owns | SpriteBatchRenderer, UICanvas |
| isForceActive | false |
| Components | None (screen overlay, not entity-component based) |
| update() | Hit testing, input routing, signal dispatch |

**SpriteBatchRenderer:**
- Batched 2D quad rendering with orthographic projection
- Pre-allocated VBO (1000 quads per batch)
- Single draw call per texture group
- Depth testing disabled

**UIElement** (base class):
- Position (relative to anchor), size, anchor (9-position enum), visible, interactive
- Virtual render() and hitTest()
- Signal-based callbacks (onClick, onHover)
- Children vector for hierarchy

**Concrete elements:** UILabel (text via TextRenderer), UIImage (textured quad), UIPanel (background rect)

**UICanvas:**
- Screen-space root container
- Element tree ownership
- Hit testing and signal dispatch

**Render order:** 3D scene → post-processing → blitToScreen → **UI overlay** → ImGui editor

### Navigation System
| Property | Value |
|----------|-------|
| System Name | "Navigation" |
| Owns | NavMeshBuilder, NavMeshQuery |
| isForceActive | false |
| Components | NavAgentComponent |
| update() | Advances agents along their paths |

**NavMeshBuilder:**
- Collects triangle geometry from scene entities (via glGetBufferSubData)
- Transforms vertices by world matrix
- Feeds to Recast voxelization pipeline
- Editor-triggered bake (not runtime)

**NavMeshQuery:**
- Wraps dtNavMeshQuery for A* pathfinding
- findPath(start, end) → vector of waypoints
- findNearestPoint(point) → snapped navmesh position

**NavAgentComponent** (entity component):
- radius, height, maxSpeed, currentPath, pathIndex

**NavMeshBuildConfig:**
- cellSize, cellHeight, agentHeight, agentRadius, agentMaxClimb, agentMaxSlope
- regionMinSize, regionMergeSize, edgeMaxLen, edgeMaxError, vertsPerPoly

## Engine Changes

### engine.h
- Add forward declaration: `class UISystem;`
- Add cached pointer: `UISystem* m_uiSystem = nullptr;`

### engine.cpp
- Register 3 new systems after LightingSystem (order: Audio, UI, Navigation)
- Cache UISystem pointer for render loop
- Add UI render call in play-mode section (after blitToScreen, before editor drawPanels)

### external/CMakeLists.txt
- OpenAL Soft via FetchContent (1.24.1)
- recastnavigation via FetchContent (v1.6.0)

### engine/CMakeLists.txt
- Add all new source files to ENGINE_SOURCES
- Add dr_libs include directory
- Link OpenAL, Recast, Detour

### system_events.h
- AudioPlayEvent (clipPath, position, volume)
- NavMeshBakedEvent (polyCount, bakeTimeMs)

## New Files Summary

| File | System |
|------|--------|
| engine/audio/audio_engine.h/cpp | Audio |
| engine/audio/audio_clip.h/cpp | Audio |
| engine/audio/audio_source_component.h/cpp | Audio |
| engine/systems/audio_system.h/cpp | Audio |
| engine/ui/sprite_batch_renderer.h/cpp | UI |
| engine/ui/ui_element.h/cpp | UI |
| engine/ui/ui_canvas.h/cpp | UI |
| engine/ui/ui_label.h/cpp | UI |
| engine/ui/ui_image.h/cpp | UI |
| engine/ui/ui_panel.h/cpp | UI |
| engine/ui/ui_signal.h | UI |
| engine/systems/ui_system.h/cpp | UI |
| engine/navigation/nav_mesh_builder.h/cpp | Navigation |
| engine/navigation/nav_mesh_query.h/cpp | Navigation |
| engine/navigation/nav_mesh_config.h | Navigation |
| engine/navigation/nav_agent_component.h/cpp | Navigation |
| engine/systems/navigation_system.h/cpp | Navigation |
| assets/shaders/ui_sprite.vert | UI |
| assets/shaders/ui_sprite.frag | UI |
| external/dr_libs/dr_wav.h | Audio (vendored) |
| external/dr_libs/dr_mp3.h | Audio (vendored) |
| external/dr_libs/dr_flac.h | Audio (vendored) |
| external/dr_libs/dr_libs_impl.cpp | Audio (vendored) |
| external/stb/stb_vorbis.c | Audio (vendored) |
| external/stb/stb_vorbis_impl.cpp | Audio (vendored) |

## Test Plan

### Domain System Tests (test_domain_systems.cpp updates)
- Name, force-active, component ownership tests for all 3 new systems
- Update AllSystemsHaveUniqueNames count: 9 → 12
- Update AllSystemsInheritFromISystem: add 3 entries

### System-Specific Tests
- AudioClip format detection (channels/bits → AL format)
- UISignal connect/emit/callback
- UIElement hit testing (point-in-rect)
- NavMeshBuildConfig default values
- NavigationSystem::hasNavMesh() initially false

## Key Design Decisions

1. **Audio fails gracefully** — no audio device → engine continues with warning
2. **UI renders in PLAY mode only** — ImGui handles EDIT mode
3. **NavMesh is editor-baked** — no runtime generation (too expensive for 60 FPS)
4. **Mesh vertex readback via GPU** — glGetBufferSubData for navmesh bake (editor-only)
5. **No cached pointers for Audio/Nav** — no hot-path render loop usage
6. **UISystem gets cached pointer** — called explicitly in render loop
