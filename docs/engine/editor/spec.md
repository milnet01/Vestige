# Subsystem Specification — `engine/editor`

## Header

| Field | Value |
|-------|-------|
| Subsystem | `engine/editor` |
| Status | `shipped` |
| Spec version | `1.0` |
| Last reviewed | `2026-04-28` (initial draft — pending cold-eyes review) |
| Owners | `milnet01` |
| Engine version range | `v0.1.0+` (Phase 5 Scene Editor; Phase 10.5 Onboarding & Settings polish) |

---

## 1. Purpose

`engine/editor` is the in-process scene-authoring user interface — the toolkit a content author uses to build, light, dress, and validate a Vestige scene without leaving the running engine. It owns the Dear ImGui (Immediate-Mode Graphical User Interface, henceforth ImGui) integration, the dockable panel layer (Hierarchy, Inspector, Asset Browser, Environment, Terrain, Audio, Navigation, Performance, Validation, Script Editor, Settings, …), the editor-side orbit/pan/zoom camera, transform gizmos via ImGuizmo, the undoable command stack, scene save / load + atomic auto-save + crash-recovery, the prefab system, the architectural and brush tools, and the first-run onboarding wizard. It exists as its own subsystem because it must run inside the engine process for **What You See Is What You Get (WYSIWYG)** real-time editing — every editor action mutates the live scene within one frame, with **no bake step on the runtime side** (CODING_STANDARDS §33). For the engine's primary use case (architectural walkthroughs of biblical structures — Tabernacle, Solomon's Temple), the editor is the apparatus that turns a sketch into a navigable space; "place a column, look at it, adjust" must be a sub-second loop.

## 2. Scope

| In scope | Out of scope |
|----------|--------------|
| `Editor` facade — owns ImGui lifetime, panel set, EDIT/PLAY mode toggle, viewport bounds, gizmo state | The renderer / scene runtime — `engine/renderer/`, `engine/scene/` (editor only *drives* them) |
| Dockable panels: Hierarchy, Inspector, Asset Browser, Environment, Terrain, Audio, Navigation, Performance, Validation, History, Script Editor, Settings, Welcome, First-Run Wizard, Texture / HDRI / Model Viewer, Sprite Atlas, Tilemap, UI Layout, UI Runtime | Panel content's underlying domain logic (e.g. terrain heightmap generation lives in `engine/terrain/`; the panel is a thin façade over it) |
| `EditorCamera` — orbit / pan / zoom, view presets, focus-on-selected, sync from runtime camera | First-person camera + collision controller — `engine/core/first_person_controller.h` |
| `Selection` — multi-select, primary entity, modifier-aware pick handling | Entity ownership / component storage — `engine/scene/scene.h` |
| `CommandHistory` + `EditorCommand` base + concrete commands (transform, create, delete, reparent, align/distribute, terrain sculpt, foliage paint, particle / entity property, place-tree) | Generic command-execution machinery outside the editor (no runtime command stack) |
| `FileMenu` — scene New / Open / Save / Save As, recent-files, unsaved-changes guard, 120 s auto-save, crash-recovery modal | Full project-management UI (multi-scene projects, asset bundling) — not yet built |
| `SceneSerializer` — JSON scene save / load with metadata + format-version envelope, atomic write via `engine/utils/atomic_write.h` | Per-component serialisation primitives — `engine/scene/entity_serializer.h` (editor wraps it) |
| `PrefabSystem` — entity-tree save / load to `assets/prefabs/*.json` | Prefab variants / overrides — not yet built (Open Q3) |
| `EntityActions` (free-function namespace) — duplicate, delete, group, copy/paste transform, align, distribute | Component-add / -remove primitives — `Entity::addComponent<T>()` |
| `EntityFactory` (static helpers) — Create-menu spawners (primitives, lights, particles, water, walls, rooms, stairs, …) | Mesh / material asset loading — `engine/resource/` |
| `MaterialPreview` — offscreen Frame Buffer Object (FBO) sphere preview for Inspector swatches | Generic offscreen render passes — `engine/renderer/` |
| Architectural / paint tools: `BrushTool`, `BrushPreviewRenderer`, `TerrainBrush`, `WallTool`, `RoomTool`, `CutoutTool`, `RoofTool`, `StairTool`, `PathTool`, `RulerTool` | Runtime gameplay tools (no runtime equivalent — these are EDIT-mode only) |
| Widgets: `CurveEditorWidget`, `GradientEditorWidget`, `NodeEditorWidget`, `AnimationCurve`, `ColorGradient` | Generic ImGui controls — those live in `imgui` / `imgui_internal.h` |
| `FirstRunWizard` — Phase 10.5 onboarding flow + template picker + resumability | Project templates themselves — `engine/editor/panels/template_dialog.{h,cpp}` (also editor, but a separate concern) |
| `SettingsEditorPanel` — UI in front of `engine/core/SettingsEditor` (live-apply / commit / revert / per-category restore) | The two-copy `SettingsEditor` state machine — `engine/core/settings_editor.h` |
| `RecentFiles` — persistence to `~/.config/vestige/recent_files.json` (XDG-aware) | Generic configuration directory resolution — `engine/utils/config_path.h` |

## 3. Architecture

```
                     ┌─────────────────────────────────────┐
                     │             Editor                  │
                     │  (engine/editor/editor.h:86)        │
                     │   ImGui frame, EDIT/PLAY mode,      │
                     │   gizmo state, viewport bounds      │
                     └────┬─────────────────┬──────────────┘
                          │ owns by value   │ owns by unique_ptr
   ┌──────────────────────┼─────────────────┼────────────────────────┐
   ▼                      ▼                 ▼                        ▼
┌──────────┐      ┌────────────────┐  ┌──────────────┐      ┌────────────────┐
│Selection │      │ CommandHistory │  │EditorCamera  │      │ ~25 panels     │
│(IDs +    │      │ (200-cmd ring, │  │ (orbit / pan │      │ Hierarchy,     │
│ primary) │      │  version-     │  │  / zoom +    │      │ Inspector, …   │
└─────┬────┘      │  counter      │  │  smoothed)   │      │ each owns its  │
      │           │  dirty-track) │  └──────┬───────┘      │ own state      │
      │           └────┬───────────┘         │              └──────┬─────────┘
      │                │ executes            │                     │
      │                ▼                     │                     │
      │       ┌──────────────────┐           │                     │
      │       │ EditorCommand    │           │                     │
      │       │ base + 13 conc.  │           │                     │
      │       └────────┬─────────┘           │                     │
      ▼                ▼                     ▼                     ▼
                 mutate Scene + ResourceManager + Terrain + FoliageManager
                            (via stored pointers, never via friendship)
                                          │
                                          ▼
                                   per-frame render
                                  (no bake — §33)

   ┌──────────────────────────┐    ┌─────────────────────────────────┐
   │ FileMenu                 │    │ Tools (8)                       │
   │ ┌─ RecentFiles           │    │ Brush, BrushPreview, TerrainBrush│
   │ ┌─ SceneSerializer       │    │ Wall, Room, Cutout, Roof, Stair, │
   │ ├─ atomic auto-save (120s│    │ Path, Ruler                      │
   │ ├─ crash-recovery modal  │    │ (gizmo+screen-space drag drivers)│
   │ └─ unsaved-changes guard │    └─────────────────────────────────┘
   └──────────────────────────┘
```

