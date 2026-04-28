# Phase 5C: Material Editor, Asset Browser & Light Widgets — Design Document

## Goal

Upgrade the editor's content creation tooling with three interconnected systems: an enhanced material editor with texture slot thumbnails and a preview sphere, an asset browser panel for navigating and dragging assets into the scene, and improved light editing widgets with viewport gizmos and artist-friendly controls.

**Milestone:** A working content pipeline where the user can browse assets, assign textures to materials via drag-and-drop, preview materials in real-time, and tune lights with intuitive range/color controls and in-viewport gizmos.

---

## Current State (End of Phase 5B)

The editor now has:
- **Inspector panel** with material editing (PBR + Blinn-Phong properties, texture slot indicators, transparency)
- **Hierarchy panel** with tree view, drag-reparent, rename, visibility/lock, context menu
- **Import dialog** with file browser for glTF/GLB/OBJ models
- **Prefab system** with save/load from `assets/prefabs/`
- **Create menu** with primitives (cube, sphere, plane, cylinder, cone, wedge), lights, prefabs
- **Transform gizmos** via ImGuizmo (Translate/Rotate/Scale)
- **Selection system** with single/multi-select, viewport picking via ID buffer

**What's missing:**
- Texture slots in the inspector show only "loaded" / "(none)" — no thumbnails, no browse/clear buttons
- No material preview sphere — users cannot see material changes without looking at the viewport
- No asset browser — users cannot browse project files or drag textures/models
- Light editing uses raw attenuation coefficients (constant/linear/quadratic) — not artist-friendly
- No viewport gizmos for lights — no visual feedback for light range, cone angle, or direction
- No debug draw system for wireframe overlays

---

## Research Summary

Online research across game engines (Unity, Unreal, Godot, Blender), open-source ImGui engines (Hazel, LumixEngine, Razix), and graphics programming resources informed the following design decisions. Full sources are cited inline.

### Material Editor UI (Unity, Godot, Blender, Hazel Engine)