Key abstractions:

| Abstraction | Type | Purpose |
|-------------|------|---------|
| `Editor` | class | Owns ImGui frame, panels, gizmo state, EDIT/PLAY mode, viewport bounds. `engine/editor/editor.h:86` |
| `EditorMode` | enum | `EDIT` vs. `PLAY` toggle. `engine/editor/editor.h:79` |
| `EditorCamera` | class | Orbit/pan/zoom turntable camera with view presets and smoothed focus animation. `engine/editor/editor_camera.h:23` |
| `Selection` | class | Multi-select set + primary-id accessor + modifier-aware add/toggle. `engine/editor/selection.h:18` |
| `CommandHistory` | class | 200-command ring buffer with version-counter dirty tracking + saved-version invariant. `engine/editor/command_history.h:23` |
| `EditorCommand` | abstract base | `execute() / undo() / getDescription() / canMergeWith() / mergeWith()`. `engine/editor/commands/editor_command.h:19` |
| `FileMenu` | class | File menu, save/load dialogs, recent files, 120 s auto-save, unsaved-changes modal, crash-recovery modal. `engine/editor/file_menu.h:35` |
| `SceneSerializer` | class (static) | JSON scene I/O with metadata + format envelope; atomic write via `engine/utils/atomic_write.h`. `engine/editor/scene_serializer.h:44` |
| `PrefabSystem` | class | Save/load entity trees to `assets/prefabs/*.json`. `engine/editor/prefab_system.h:19` |
| `EntityFactory` | class (static) | Spawners for primitives, lights, particles, water, walls, rooms, stairs, slabs. `engine/editor/entity_factory.h:23` |
| `EntityActions` | namespace | duplicate / delete / group / copyTransform / pasteTransform / align / distribute. `engine/editor/entity_actions.h:23` |
| `MaterialPreview` | class | Offscreen FBO sphere preview for Inspector swatches. `engine/editor/material_preview.h:29` |
| `RecentFiles` | class | XDG-aware persistence at `~/.config/vestige/recent_files.json`. `engine/editor/recent_files.h:19` |
| Panel classes (~25) | class | Each panel owns its own state; drawn from `Editor::drawPanels`. `engine/editor/panels/*.h` |
| Tool classes (~10) | class | Editor-mode-only manipulators driving gizmo / brush / drag interactions. `engine/editor/tools/*.h` |
| Widget classes (5) | class | Reusable ImGui composite controls (curve, gradient, node graph). `engine/editor/widgets/*.h` |
| Concrete commands (13) | class | `transform_command.h`, `create_entity_command.h`, `delete_entity_command.h`, `reparent_command.h`, `align_distribute_command.h`, `composite_command.h`, `entity_property_command.h`, `particle_property_command.h`, `paint_foliage_command.h`, `paint_scatter_command.h`, `place_tree_command.h`, `terrain_sculpt_command.h`, `editor_command.h` (base). |

## 4. Public API

The editor exposes a many-header facade because each panel / tool is an independent unit; downstream code (chiefly `engine/core/engine.cpp`) reaches into individual panels via accessors. The legitimate `#include` targets are:

```cpp
// engine/editor/editor.h:86 — facade. Owns every panel + tool by value or unique_ptr.
class Editor {
    bool initialize(GLFWwindow*, const std::string& assetPath);
    void shutdown();
    void prepareFrame();
    void drawPanels(Renderer*, Scene*, Camera* = nullptr,
                    Timer* = nullptr, Window* = nullptr);
    void endFrame();
    void updateEditorCamera(float dt);
    void applyEditorCamera(Camera&);

    void setMode(EditorMode);  EditorMode getMode() const;  void toggleMode();

    void getViewportSize(int& w, int& h) const;
    glm::vec2 getViewportMin() const;

    bool isGizmoActive() const;
    bool wantCaptureMouse() const;     // ImGui IO pass-through
    bool wantCaptureKeyboard() const;

    void processViewportClick(int fboW, int fboH);
    bool isPickRequested() const;
    void getPickCoords(int& x, int& y) const;
    void handlePickResult(uint32_t entityId);

    // Engine-side wiring (called once from Engine::initialize).
    void setResourceManager(ResourceManager*);
    void setFoliageManager(FoliageManager*);
    void setTerrain(Terrain*);
    void setProfiler(PerformanceProfiler*);
    void setNavigationSystem(NavigationSystem*);
    void setAudioSystem(AudioSystem*);
    void setUISystem(UISystem*);
    void wireFirstRunWizard(OnboardingSettings*, std::filesystem::path,
                            std::function<void()> applyDemoCallback);
    void wireSettingsEditorPanel(SettingsEditor*, InputActionMap*,
                                 std::filesystem::path settingsPath);
    bool consumeWizardJustClosed();      // edge-triggered: engine calls Settings::saveAtomic.

    // Sub-component accessors — see editor.h:182–313 for full list.
    Selection& getSelection();           FileMenu& getFileMenu();
    CommandHistory& getCommandHistory(); BrushTool& getBrushTool();
    EnvironmentPanel& getEnvironmentPanel();
    /* …+ ~25 more accessor pairs, one per panel/tool. */

    void showNotification(const std::string&);
    bool consumeBoxSelect(int& x0, int& y0, int& x1, int& y1);
    bool consumeScreenshotRequest();
};
```

```cpp
// engine/editor/selection.h:18
void Selection::select(uint32_t id);                  // 0 clears
void Selection::addToSelection(uint32_t id);          // Shift+click
void Selection::toggleSelection(uint32_t id);         // Ctrl+click
void Selection::clearSelection();
bool Selection::isSelected(uint32_t id) const;
bool Selection::hasSelection() const;
const std::vector<uint32_t>& Selection::getSelectedIds() const;
uint32_t Selection::getPrimaryId() const;             // 0 if empty
Entity* Selection::getPrimaryEntity(Scene&) const;
```

```cpp
// engine/editor/command_history.h:23  — see header for full surface.
void CommandHistory::execute(std::unique_ptr<EditorCommand>);
void CommandHistory::undo();    void CommandHistory::redo();
bool CommandHistory::canUndo() const;  bool CommandHistory::canRedo() const;
void CommandHistory::clear();   void CommandHistory::markSaved();
bool CommandHistory::isDirty() const;
const std::vector<std::unique_ptr<EditorCommand>>& getCommands() const;
int  CommandHistory::getCurrentIndex() const;
static constexpr size_t MAX_COMMANDS = 200;

// engine/editor/commands/editor_command.h:19
class EditorCommand {
    virtual void execute() = 0;
    virtual void undo() = 0;
    virtual std::string getDescription() const = 0;
    virtual bool canMergeWith(const EditorCommand&) const { return false; }
    virtual void mergeWith(EditorCommand&) {}
};
```

```cpp
// engine/editor/file_menu.h:35
void FileMenu::tickAutoSave(const Scene*);             // call once / frame
void FileMenu::drawMenuItems(Scene*, Selection&);      // inside BeginMenu("File")
void FileMenu::processShortcuts(Scene*, Selection&);   // Ctrl+N/O/S/Shift+S/Q
void FileMenu::drawDialogs(Scene*, Selection&);        // browser + unsaved + recovery
void FileMenu::requestQuit();   bool FileMenu::shouldQuit() const;
void FileMenu::markDirty();     void FileMenu::markClean();
bool FileMenu::isDirty() const;
const std::filesystem::path& FileMenu::getCurrentScenePath() const;
void FileMenu::updateWindowTitle(const std::string& sceneName);
static constexpr float AUTO_SAVE_INTERVAL = 120.0f;    // seconds
```

```cpp
// engine/editor/scene_serializer.h:44
struct SceneSerializerResult { bool success; std::string errorMessage;
                               int entityCount; int warningCount; };
struct SceneMetadata { std::string name, description, author,
                       created, modified, engineVersion;
                       int formatVersion; };
static SceneSerializerResult SceneSerializer::saveScene(
    const Scene&, const std::filesystem::path&, const ResourceManager&,
    const FoliageManager* = nullptr, const Terrain* = nullptr);
static SceneSerializerResult SceneSerializer::loadScene(
    Scene&, const std::filesystem::path&, ResourceManager&,
    FoliageManager* = nullptr, Terrain* = nullptr);
static std::string SceneSerializer::serializeToString(
    const Scene&, const ResourceManager&);             // for auto-save
static SceneMetadata SceneSerializer::readMetadata(
    const std::filesystem::path&);                     // peek without loading
static constexpr int CURRENT_FORMAT_VERSION = 1;
```

```cpp
// engine/editor/entity_actions.h:23 — namespace EntityActions
Entity* duplicateEntity(Scene&, Selection&, uint32_t id);
void    deleteSelectedEntities(Scene&, Selection&);
std::string generateDuplicateName(const std::string&, const Entity* parent);
void    copyTransform(Scene&, uint32_t, TransformClipboard&);
void    pasteTransform(Scene&, uint32_t, const TransformClipboard&);
Entity* groupEntities(Scene&, Selection&);
enum class AlignAxis  { X, Y, Z };
enum class AlignAnchor { MIN, CENTER, MAX };
void alignEntities    (Scene&, const Selection&, CommandHistory&,
                       AlignAxis, AlignAnchor);
void distributeEntities(Scene&, const Selection&, CommandHistory&, AlignAxis);
```

```cpp
// engine/editor/entity_factory.h:23 — class EntityFactory (static methods)
static Entity* createCube      (Scene&, ResourceManager&, const glm::vec3& pos);
static Entity* createSphere    (Scene&, ResourceManager&, const glm::vec3&);
// …createPlane / createCylinder / createCone / createWedge
static Entity* createDirectionalLight(Scene&, const glm::vec3&);
// …createPointLight / createSpotLight / createParticleEmitter / createWaterSurface
static Entity* createWall      (Scene&, ResourceManager&, const glm::vec3&);
// …createWallWithDoor / createWallWithWindow / createRoom / createRoof
//    / createStairs / createSpiralStairs / createFloorSlab
```

```cpp
// engine/editor/prefab_system.h:19
bool    PrefabSystem::savePrefab(const Entity&, const std::string& name,
                                 const ResourceManager&,
                                 const std::string& assetsPath);
Entity* PrefabSystem::loadPrefab(const std::string& filePath, Scene&,
                                 ResourceManager&);
std::vector<std::string> PrefabSystem::listPrefabs(const std::string& assetsPath) const;
```

Panel and tool surfaces follow a uniform template — most expose `initialize / draw / setX(...)` plus a small handful of toggles. See per-header docs for the full surface.

**Non-obvious contract details:**