- **Universal pattern:** Collapsible sections where each texture sits **inline** with its scalar/color property — not in a separate "Textures" section. Albedo texture next to albedo color, normal map next to normal settings, etc. (Unity Standard Shader Inspector, Godot StandardMaterial3D)
- **Texture slot widget:** Small thumbnail (48-64px), "Browse" button, "X" clear button, drag-drop target. `ImGui::Image()` with `texture->getId()` cast to `ImTextureID`. UV flip required for OpenGL: `ImVec2(0, 1), ImVec2(1, 0)`. (ImGui Wiki: Image Loading and Displaying Examples)
- **Color space:** Store colors in linear space, convert to sRGB for display in `ImGui::ColorEdit3`, convert back on edit. `pow(color, 1/2.2)` for display, `pow(color, 2.2)` for storage. (ImGui GitHub Issue #578, #1724)
- **HDR colors:** Use `ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float` for emissive colors. (ImGui ColorEdit flags reference)
- **Material preview:** Render a sphere with the material to a small FBO (128x128), display via `ImGui::Image()`. Fixed camera at `(0, 0, 2.5)` looking at origin, single directional light from upper-right. Only re-render when material properties change (dirty flag). (LearnOpenGL Framebuffers, codingwiththomas.com)
- **ImageButton API:** ImGui docking branch requires `const char* str_id` as first parameter (deprecated old signature in 1.89). (ImGui changelog)

### Asset Browser (Unity Project Window, Unreal Content Browser, LumixEngine)

- **Layout:** Left panel = folder tree, right panel = thumbnail grid or list view. Breadcrumb path navigation, search/filter bar at top. (Unity Project Browser Manual, Unreal Content Browser)
- **Grid layout:** Manual cursor positioning with `ImGui::SameLine()` for wrapping. Compute columns from `GetContentRegionAvail().x / cellSize`. Use `ImGuiListClipper` for virtual scrolling in large directories. (LumixEngine asset_browser.cpp, ImGui GitHub Issue #428)
- **Thumbnail sizes:** Texture thumbnails load the image directly (no FBO). Mesh/material thumbnails use FBO rendering. Static icons for non-visual assets (audio, scripts, shaders). (Unreal ThumbnailRendering API)
- **Thumbnail caching:** Two-tier: in-memory LRU cache (GPU textures) + disk cache (PNG files in `.vestige/thumbnails/`). Invalidation via `std::filesystem::last_write_time` comparison. Generate 1-2 thumbnails per frame to avoid stalls. (LumixEngine TileState pattern, Godot EditorResourcePreview)
- **Drag-and-drop:** `ImGui::BeginDragDropSource` on browser items, `ImGui::BeginDragDropTarget` on inspector texture slots. Payload type: `"ASSET_PATH"` containing the file path string. (ImGui Drag and Drop Demo #1931)
- **File watching:** Start with `std::filesystem` polling (1-second interval) — simple, cross-platform, negligible CPU cost. Upgrade to `inotify` or `efsw` library later if needed. (cpp17-filewatcher, efsw library)
- **Asset type detection:** Extension-based lookup table mapping `.png/.jpg/.tga` to TEXTURE, `.obj/.gltf/.glb` to MESH, `.json` to PREFAB, etc. (All engines use this approach)

### Light Editing Widgets (Unity, Unreal, Godot, Hazel, Razix)

- **Range-based attenuation:** All engines expose a single "Range" float to artists, not raw constant/linear/quadratic coefficients. Auto-calculate coefficients from range. (Unity Light Inspector, Unreal Physical Lighting Units, Godot OmniLight3D)
- **Color temperature:** Kelvin slider (1000K-12000K) that tints the base light color. Tanner Helland algorithm for Kelvin-to-RGB conversion. Optional toggle — not all users want it. (Tanner Helland, Unreal "Use Temperature" toggle)
- **Attenuation curve:** Small plot widget (`ImGui::PlotLines` or custom DrawList rendering) showing falloff from 0 to range. Real-time update as properties change. (Unity, Unreal details panels)
- **Viewport gizmos:** Directional = arrows showing direction. Point = 3 wireframe circles (XY, XZ, YZ planes) at light range. Spot = cone wireframe (lines from apex to far circle). Color-coded to match light color. (Unity Gizmos API, all engines)
- **Debug draw system:** Immediate-mode line rendering with `GL_LINES`. Accumulate `DebugVertex {position, color}` into a dynamic VBO. Minimal shader (VP transform + passthrough color). Render as overlay with depth test on but depth write off. (glampert/debug-draw, sjb3d/imdd)
- **Shadow settings:** Expose existing `CascadedShadowConfig` and `PointShadowConfig` in the inspector under collapsible tree nodes. Resolution dropdown, cascade count, shadow distance, split lambda. (Unity HDRP Shadow Inspector, Godot shadow settings)
- **Logarithmic intensity slider:** `ImGuiSliderFlags_Logarithmic` for light intensity — essential for HDR values spanning orders of magnitude. (ImGui API)

---

## Architecture Overview

Phase 5C adds four new subsystems and enhances the existing inspector:

```
+-------------------------------------------------------------+
|                          Editor                               |
|                                                               |
|  +------------------+  +------------------+                   |
|  | AssetBrowserPanel|  | MaterialPreview  |                   |
|  | (folder tree,    |  | (FBO sphere      |                   |
|  |  thumbnail grid, |  |  rendering)      |                   |
|  |  drag-drop)      |  |                  |                   |
|  +------------------+  +------------------+                   |
|                                                               |
|  +------------------+  +------------------+                   |
|  | DebugDraw        |  | InspectorPanel   |                   |
|  | (line rendering, |  | (enhanced: tex   |                   |
|  |  light gizmos)   |  |  slots, light    |                   |
|  |                  |  |  range, curves)  |                   |
|  +------------------+  +------------------+                   |
|                                                               |
|  Existing: Hierarchy, Viewport, Console, Import, Prefabs      |
+-------------------------------------------------------------+
```

### Design Principles

1. **No new external dependencies** — All new functionality uses existing libraries (ImGui, stb_image, OpenGL 4.5) and existing engine infrastructure (Framebuffer, Mesh, Shader, ResourceManager).
2. **Texture thumbnails are the bridge** — The same texture thumbnail pattern serves both the asset browser grid and the inspector texture slots.
3. **Debug draw is foundational** — Light gizmos are the first use case, but the debug draw system also supports future physics debugging, nav mesh visualization, etc.
4. **Dirty-flag rendering** — Material preview and thumbnails only re-render when their inputs change, not every frame.
5. **Incremental loading** — Thumbnail generation is spread across frames (1-2 per frame) to maintain 60 FPS.

---

## Implementation Steps

### 5C-1: Debug Draw System

**Goal:** An immediate-mode line rendering system for drawing wireframe overlays in the viewport.

This is the foundation for light gizmos and future debug visualization (bounding boxes, physics shapes, nav meshes).

#### API

```cpp
/// @file debug_draw.h
/// @brief Immediate-mode debug line rendering for editor overlays.

class DebugDraw
{
public:
    /// @brief Initializes GPU resources (VAO, VBO, shader).
    void initialize(const std::string& assetPath);

    /// @brief Queues a line segment.
    static void line(const glm::vec3& from, const glm::vec3& to,
                     const glm::vec3& color);

    /// @brief Queues a circle (N line segments in a plane).
    static void circle(const glm::vec3& center, const glm::vec3& normal,
                       float radius, const glm::vec3& color, int segments = 32);

    /// @brief Queues 3 orthogonal great circles (XY, XZ, YZ planes).
    static void wireSphere(const glm::vec3& center, float radius,
                           const glm::vec3& color, int segments = 32);

    /// @brief Queues a cone wireframe (apex + far circle + ribs).
    static void cone(const glm::vec3& apex, const glm::vec3& direction,
                     float length, float angleDeg,
                     const glm::vec3& color, int ribs = 8);

    /// @brief Queues an arrow (line + arrowhead).
    static void arrow(const glm::vec3& from, const glm::vec3& to,
                      const glm::vec3& color, float headSize = 0.15f);

    /// @brief Renders all queued lines and clears the buffer.
    /// @param viewProjection Combined VP matrix from the camera.
    void flush(const glm::mat4& viewProjection);

    void cleanup();
};
```

#### Implementation Details

- **Vertex format:** `struct DebugVertex { glm::vec3 position; glm::vec3 color; }` — 24 bytes per vertex.
- **GPU resources:** Single VAO + dynamic VBO (pre-allocated for ~16K vertices, resized if needed). `GL_LINES` primitive mode.
- **Shader:** Minimal vertex shader (VP transform), fragment shader (passthrough color). No lighting, no textures.
- **Rendering:** Disable depth write (`glDepthMask(GL_FALSE)`), keep depth test on, set line width 2.0. This makes gizmos visible but occluded by scene geometry.
- **Static accumulation:** Lines are accumulated in a `static std::vector<DebugVertex>`. `flush()` uploads to GPU, draws, and clears.
- **Integration:** Called from `Editor::drawPanels()` after `renderScene()` but before `endFrame()`.

#### Shader (new files)

- `assets/shaders/debug_line.vert` — VP transform only
- `assets/shaders/debug_line.frag` — Passthrough color

**New files:**
- `engine/renderer/debug_draw.h` / `.cpp`
- `assets/shaders/debug_line.vert` / `.frag`

**Modified files:**
- `engine/editor/editor.h` / `.cpp` — Add `DebugDraw` member, call `flush()` in viewport rendering
- `engine/CMakeLists.txt` — Add `renderer/debug_draw.cpp`

---

### 5C-2: Light Viewport Gizmos

**Goal:** Draw wireframe gizmos in the viewport for selected light entities, showing direction, range, and cone angles.

#### Gizmo Types

| Light Type | Gizmo | Details |
|------------|-------|---------|
| Directional | 3 parallel arrows | From entity position in light direction, length ~2m |
| Point | 3-circle wireframe sphere | Radius = calculated effective range from attenuation |
| Spot | Cone wireframe | Lines from apex to far circle at outer angle; inner cone dimmer |

#### Range Calculation

Current `PointLight` and `SpotLight` store raw attenuation coefficients. To draw a range sphere, calculate the effective range where intensity drops below a threshold (1/256 ≈ 0.004):

```cpp
/// @brief Calculates the effective light range from attenuation coefficients.
/// Returns the distance where intensity drops to ~0.4% (1/256).
float calculateLightRange(float constant, float linear, float quadratic)
{
    // Solve: 1/(c + l*d + q*d^2) = 1/256
    // => q*d^2 + l*d + (c - 256) = 0
    // Quadratic formula: d = (-l + sqrt(l^2 - 4*q*(c-256))) / (2*q)
    float a = quadratic;
    float b = linear;
    float c = constant - 256.0f;
    float discriminant = b * b - 4.0f * a * c;
    if (discriminant < 0.0f || a <= 0.0f) return 50.0f; // fallback
    return (-b + std::sqrt(discriminant)) / (2.0f * a);
}
```

#### Drawing

In `Editor::drawPanels()`, after scene rendering, iterate selected entities with light components and call the appropriate DebugDraw method:

```cpp
// For each selected entity with a PointLightComponent:
float range = calculateLightRange(light.constant, light.linear, light.quadratic);
DebugDraw::wireSphere(entityWorldPos, range, light.diffuse);

// For each selected entity with a SpotLightComponent:
float outerAngleDeg = glm::degrees(std::acos(light.outerCutoff));
float range = calculateLightRange(light.constant, light.linear, light.quadratic);
DebugDraw::cone(entityWorldPos, light.direction, range, outerAngleDeg, light.diffuse);

// For each selected entity with a DirectionalLightComponent:
DebugDraw::arrow(entityWorldPos, entityWorldPos + light.direction * 2.0f, light.diffuse);
// Plus 2 parallel offset arrows
```

**Modified files:**
- `engine/editor/editor.cpp` — Add light gizmo drawing after scene render

---

### 5C-3: Enhanced Light Inspector

**Goal:** Replace raw attenuation coefficients with artist-friendly controls: range slider, color temperature, attenuation curve visualization, and shadow settings.

#### New Controls

**Range-based attenuation (Point + Spot lights):**
- Add a "Range" `DragFloat` (0.1 to 200.0, default 10.0) that auto-calculates attenuation coefficients.
- Keep an "Advanced" tree node with raw constant/linear/quadratic for power users.
- Range-to-attenuation formula (from LearnOpenGL attenuation table, interpolated):

```cpp
void setAttenuationFromRange(PointLight& light, float range)
{
    light.constant  = 1.0f;
    light.linear    = 4.5f / range;
    light.quadratic = 75.0f / (range * range);
}
```

**Color temperature (all light types):**
- Optional toggle: "Use Temperature" checkbox.
- When enabled: Kelvin slider (1000K-12000K, default 6500K) replaces the diffuse color picker. Temperature color multiplied with a base intensity.
- Tanner Helland Kelvin-to-RGB conversion function.
- Color temperature gradient bar rendered above the slider using `ImDrawList::AddRectFilled`.

**Attenuation curve (Point + Spot lights):**
- Small plot (full-width, 60px height) using `ImGui::PlotLines` or custom `ImDrawList` rendering.
- X axis: 0 to range, Y axis: 0.0 to 1.0 intensity.
- Updates in real-time as range/attenuation changes.

**Shadow settings (collapsible tree node):**
- Directional: Cascade count (1-4), resolution dropdown (512/1024/2048/4096), shadow distance, split lambda slider with tooltip.
- Point: Resolution dropdown (256/512/1024/2048), near/far plane.

**Logarithmic intensity:**
- Add `ImGuiSliderFlags_Logarithmic` to emissive light intensity and any future intensity sliders.

#### Inspector Layout Change

Replace the current 3-separate-color-picker pattern (ambient/diffuse/specular) with a simpler model for each light:

```
[Point Light]
  Color           [ColorEdit3]       <- replaces diffuse
  Intensity       [DragFloat, log]   <- multiplier for color
  Range           [DragFloat]        <- auto-calculates attenuation
  Attenuation     [PlotLines]        <- visual curve
  [v] Casts Shadow
    [Shadow Settings]                <- collapsible
      Resolution  [Combo]
      Near Plane  [DragFloat]
      Far Plane   [DragFloat]
  [Advanced Attenuation]             <- collapsible, closed by default
    Constant  [DragFloat]
    Linear    [DragFloat]
    Quadratic [DragFloat]
```

The ambient/specular colors are derived: `ambient = color * 0.1`, `specular = color`. This matches how most engines work — separate ambient/specular colors confuse artists and rarely need to differ from the diffuse.

**New files:**
- `engine/renderer/light_utils.h` — `calculateLightRange()`, `setAttenuationFromRange()`, `kelvinToRgb()`

**Modified files:**
- `engine/editor/panels/inspector_panel.h` / `.cpp` — Restructured light sections
- `engine/renderer/light.h` — Add `range` field to PointLight and SpotLight (cached, synced with attenuation)

---

### 5C-4: Material Editor Texture Slots

**Goal:** Replace text-only texture indicators with thumbnail previews, browse buttons, and drag-drop targets.

#### Texture Slot Widget

A reusable helper function drawn inline with each material property:

```
[Albedo]                          <- CollapsingHeader
  [48x48 thumbnail] Browse  X    <- texture slot
  Color: [ColorEdit3]             <- albedo color (tints texture)

[Normal & Height]                 <- CollapsingHeader
  Normal Map:
  [48x48 thumbnail] Browse  X
  Height Map:
  [48x48 thumbnail] Browse  X
  [x] Parallax Occlusion
  Height Scale: [SliderFloat]
  [x] Stochastic Tiling
```

#### Implementation

Static helper function in `inspector_panel.cpp`:

```cpp
static bool drawTextureSlot(const char* label,
                            const std::shared_ptr<Texture>& texture,
                            bool& wantBrowse, bool& wantClear)
```

- **Thumbnail:** `ImGui::Image()` with `texture->getId()` at 48x48, UV-flipped for OpenGL.
- **Placeholder:** When no texture, draw a dark rectangle with "None" text using `ImDrawList`.
- **Browse button:** Sets a flag; the editor opens a file browser filtered to image extensions (`.png`, `.jpg`, `.tga`, `.bmp`, `.hdr`). Reuses the existing `ImGui::FileBrowser` from imgui-filebrowser.
- **Clear button:** "X" button, only visible when texture is loaded. Clears the texture slot.
- **Drag-drop target:** `ImGui::BeginDragDropTarget()` accepting `"ASSET_PATH"` payloads from the asset browser. Loads the dropped texture via `ResourceManager::loadTexture()`.
- **Dimensions tooltip:** On hover, show `WxH` pixel dimensions.

#### File Browser Integration

Create a texture-specific file browser instance (separate from the import dialog's model browser):

```cpp
// In Editor or InspectorPanel
ImGui::FileBrowser m_textureBrowser;
m_textureBrowser.SetTitle("Select Texture");
m_textureBrowser.SetTypeFilters({".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr"});
```

Track which texture slot initiated the browse (enum: ALBEDO, NORMAL, HEIGHT, METALLIC_ROUGHNESS, EMISSIVE, AO) so the result can be assigned to the correct material property.

#### Color Space Awareness

When loading a texture via browse/drag-drop, the editor must know whether to load as linear or sRGB:
- **sRGB:** Albedo/diffuse, emissive
- **Linear:** Normal map, height map, metallic-roughness, AO

Map each slot type to its color space for the `loadTexture(path, linear)` call.

#### Inspector Section Restructuring

The current `drawMaterial()` flow is:
1. `drawMaterialBlinnPhong()` or `drawMaterialPbr()` — color/slider properties
2. `drawMaterialTextures()` — separate texture list
3. `drawMaterialTransparency()`

Replace with per-section grouping:
1. **Base Color** section — albedo/diffuse color + texture slot inline
2. **Surface** section (PBR only) — metallic + met-rough texture, roughness, AO + texture
3. **Clearcoat** section — clearcoat + CC roughness
4. **Emission** section — emissive color (HDR flags) + strength + texture
5. **Normal & Height** section — normal map slot, height map slot, POM toggle, height scale, stochastic tiling
6. **UV & Tiling** section — UV scale
7. **Transparency** section — alpha mode, cutoff, opacity, double-sided

**Modified files:**
- `engine/editor/panels/inspector_panel.h` / `.cpp` — New `drawTextureSlot()` helper, restructured material sections
- `engine/editor/editor.h` / `.cpp` — Texture file browser instance, slot assignment logic

---

### 5C-5: Material Preview

**Goal:** Render a small preview sphere showing the material applied, displayed in the inspector.

#### MaterialPreview Class

```cpp
/// @file material_preview.h
class MaterialPreview
{
public:
    void initialize(int resolution = 128);
    void render(const Material& material, Renderer& renderer);
    GLuint getTextureId() const;
    void markDirty();
    bool isDirty() const;
    void cleanup();

private:
    std::unique_ptr<Framebuffer> m_fbo;
    Mesh m_sphere;
    int m_resolution = 128;
    bool m_dirty = true;
    glm::mat4 m_viewMatrix;
    glm::mat4 m_projMatrix;
};
```

#### Implementation Details

- **FBO config:** 128x128, no MSAA, LDR (`isFloatingPoint = false`), depth attachment for correct occlusion.
- **Camera:** Position `(0, 0, 2.5)`, looking at origin, 45-degree FOV, 1:1 aspect ratio.
- **Lighting:** Fixed directional light from upper-right `normalize(1, 1, 1)`, warm white color. This matches the convention of Unity/Godot preview spheres.
- **Sphere mesh:** Use `Mesh::createSphere(32, 16)` — same quality as the spawn sphere.
- **Rendering:** Save/restore OpenGL state (viewport, bound FBO). Bind the preview FBO, clear, set up fixed camera/light uniforms, upload material uniforms, bind material textures (with fallback textures for empty slots per Mesa AMD driver requirement), draw sphere, restore state.
- **Dirty flag:** Set whenever any material property changes. The inspector calls `render()` only when dirty — at most once per frame.
- **Display:** `ImGui::Image()` at 128x128 in a "Preview" collapsible header at the top of the material section.

**Integration note:** The `MaterialPreview` needs access to the Renderer's shader programs to upload uniforms correctly. Pass a `Renderer&` reference, or extract the uniform upload into a shared utility. The simplest approach: `MaterialPreview` uses `Renderer::drawMesh()` directly with a temporary camera and fixed light setup.

**New files:**
- `engine/editor/material_preview.h` / `.cpp`

**Modified files:**
- `engine/editor/panels/inspector_panel.h` / `.cpp` — Add `MaterialPreview` member, draw preview in material section
- `engine/editor/editor.cpp` — Initialize `MaterialPreview`
- `engine/CMakeLists.txt` — Add `editor/material_preview.cpp`

---

### 5C-6: Asset Browser Panel

**Goal:** A dockable panel showing project assets with folder navigation, thumbnail grid, search/filter, and drag-drop to the scene/inspector.

#### Layout

```
+-----------------------------------------------+
| [Search: _______________] [Grid|List] [Scale]  |
| Home > assets > textures    (breadcrumbs)      |
+-------+---------------------------------------+
| assets|  [brick.png]  [gold.png]  [wood.png]  |
|  tex/ |  [normal.png] [rough.png] [ao.png]    |
|  mesh/|                                        |
|  pre/ |  [wall.gltf]  [lamp.obj]              |
|  shdr/|                                        |
+-------+---------------------------------------+
```

#### AssetBrowserPanel Class

```cpp
/// @file asset_browser_panel.h
class AssetBrowserPanel
{
public:
    void initialize(const std::string& assetsPath, ResourceManager& resources);
    void draw();

private:
    void drawFolderTree();
    void drawBreadcrumbs();
    void drawSearchBar();
    void drawGrid();
    void drawListView();
    void scanDirectory(const std::string& path);
    void pollFileChanges();

    // State
    std::string m_rootPath;
    std::string m_currentPath;
    std::string m_searchFilter;
    float m_thumbnailScale = 1.0f;
    bool m_gridView = true;  // true = grid, false = list

    // Directory entries
    struct AssetEntry
    {
        std::string name;
        std::string fullPath;
        std::string extension;
        AssetType type;
        bool isDirectory;
        GLuint thumbnailId = 0;
        bool thumbnailLoaded = false;
    };
    std::vector<AssetEntry> m_entries;

    // Thumbnail cache
    std::unordered_map<std::string, GLuint> m_thumbnailCache;
    std::queue<std::string> m_thumbnailQueue;

    // File watching
    std::unordered_map<std::string, std::filesystem::file_time_type> m_fileTimestamps;
    std::chrono::steady_clock::time_point m_lastPoll;

    ResourceManager* m_resources = nullptr;
};
```

#### Asset Type Detection

Extension-based lookup:

| Extensions | Type | Thumbnail Strategy |
|------------|------|--------------------|
| `.png`, `.jpg`, `.jpeg`, `.bmp`, `.tga`, `.hdr` | TEXTURE | Load & downsample image directly |
| `.obj`, `.gltf`, `.glb` | MESH | Icon glyph (FBO rendering deferred to later) |
| `.json` (in prefabs/) | PREFAB | Icon glyph |
| `.vert`, `.frag`, `.glsl` | SHADER | Icon glyph |
| directories | DIRECTORY | Folder icon |
| other | UNKNOWN | Generic file icon |

For Phase 5C, texture thumbnails are the priority. Mesh/material FBO thumbnails are deferred to a later phase to keep scope manageable.

#### Texture Thumbnail Loading

For texture assets, load the image at reduced resolution for the thumbnail:
1. Load the full image via `stb_image` (already in the project).
2. Create a small GL texture (64x64) using `glTexImage2D` with the loaded data.
3. Store in the LRU cache keyed by file path.
4. Process 1-2 thumbnails per frame from the queue.

Alternatively, use `ResourceManager::loadTexture()` directly (it caches textures), and display the full-size texture at 48x48 in ImGui — the GPU handles downscaling. This is simpler and avoids duplicate loading. The only cost is GPU memory for full-res textures, which is acceptable for a project with dozens of textures.

#### Drag-and-Drop

Asset browser items are drag sources with payload type `"ASSET_PATH"`:

```cpp
if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
{
    ImGui::SetDragDropPayload("ASSET_PATH", path.c_str(), path.size() + 1);
    ImGui::Text("%s", entry.name.c_str());
    ImGui::EndDragDropSource();
}
```

Receiving targets:
- **Inspector texture slots** — assign texture to the material slot
- **Viewport** — import model at camera focus (for mesh assets)
- **Hierarchy** — instantiate prefab (for prefab assets)

#### File Watching

Poll-based using `std::filesystem::last_write_time`, checked once per second:
- New files → add to entries, queue thumbnail
- Deleted files → remove from entries, evict thumbnail from cache
- Modified files → mark thumbnail as stale, re-queue

#### Docking Integration

Add the asset browser as a new dockable panel. Default layout: docked at the bottom alongside the console, or in a new tab group:

```
+-------------------+---------------------+---------+
|    Hierarchy      |      Viewport       |Inspector|
|                   |                     |         |
+-------------------+---------------------+---------+
|      Console      |    Asset Browser               |
+-------------------+-------------------------------+
```

**New files:**
- `engine/editor/panels/asset_browser_panel.h` / `.cpp`

**Modified files:**
- `engine/editor/editor.h` / `.cpp` — Add `AssetBrowserPanel` member, dock layout, drag-drop handling
- `engine/CMakeLists.txt` — Add `editor/panels/asset_browser_panel.cpp`

---

## Implementation Order & Dependencies

```
5C-1: Debug Draw System ──────────────┐
                                       ├──→ 5C-2: Light Viewport Gizmos
                                       │
5C-3: Enhanced Light Inspector ───────→│    (independent of 5C-1/2)
                                       │
5C-4: Material Editor Texture Slots ──→│    (independent of 5C-1/2/3)
                                       │
5C-5: Material Preview ──────────────→│    (benefits from 5C-4 restructuring)
                                       │
5C-6: Asset Browser Panel ───────────→┘    (provides drag-drop for 5C-4)
```

- **5C-1** must come first (debug draw needed for light gizmos)
- **5C-2** depends on 5C-1 (uses DebugDraw API)
- **5C-3** is independent (inspector changes only)
- **5C-4** is independent (inspector changes only)
- **5C-5** benefits from 5C-4 (material section restructuring) but can be done in any order
- **5C-6** is last (asset browser provides drag-drop sources for texture slots)

Recommended order: 5C-1 → 5C-2 → 5C-3 → 5C-4 → 5C-5 → 5C-6

---

## Performance Considerations

- **Debug draw:** Line accumulation is CPU-side (~16K vertices max = 384 KB). Single draw call per frame. GPU cost negligible.
- **Material preview:** 128x128 FBO render = ~0.01ms on RX 6600. Only when dirty. No per-frame cost when material is stable.
- **Texture thumbnails:** Using `ResourceManager::loadTexture()` with full-res textures displayed at 48x48. GPU handles downscaling. Memory: each 1024x1024 RGBA texture = 4 MB VRAM. A project with 100 textures = 400 MB — well within the RX 6600's 8 GB budget.
- **Asset browser polling:** `std::filesystem::recursive_directory_iterator` once per second. For a project with ~1000 files, this takes <1ms. No per-frame cost.
- **Light gizmos:** 3 circles × 32 segments = 192 lines per point light. 8 ribs + 32-segment circle = 72 lines per spot light. Total for a scene with 8 lights: ~1500 lines = trivial.

---

## Accessibility Notes

Continuing Phase 5A/5B accessibility standards:
- All texture slots have text labels alongside thumbnails (not icon-only)
- Thumbnail size (48x48) is readable at the user's 18pt font scale
- Color temperature gradient bar is decorative — the Kelvin value is always shown as text
- Attenuation curve has axis labels ("0" and range value as text)
- Asset browser supports list view (text + small icon) as alternative to grid view
- Drag-drop has visual feedback: highlighted border on valid drop targets
- All new sections use `CollapsingHeader` with text labels (not icon-only buttons)

---

## Design Decisions

1. **No sRGB correction in color pickers (yet).** The research recommends linear→sRGB conversion for display. However, the current inspector works without it and adding it risks confusing the user with different visual results. Defer to Phase 5D when full scene serialization can store color spaces explicitly. For now, document that colors are in linear space.

2. **Texture thumbnails use full-res textures.** Rather than generating downscaled thumbnail textures, display full-res textures at 48x48 via ImGui. Simpler implementation, no disk cache needed, acceptable VRAM cost. If VRAM becomes a concern with hundreds of textures, add downscaled caching later.

3. **Light range field is convenience, not authoritative.** The range field auto-calculates attenuation coefficients, but the raw coefficients remain editable in an "Advanced" section. This avoids breaking existing scenes while providing a better UX.

4. **Ambient/specular derived from diffuse.** For light components, show a single "Color" picker instead of three separate ambient/diffuse/specular pickers. Ambient = color × 0.1, specular = color. An "Advanced" section preserves the three-color model for power users. This matches Unity/Godot/Unreal conventions.

5. **Asset browser starts simple.** Grid view with texture thumbnails and type-based icons. No FBO mesh thumbnails in Phase 5C — they add complexity (auto-framing camera, per-mesh lighting) for limited benefit at this stage. Mesh thumbnails can be added incrementally later.

6. **Material preview uses existing Renderer.** Rather than duplicating shader setup, `MaterialPreview` calls `Renderer::drawMesh()` with a fixed camera and light. This ensures the preview matches the viewport exactly.

---

## Sources

### Material Editor
- [Unity Standard Shader Inspector](https://docs.unity3d.com/6000.0/Documentation/Manual/StandardShaderMaterialParameters.html)
- [Godot StandardMaterial3D](https://docs.godotengine.org/en/stable/tutorials/3d/standard_material_3d.html)
- [Blender Principled BSDF](https://docs.blender.org/manual/en/latest/render/shader_nodes/shader/principled.html)
- [ImGui Image Loading and Displaying](https://github.com/ocornut/imgui/wiki/Image-Loading-and-Displaying-Examples)
- [ImGui Color Space Discussion (#578, #1724)](https://github.com/ocornut/imgui/issues/578)
- [ImGui Drag and Drop (#1931)](https://github.com/ocornut/imgui/issues/1931)
- [Hazel Engine](https://github.com/TheCherno/Hazel)
- [LearnOpenGL Framebuffers](https://learnopengl.com/Advanced-OpenGL/Framebuffers)
- [Rendering FBO into ImGui Window](https://www.codingwiththomas.com/blog/rendering-an-opengl-framebuffer-into-a-dear-imgui-window)

### Asset Browser
- [Unity Project Browser](https://docs.unity3d.com/520/Documentation/Manual/ProjectView.html)
- [Unreal Content Browser](https://dev.epicgames.com/documentation/en-us/unreal-engine/content-browser-settings-in-unreal-engine)
- [LumixEngine Asset Browser Source](https://github.com/nem0/LumixEngine/blob/master/src/editor/asset_browser.cpp)
- [ImGui Grid Layout (#428)](https://github.com/ocornut/imgui/issues/428)
- [efsw Cross-Platform File Watcher](https://github.com/SpartanJ/efsw)
- [C++17 Filesystem File Watcher](https://solarianprogrammer.com/2019/01/13/cpp-17-filesystem-write-file-watcher-monitor/)
- [Godot FileSystem Dock](https://deepwiki.com/godotengine/godot/10.1-filesystem-dock)

### Light Editing
- [Unity Light Inspector](https://docs.unity3d.com/Manual/class-Light.html)
- [Unreal Physical Lighting Units](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-physical-lighting-units-in-unreal-engine)
- [Godot OmniLight3D](https://docs.godotengine.org/en/stable/classes/class_omnilight3d.html)
- [Godot Physical Light Units](https://docs.godotengine.org/en/stable/tutorials/3d/physical_light_and_camera_units.html)
- [Tanner Helland Kelvin-to-RGB](https://tannerhelland.com/2012/09/18/convert-temperature-rgb-algorithm-code.html)
- [Unity Gizmos API](https://docs.unity3d.com/ScriptReference/Gizmos.html)
- [glampert/debug-draw](https://github.com/glampert/debug-draw)
- [implot (for future curve widgets)](https://github.com/epezent/implot)
- [Catlike Coding: Point and Spot Lights](https://catlikecoding.com/unity/tutorials/custom-srp/point-and-spot-lights/)