- `Editor::initialize` requires a valid `GLFWwindow*` and an `assetPath` from which to load the editor font. It is **not** safe to call before the GL 4.5 context exists. Two-stage: construct the `Editor` on the stack, but defer `initialize` to after `Window` creation (`engine/core/engine.cpp:72`).
- `Editor::prepareFrame` / `Editor::endFrame` straddle the rest of the editor draw — every panel and tool draws *between* them. ImGui's per-frame state machine fails loudly if those bracket calls are missing.
- `Editor::drawPanels` polls panel state machines for one-shot pick / box-select / screenshot intents that the engine consumes via `consume*()` accessors next frame. The consume pattern keeps the editor from reaching into the renderer — the renderer pulls intents out of the editor instead.
- `CommandHistory::execute` discards the redo branch and trims the oldest commands once the buffer hits `MAX_COMMANDS` (200). When trimming would erase the saved version, `m_savedVersionLost` flips to `true` and `isDirty()` returns `true` permanently until `markSaved()` runs again — by design, because the file on disk no longer corresponds to a state the user can reach via undo.
- `EditorCommand::canMergeWith` + `mergeWith` are how slider drags collapse into one undo entry. Concrete commands implement merge by checking a target id + property tag and absorbing the new value; `transform_command.h` is the canonical example.
- `SceneSerializer::saveScene` writes via `engine/utils/atomic_write.h` — write-temp + fsync(file) + rename + fsync(dir). On failure the original file is intact and `SceneSerializerResult::success == false` with a populated `errorMessage`.
- `FileMenu::tickAutoSave` writes `~/.config/vestige/autosave.scene` every 120 s **only when dirty**. On clean shutdown the autosave is deleted; if it survives a launch, the recovery modal offers to load it (`drawRecoveryModal` — `engine/editor/file_menu.h:123`).
- `EditorMode::PLAY` hides every panel, captures the cursor, and routes input to `FirstPersonController`. The transition is instant — there is no scene rebuild on either side of the toggle (CODING_STANDARDS §33).
- `wireFirstRunWizard` does **not** transfer ownership of `OnboardingSettings*`; `engine/core` owns the `Settings` struct. The editor reads + writes the substruct in place and signals "wizard closed" through `consumeWizardJustClosed()` so the engine layer can fire `Settings::saveAtomic`.
- Panel state is **not** serialised with the scene — dock layout, expanded/collapsed sections, viewport gizmo mode etc. live in ImGui's `imgui.ini` (auto-managed by ImGui itself) and `Settings.uiState` (engine/core).

**Stability:** the facade above is semver-respected for `v0.x`. The panel set itself is **not** semver-stable — panels may be added, removed, or split across phases without a major bump (panels are the editor's product surface, not its API surface). Concrete `EditorCommand` subclasses are not semver-stable either; downstream code calls them only via `CommandHistory::execute(std::unique_ptr<EditorCommand>)`.

## 5. Data Flow

**Steady-state per-frame (EDIT mode):**

1. `Engine::run` → `Editor::prepareFrame()` — `ImGui_ImplOpenGL3_NewFrame` + `ImGui_ImplGlfw_NewFrame` + `ImGui::NewFrame`.
2. `Window::pollEvents()` already drained GLFW callbacks → ImGui IO updated → `wantCaptureMouse / wantCaptureKeyboard` are valid.
3. Engine queries `Editor::wantCaptureKeyboard()` and skips game-side hotkey processing when ImGui has focus.
4. `Editor::drawPanels(renderer, scene, camera, timer, window)`:
   1. Begin DockSpaceOverViewport (ImGui docking branch).
   2. Draw main menu bar → calls `FileMenu::drawMenuItems` + Edit / View / Create / Help menus.
   3. Process global shortcuts: Ctrl+Z/Y (undo/redo via `CommandHistory`), Ctrl+S/N/O/Q (`FileMenu::processShortcuts`), F (focus selected via `EditorCamera::focusOn`), Numpad 1/3/7 (view presets), Ctrl+D (duplicate), Ctrl+G (group), Delete (`EntityActions::deleteSelectedEntities`).
   4. Draw 3D viewport — `ImGui::Image(rendererFboTexture)` sized to the dock cell. Capture viewport bounds → `m_viewportMin/Max` for next-frame click detection.
   5. Draw panels (Hierarchy → Inspector → Asset Browser → Environment → Terrain → … → Settings). Each panel's `draw(...)` reads + mutates its bound subsystem.
   6. Draw `ImGuizmo::Manipulate` over the viewport when an entity is selected. Gizmo deltas → on release, push a `TransformCommand` into `CommandHistory` (the slider-drag merge swallows interim mutations into one undo entry).
   7. Draw FileMenu dialogs (open/save browser, unsaved-changes modal, recovery modal).
   8. Draw notification overlay (auto-fades `m_notifyTimer`).
5. `Editor::processViewportClick(fboW, fboH)` — translates mouse position to FBO-space pick coords if the viewport was clicked this frame (suppressed when gizmo was active).
6. Engine renders the scene into the renderer's FBO; if `isPickRequested()` it also reads the ID buffer and calls `Editor::handlePickResult(entityId)` which routes through `Selection`.
7. `FileMenu::tickAutoSave(scene)` — writes `autosave.scene` atomically when dirty + interval elapsed.
8. `Editor::endFrame()` — `ImGui::Render` + `ImGui_ImplOpenGL3_RenderDrawData`. ImGui draws as the final overlay before `Window::swapBuffers`.

**EDIT → PLAY toggle:**

1. `Editor::toggleMode()` → mode = `PLAY`, cursor captured, panels hidden.
2. `EditorCamera::syncFromCamera(camera)` recorded **before** the toggle so the orbit camera resumes at the play camera's last orientation when the user toggles back. (`editor_camera.h:63`)
3. PLAY frame loop bypasses `drawPanels` — only `prepareFrame` + main viewport + `endFrame` so ImGui still drives input capture.

**Scene save (Ctrl+S):**

1. `FileMenu::saveScene(scene)` — if `m_currentScenePath` empty, falls through to `saveSceneAs`.
2. `SceneSerializer::saveScene(scene, path, resources, foliage, terrain)`.
3. `atomic_write` writes `<path>.tmp`, fsyncs, renames, fsyncs the parent directory.
4. On success: `RecentFiles::addPath` + `RecentFiles::save`, `CommandHistory::markSaved`, `FileMenu::markClean`, `updateWindowTitle`. On failure: `Logger::error` + notification overlay; `m_isDirty` remains true.

**Scene load (Ctrl+O / recent):**

1. Unsaved-changes guard: if `isDirty()`, show modal — Save / Discard / Cancel routes to `proceedWithPendingAction` once the user picks.
2. `SceneSerializer::loadScene(scene, path, resources, foliage, terrain)` clears the scene first, then re-creates entities + reads metadata + resolves resource references.
3. `Selection::clearSelection`; `CommandHistory::clear` (the post-load history is empty by definition); `markClean`; `updateWindowTitle`.
4. On parse error: scene reverted to whatever shape `loadScene` left (typically empty); error surfaced to user via notification + log.

**First-run wizard (cold start, fresh `Settings`):**

1. `Engine::initialize` builds the `Editor`, calls `wireFirstRunWizard(&settings.onboarding, assetRoot, applyDemoCallback)`.
2. If `settings.onboarding.hasCompletedFirstRun == false`, the wizard auto-opens on the first frame.
3. Wizard step machine (Phase 10.5 design): Welcome → Template Picker → Done. "Skip for now" closes without flipping the flag — re-opens next launch.
4. On terminal close, `m_wizardJustClosedThisFrame = true`. Engine polls `consumeWizardJustClosed()` next frame and runs `Settings::saveAtomic`.

**Crash recovery:**

1. On `Editor::initialize` (via `FileMenu`), check whether `~/.config/vestige/autosave.scene` exists.
2. If yes → `m_recoveryPending = true`, show recovery modal on first frame: "Recover unsaved changes from <timestamp>? [Recover] [Discard]".
3. Recover loads the autosave file via `SceneSerializer::loadScene` — same path as a normal Ctrl+O.

## 6. CPU / GPU placement

The editor is overwhelmingly CPU work. The 3D viewport hooks the renderer's existing FBO output for display; nothing in `engine/editor/` originates per-pixel or per-vertex GPU work *except* `MaterialPreview`, which lights a small offscreen sphere into a 128×128 FBO on demand.

| Workload | Placement | Reason |
|----------|-----------|--------|
| Panel layout, widget interaction, hit-testing, dock arbitration | CPU (main thread) | Branching / decision-heavy — exactly the §17 default. ImGui is itself CPU. |
| ImGui render-data → draw-list → GL submission | CPU (ImGui assembly) → GPU (`ImGui_ImplOpenGL3_RenderDrawData`) | ImGui's hand-tuned vertex assembly + textured-quad rasterisation; vendor-blessed. |
| Editor camera orbit / pan / zoom math | CPU (main thread) | Per-frame; ≤ a handful of `glm` ops; not data-parallel. |
| Selection set updates, undo/redo dispatch | CPU (main thread) | Sparse, branching, indirect (each command knows its own target). |
| Scene save / load JSON parse + atomic write | CPU (main thread, save / load only) | I/O + parse; never per-frame. Auto-save likewise (every 120 s). |
| Material preview sphere render | GPU (one frag-shader pass) | Per-pixel shading is the canonical GPU case; only re-rendered when the underlying material is mutated (`MaterialPreview::markDirty`). |
| Brush preview overlay (terrain ring, foliage stamp) | GPU (forward overlay pass driven by editor; `BrushPreviewRenderer`) | Per-pixel; integrates with the renderer's overlay pass. |
| Terrain sculpt apply (heightmap mutation) | CPU (main thread) | Sparse update → small region of a texture; the GPU upload is a `glTexSubImage2D` triggered by the editor command's `execute`. |
| Viewport pick (click → ID buffer read) | GPU (ID buffer rasterised as part of the main pass) → CPU (`glReadPixels` on a 1×1 region) | The ID buffer is already produced by the renderer; the editor reads one pixel per click. |

No dual implementation; no parity test required. The editor is a control surface, not a compute path.

## 7. Threading model

| Caller thread | Allowed APIs | Locks held |
|---------------|--------------|------------|
| **Main thread** (the only thread that runs `Engine::run`) | All of `Editor`, every panel, every tool, `Selection`, `CommandHistory`, `FileMenu`, `SceneSerializer::saveScene/loadScene`, `PrefabSystem`, `EntityFactory`, `EntityActions`, `EditorCamera`, `MaterialPreview`. ImGui context lives here. | None. |
| Worker threads | None. The editor must not be touched off-thread. | — |

ImGui is not thread-safe — its global context is single-threaded by design, and `Editor::prepareFrame / drawPanels / endFrame` are bracketed by `ImGui::NewFrame / Render` calls on the main thread. GLFW callbacks fire on the main thread (CODING_STANDARDS §13), so input → ImGui IO is a same-thread hop.

**Async loaders flagged for follow-up (Open Q5):** `engine/resource/AsyncTextureLoader` runs on a worker pool; the editor's Asset Browser thumbnails currently subscribe to its completion via the synchronous `EventBus::publish<TextureLoadedEvent>` (publisher = main thread, callback runs there). No editor data structure is mutated off-thread today. If the Asset Browser ever needs background mesh imports, the producer must marshal results back to the main thread — there is no editor-side queue today.

**Lock-free / atomic:** none required. `CommandHistory::isDirty()` and `FileMenu::isDirty()` are plain-int reads on the main thread; the version-counter scheme in `CommandHistory` (`m_version` vs `m_savedVersion`) is single-thread by contract.

## 8. Performance budget

60 frames per second hard requirement (CLAUDE.md) → 16.6 ms per frame. The editor is overhead — every millisecond it spends shrinks the renderer's slice. EDIT mode runs the full panel set; PLAY mode skips `drawPanels` entirely (only ImGui IO + the empty frame survives).

Not yet measured — will be filled by the Phase 11 audit; tracked as Open Q1 in §15.

Profiler markers / capture points: `engine/editor` does not emit `glPushDebugGroup` markers today (Open Q4). The renderer's existing markers on the main pass + ID buffer + post-process bracket every editor-driven draw, so RenderDoc captures of the editor frame are searchable by the renderer's labels. Add `EditorPanels`, `EditorGizmo`, `EditorViewport`, `MaterialPreview` markers at the next pass.

Empirical EDIT-mode budget assumed during implementation: ImGui draw-call submission ≤ 2 ms on RX 6600 at 1080p (vendor-typical for ~25 panels with a 3D viewport). The per-frame ImGui draw cost dominates; everything else (Selection / CommandHistory / EditorCamera math) is sub-frame noise.

## 9. Memory

| Aspect | Value |
|--------|-------|
| Allocation pattern | Heap. Most panels and tools are owned by value as members of `Editor`; the editor camera and command history sit behind `unique_ptr` / containers. ImGui itself allocates from its own arena. |
| Reusable per-frame buffers | `ImGui::DrawListSharedData` (vendor-internal); per-panel scratch strings retained across frames. The `m_renderData` / pick-state buffers in `Editor` itself live across frames so the hot path doesn't heap-alloc. |
| Peak working set | Low single-digit MB for the editor itself (panel state + `CommandHistory` capped at 200 commands; the heaviest cell is each command's captured-state payload — a few hundred bytes for `TransformCommand`, kilobytes for `TerrainSculptCommand` which keeps before/after height tiles). ImGui draw lists + font atlas ≈ 1–2 MB. `MaterialPreview` 128×128 RGBA8 + depth ≈ 80 KB GPU. `RecentFiles` ≤ 10 entries. |
| Ownership | `Editor` owns every panel, tool, and widget. `CommandHistory` owns its `unique_ptr<EditorCommand>` slots. ImGui's draw lists / context owned by the ImGui library (`ImGui::CreateContext` ↔ `ImGui::DestroyContext` in `Editor::initialize/shutdown`). |
| Lifetimes | Engine-lifetime — every editor allocation lives from `Editor::initialize` until `Editor::shutdown`. Per-command lifetimes scoped to `CommandHistory` (oldest evicted at 200). Auto-save string buffer recycled per write. |

No `new`/`delete` in feature code (CODING_STANDARDS §12). ImGui internal allocations are routed through `ImGui::SetAllocatorFunctions` — currently default (libc); a custom allocator is on the post-MIT debt list.

## 10. Error handling

Per CODING_STANDARDS §11 — no exceptions in steady-state hot paths.

| Failure mode | Reported as | Caller's recourse |
|--------------|-------------|-------------------|
| `Editor::initialize` failed (ImGui context create, GL backend init, font load) | `Logger::error` + `return false` from `initialize` | Engine logs a warning and continues without editor (CODING_STANDARDS §11 — soft-disable per `engine/core/engine.cpp` initialization pattern). |
| Scene save failed (disk full, permission, atomic-write rename failed) | `SceneSerializerResult{ success=false, errorMessage }` — atomic-write didn't commit; old file intact | `FileMenu` surfaces "save failed" via notification + Logger. `m_isDirty` stays true so the user can retry; `CommandHistory::markSaved` is **not** called. |
| Scene load — JSON parse error / unknown format version | `SceneSerializerResult{ success=false }` + `Logger::error` | Scene left empty; user notified; `CommandHistory::clear` still runs (post-load state is consistent). |
| Scene load — missing asset references (mesh / texture / material) | `warningCount` incremented inside `SceneSerializerResult`; entity created with placeholder material | Surface as "loaded with N warnings" toast; user can hand-edit the JSON. |
| Prefab save failed | `PrefabSystem::savePrefab` returns `false` + `Logger::error` | Notification; user retries. |
| Prefab load failed | `PrefabSystem::loadPrefab` returns `nullptr` + `Logger::error` | Caller (Asset Browser drag-drop) restores no entity. |
| Auto-save write failed | Logger warning; the user-visible scene state is unaffected | Next 120 s tick retries. The crash-recovery modal will not offer a stale autosave because the previous successful write is what survives on disk. |
| Undo on empty stack / Redo with no redo branch | No-op (silent) | Buttons disabled by `canUndo()` / `canRedo()` — pressing the shortcut anyway is a no-op by design. |
| `CommandHistory` saved-version evicted (200-cmd ring trimmed past it) | `m_savedVersionLost = true`; `isDirty()` becomes permanently true until next save | Window title shows the dirty marker; user saves to recover the invariant. |
| GLFW window-close while wizard or unsaved-changes modal is open | The window-close routes through `FileMenu::requestQuit()` → unsaved-changes guard; the wizard's window-close is treated as Skip-for-now (Phase 10.5 §4) | Engine polls `FileMenu::shouldQuit()` and only flips `m_isRunning` when set. |
| Subscriber callback inside an editor `EventBus::publish` throws | Propagates to publisher (per `engine/core` policy) | Callbacks must not throw — fix the callback. |
| Programmer error (null `Scene*` passed to a panel that requires one) | `assert` (debug); panels that accept nullptr defensively early-return | Fix the caller. |
| Out of memory | `std::bad_alloc` propagates | App aborts (matches CODING_STANDARDS §11 — OOM is fatal). |

`Result<T, E>` / `std::expected` not yet used in `engine/editor` (the codebase pre-dates the policy). `SceneSerializerResult` is the local equivalent — a status enum + payload — and remains the shape until the broader engine-wide `Result` migration (Open Q6).

## 11. Testing

| Concern | Test file | Coverage |
|---------|-----------|----------|
| `Selection` add / toggle / clear / primary | `tests/test_selection.cpp` | Public API contract |
| `CommandHistory` execute / undo / redo / merge / version-counter dirty / saved-version-evicted invariant | `tests/test_command_history.cpp` | Public API contract + dirty tracking |
| `SceneSerializer` round-trip + metadata + format-version + atomic-write semantics | `tests/test_scene_serializer.cpp` | Save / load + missing-asset warnings |
| `PrefabSystem` save / load / list / drag-drop integration | `tests/test_menu_prefabs.cpp` | Prefab JSON round-trip |
| `FileMenu` Save / Save As / Recent / unsaved guard / autosave / recovery | `tests/test_file_menu.cpp` | Menu actions + dirty tracking |
| `FirstRunWizard` step machine + Settings flag promotion + Skip-for-now non-persistence | `tests/test_first_run_wizard.cpp` | Phase 10.5 onboarding flow |
| Asset viewer panels (Texture / HDRI / Model) | `tests/test_editor_viewers.cpp` | Smoke / regression |
| Audio / Navigation / Sprite / UI Layout / UI Runtime panels | `tests/test_audio_panel.cpp`, `test_navigation_panel.cpp`, `test_sprite_panel.cpp`, `test_ui_layout_panel.cpp`, `test_ui_runtime_panel.cpp` | Panel contract + state machine |
| Visual-script node editor | `tests/test_formula_node_editor_panel.cpp` | Node graph round-trip |

Every concrete `EditorCommand` subclass that is introduced ships with a focused test next to its peers (project rule — every feature + bug fix gets a test).

**Adding a test for `engine/editor`:** drop a new `tests/test_<thing>.cpp` next to its peers, link against `vestige_engine` + `gtest_main` in `tests/CMakeLists.txt` (auto-discovered via `gtest_discover_tests`). Use `Selection`, `CommandHistory`, `SceneSerializer`, `PrefabSystem`, `EntityActions`, `EntityFactory`, `EditorCamera`, `RecentFiles` directly without an `Editor` instance — every editor primitive **except `Editor` itself, the per-panel `draw()` path, `MaterialPreview`, and `BrushPreviewRenderer`** is unit-testable headlessly. ImGui-bound paths (`Editor::initialize / drawPanels / endFrame`, every `<Panel>::draw`) require a GLFW + GL 4.5 context — exercise those via `engine/testing/visual_test_runner.h`. Determinism for randomness: inline a fixed seed (the editor has no built-in PRNG today).

**Coverage gap:** the panel `draw()` pass is not unit-testable headlessly because ImGui requires a live context bound to a GL backend; visual-test runs in CI catch only crashes, not pixel-level layout regressions. The `Editor` orchestration loop (`prepareFrame → drawPanels → endFrame`) likewise only has smoke coverage.

## 12. Accessibility

The user is partially sighted (project memory). The editor is the engine's largest surface of user-facing pixels — accessibility constraints here are non-negotiable.

**Inspector / panel contrast.** Every Inspector field, every panel header, every dock title-bar must meet the contrast ratio target inherited from the engine theme (CODING_STANDARDS §27 references the runtime UI theme; the editor theme follows the same palette). The dark theme is set via `Editor::setupTheme()` (`engine/editor/editor.cpp` static method). High-contrast variants flow in through `Settings::accessibility.uiHighContrast` via the `UIAccessibilityApplySink`; the editor must subscribe to that in a future pass — the runtime UI does today, the editor inherits the ImGui style and currently does not switch (Open Q2).

**Keyboard navigation.** ImGui's `ImGuiConfigFlags_NavEnableKeyboard` is enabled in `Editor::initialize` so every panel is reachable without a mouse. Every Inspector field has an explicit label (no icon-only buttons in editing surfaces); icon-only items in the toolbar carry an ImGui tooltip (`ImGui::SetTooltip`). Every menu and dialog must be reachable via Tab + Arrow + Enter without leaving the keyboard.

**Color-only encoding forbidden.** The Hierarchy panel's prefab/instance colour stripe carries an icon as well as the colour; the History panel's "current cursor" arrow is a glyph, not a hue; the Validation panel's severity carries a `[ERROR] / [WARN] / [INFO]` text prefix in addition to the colour cue (`engine/editor/panels/validation_panel.h`). The Logger console (Phase 1+) labels its levels with text per the `engine/core` accessibility constraint already on file.

**Reduced motion.** The `EditorCamera::focusOn` smoothed transition + the notification overlay's fade are the only motion in the editor. Both must respect `Settings::accessibility.reducedMotion` — the editor's smoothing is currently unconditional (Open Q3); the runtime camera-shake and post-process motion-blur paths already gate on the same flag.

**Photosensitive safety.** No flashing or strobing in the editor itself. `BrushPreviewRenderer`'s pulse is a slow sin curve (≤ 1 Hz) well below the photosensitive cap (`PhotosensitiveSafetyWire::maxStrobeHz = 2.0`).

**Gamepad support.** ImGui's `ImGuiConfigFlags_NavEnableGamepad` is enabled — the editor is fully drivable with an Xbox / PS controller (CLAUDE.md hardware target). The First-Person Controller's gamepad bindings are reused in PLAY mode unchanged.

**Wizard accessibility.** The `FirstRunWizard` step machine lays out two large buttons per step (no small click targets); every button has a keyboard shortcut shown in the label; the "Skip for now" link is reachable via Tab.

## 13. Dependencies

| Dependency | Type | Why |
|------------|------|-----|
| `engine/core/engine.h`, `event_bus.h`, `i_system.h`, `settings.h`, `settings_editor.h` | engine subsystem | Lifecycle, event bus, system access, settings editor backend. |
| `engine/scene/scene.h`, `entity.h`, `component.h`, `entity_serializer.h` | engine subsystem | Scene mutation, entity tree, per-component serialisation. |
| `engine/renderer/renderer.h`, `camera.h`, `framebuffer.h`, `mesh.h`, `shader.h` | engine subsystem | Viewport texture, gizmo projection, material preview FBO + shader, brush overlay. |
| `engine/resource/resource_manager.h` | engine subsystem | Asset Browser thumbnails, Inspector material library, EntityFactory. |
| `engine/terrain/terrain.h`, `terrain_brush.h` | engine subsystem | Terrain panel + sculpt commands. |
| `engine/environment/foliage_manager.h` | engine subsystem | Environment panel + paint commands. |
| `engine/audio/audio_system.h` | engine subsystem | Audio panel mixer / zones / debug. |
| `engine/navigation/navigation_system.h` | engine subsystem | Navigation panel (navmesh visualisation + bake controls). |
| `engine/profiler/performance_profiler.h` | engine subsystem | Performance panel readouts. |
| `engine/ui/ui_system.h`, `ui_theme.h` | engine subsystem | UI Runtime panel + UI Layout panel. |
| `engine/scripting/node_type_registry.h` | engine subsystem | Visual-script editor's node palette. |
| `engine/utils/atomic_write.h`, `config_path.h` | engine subsystem | Crash-safe scene saves; XDG config directory. |
| `<imgui.h>` (docking branch) | external | The whole panel layer. |
| `<imgui_internal.h>` | external | Dockspace internals + a handful of low-level widget primitives. |
| `<ImGuizmo.h>` | external | Transform gizmo (translate / rotate / scale, world / local mode). |
| `<imfilebrowser.h>` | external | File open / save dialogs (header-only ImGui addon). |
| `<glad/gl.h>`, `<GLFW/glfw3.h>` | external | GL function loader; window handle for backends. |
| `<glm/glm.hpp>` | external | Math primitives. |
| `<nlohmann/json.hpp>` | external (transitively via SceneSerializer + RecentFiles) | JSON persistence. |
| `<filesystem>`, `<chrono>`, `<functional>`, `<memory>`, `<string>`, `<vector>` | std | Paths, timers, callbacks, ownership. |

**Direction:** `engine/editor` depends downstream on virtually every domain subsystem (it is the control surface for them). Domain subsystems must not depend on `engine/editor` — runtime-only builds exclude the entire `engine/editor/` tree (CODING_STANDARDS §33 — `VESTIGE_EDITOR` gate). The one borderline case is the renderer's `BrushPreviewRenderer` overlay pass slot, which the editor *fills* but the renderer owns the slot machinery for — that is correct (the slot is a runtime feature; it just happens to be unused outside editor builds).

## 14. References

Cited research / authoritative external sources:

- Omar Cornut. *Dear ImGui — Docking & Multi-Viewport Wiki* (ongoing, current). DockSpace placement, viewport coordinate-system gotchas, the docking-branch stability guarantee. <https://github.com/ocornut/imgui/wiki/Docking>, <https://github.com/ocornut/imgui/wiki/Multi-Viewports>
- Omar Cornut. *Dear ImGui — Input and Navigation* (DeepWiki, 2025). Keyboard / gamepad navigation flags, focus management, accessibility surface area. <https://deepwiki.com/ocornut/imgui/2.6-input-and-navigation>
- Omar Cornut. *Dear ImGui — Accessibility tracking issue #8022* (open, 2024–2025). Current state of screen-reader / assistive-tech support; informs the "label everything, never icon-only" rule. <https://github.com/ocornut/imgui/issues/8022>
- Robert Nystrom. *Game Programming Patterns — Command.* Canonical reference for the undo/redo command pattern + composability. <https://gameprogrammingpatterns.com/command.html>
- esveo. *Undo, Redo, and the Command Pattern* (2024–2025). Modern phrasing of the version-counter dirty-tracking invariant + merge-consecutive semantics this engine implements. <https://www.esveo.com/en/blog/undo-redo-and-the-command-pattern/>
- Wolfire Games. *How We Implement Undo* (2009, still cited). Per-edit memory budget and command-merge rationale used as a sanity check for the 200-command ring + per-command payload size targets. <http://blog.wolfire.com/2009/02/how-we-implement-undo/>
- Constanta. *Crash-safe JSON at scale: atomic writes + recovery without a DB* (2024–2025). Independent confirmation of the write-temp + fsync + rename + fsync-dir dance `engine/utils/atomic_write.h` implements; the editor's scene save inherits the guarantee. <https://dev.to/constanta/crash-safe-json-at-scale-atomic-writes-recovery-without-a-db-3aic>
- Sergei Galuzin. *Coherent GT — User Interface creation and editing* (2025). External WYSIWYG-editor design that frames the live-edit / no-bake invariant the editor commits to. <https://coherent-labs.com/Documentation/unity-gt/df/d44/_u_i_creation.html>
- Cedric Guillemet. *ImGuizmo — README & API.* Translate/rotate/scale gizmo lifecycle (`Manipulate`, `IsUsing`, `IsOver`) used by `Editor::drawGizmo` + the slider-drag merge into one undo entry. <https://github.com/CedricGuillemet/ImGuizmo>

Internal cross-references:

- `CODING_STANDARDS.md` §11 (errors), §12 (memory), §13 (threading), §17 (CPU/GPU), §18 (public API), §27 (units), §32 (asset paths), **§33 (Editor-Runtime Boundary — authoritative for this subsystem)**.
- `ARCHITECTURE.md` §1–6 (subsystem map, engine loop, event bus).
- `CLAUDE.md` rules 1, 5, 7 (research-first, library currency, CPU/GPU placement).
- `docs/phases/phase_05*_design.md` (Phase 5A–5I — Scene Editor design corpus).
- `docs/phases/phase_10_5_first_run_wizard_design.md` (Phase 10.5 onboarding flow).
- Project memory: `feedback_editor_realtime.md` (real-time WYSIWYG, no bake, no render waits).

## 15. Open questions

| # | Question | Owner | Target |
|---|----------|-------|--------|
| 1 | Performance budgets in §8 are unmeasured. Need a Tracy / RenderDoc capture of an EDIT-mode frame on the demo scene to fill in real numbers; PLAY mode separately. | `milnet01` | Phase 11 audit |
| 2 | Editor theme does not yet switch on `Settings::accessibility.uiHighContrast`. Runtime UI does today; editor inherits the unconditional dark theme. Plumb `UIAccessibilityApplySink` through `Editor::setupTheme`. | `milnet01` | Phase 11 entry |
| 3 | `EditorCamera::focusOn` smoothing + notification-overlay fade do not gate on `Settings::accessibility.reducedMotion`. Photosensitive safety already gates camera-shake and motion-blur on the runtime side; the editor needs the same hookup. | `milnet01` | Phase 11 entry |
| 4 | No `glPushDebugGroup` markers in `engine/editor` (CODING_STANDARDS §29 names `EditorPanels`, `EditorGizmo`, `EditorViewport`, `MaterialPreview` as the suggested set). RenderDoc captures currently identify editor draws only by the renderer's surrounding labels. | `milnet01` | Phase 11 audit |
| 5 | Asset Browser thumbnails currently rely on synchronous `EventBus::publish<TextureLoadedEvent>` from `AsyncTextureLoader` (publisher-thread dispatch, callback runs on main). If background mesh / HDRI imports land, the editor needs an explicit main-thread marshalling queue. | `milnet01` | post-MIT release (Phase 12) |
| 6 | `Result<T, E>` / `std::expected` adoption — `SceneSerializerResult`, `PrefabSystem` `bool` returns, `FileMenu` `bool` flags all predate the codebase-wide policy. Migration on the broader engine-wide debt list. | `milnet01` | post-MIT release (Phase 12) |
| 7 | Prefab variants / overrides not yet built (Unity-style override stack on instantiated prefabs). Current `PrefabSystem` is round-trip-only. | `milnet01` | triage (no scheduled phase) |
| 8 | Custom ImGui allocator not wired (`ImGui::SetAllocatorFunctions` defaults to libc). Wiring it through the engine's allocator improves leak attribution and respects future arena policies. | `milnet01` | post-MIT release (Phase 12) |
Each row above also corresponds to a `// TODO(YYYY-MM-DD milnet01)` comment near the affected code (CODING_STANDARDS §20). Closed items live in §16 (Spec change log), not here — §15 is for **open** questions.

## 16. Spec change log

| Date | Spec version | Author | Change |
|------|--------------|--------|--------|
| 2026-04-28 | 1.0 | milnet01 | Initial spec — Phase 5 Scene Editor + Phase 10.5 First-Run Wizard, formalised post-Phase 10.9 audit. Phase 10.9 W14 SpritePanel/TilemapPanel zombies confirmed closed 2026-04-25 in CHANGELOG. |
| 2026-04-30 | 1.0.1 | milnet01 | Cold-eyes review fix-up: §4 facade-coverage boundary clarified (semver-frozen API blocks vs not-semver-stable panel/tool/widget headers). §15 closed Q9 (W14) moved into this change log per template — §15 is for open questions only. |
