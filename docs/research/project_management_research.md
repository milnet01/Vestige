# Phase 5D Design: Project Management System

## Research Findings

Research conducted 2026-03-20 covering project file structure, file menu operations, recent files tracking, asset directory monitoring, dirty flag pattern, cross-platform path handling, and file dialogs.

---

## 1. Project File Structure

### How Major Engines Organize Projects

#### Unity (.meta files + scene files)
- **Project structure**: A top-level `Assets/` folder contains all user content. `ProjectSettings/` stores editor and build configuration. `Library/` is a local cache (not committed to VCS). `Packages/` lists dependencies.
- **Asset tracking via .meta files**: Every file and folder in `Assets/` gets a companion `.meta` file (YAML format). Each `.meta` file contains:
  - `fileFormatVersion: 2` -- format version
  - `guid: de9a32f15f2628044842629a83d3d974` -- a 128-bit GUID that uniquely identifies the asset
  - Import settings specific to the asset type (e.g., `TextureImporter`, `MonoImporter`, `NativeFormatImporter`)
  - The GUID system lets assets reference each other by ID rather than path, so renaming/moving a file preserves all references as long as the `.meta` file moves with it
- **Scene files**: `.unity` (binary) or `.unity` (YAML text) files that serialize the full scene hierarchy, component data, and references to assets via GUIDs
- **User settings**: `UserSettings/` folder stores per-user layout, selection state, etc. (not committed to VCS)

Sources:
- [Understanding .meta files and GUIDs - Unity at Scale](https://unityatscale.com/unity-meta-file-guide/understanding-meta-files-and-guids/)
- [Why We Need .meta Files in Unity - ITNEXT](https://itnext.io/why-we-need-meta-files-in-unity-understanding-their-role-and-importance-3ce99622bf0a)
- [Unity Manual: Asset Metadata](https://docs.unity3d.com/Manual/AssetMetadata.html)
- [Managing Meta Files in Unity - ForrestTheWoods](https://www.forrestthewoods.com/blog/managing_meta_files_in_unity/)

#### Godot (project.godot + .tscn/.scn)
- **project.godot**: A single INI-format file at the project root. Contains engine version (`config_version`), the initial scene path (`run/main_scene`), input mappings, display settings, and all project-wide configuration. Only settings that differ from defaults are written -- if a setting is absent, the default applies.
- **Scene files**: `.tscn` (text) or `.scn` (binary) files using Godot's custom resource format. Scenes serialize a node tree with properties, signals, and references to external resources.
- **No .meta files**: Godot uses resource paths (`res://path/to/file.png`) instead of GUIDs. This is simpler but means renaming a file breaks all references to it.
- **Editor settings**: Stored outside the project in a per-user config directory (Linux: `$HOME/.config/godot/`, Windows: `%APPDATA%\Godot\`). Includes `editor_settings-4.x.tres` with layout, recent files, and preferences.

Sources:
- [Godot Engine - Project Settings documentation](https://docs.godotengine.org/en/stable/tutorials/editor/project_settings.html)
- [Godot Engine - File paths in projects](https://docs.godotengine.org/en/stable/tutorials/io/data_paths.html)
- [Scene Management - DeepWiki](https://deepwiki.com/godotengine/godot/8.5-scene-management)

#### Unreal Engine (.uproject + directory convention)
- **.uproject file**: JSON format with `FileVersion` (required, currently 3), engine version association, module list, and plugin enable/disable list.
- **Directory layout**:
  - `Content/` -- all game assets (meshes, textures, materials, maps, Blueprints)
  - `Config/` -- `.ini` configuration files (DefaultEngine.ini, DefaultGame.ini, DefaultEditor.ini, etc.)
  - `Source/` -- C++ source code
  - `Binaries/` -- compiled executables and libraries
  - `Intermediate/` -- temporary build files, shader compilation cache
  - `Saved/` -- autosaves, crash logs, editor logs, local config overrides
  - `Build/` -- platform-specific build files
  - `DerivedDataCache/` -- cached derived data (cooked assets, shaders)
- **Config hierarchy**: Engine defaults -> Project defaults (`Config/`) -> Platform overrides -> Saved local overrides. Each layer only stores deltas.

Sources:
- [Unreal Engine Directory Structure](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-directory-structure)
- [Unreal Engine .uproject format - Epic Forums](https://forums.unrealengine.com/t/where-is-the-documentation-on-the-uproject-file-format/424551)
- [Unreal Engine - Preserving Immersive Media KB](https://pimkb.gitbook.io/pimkb/preserving-3d-software/game-engines/unreal-engine)

### Recommended Approach for Vestige

Separate concerns into distinct files:

| File | Purpose | Format | Committed to VCS? |
|------|---------|--------|-------------------|
| `project.vestige` | Project metadata: engine version, project name, asset directories, initial scene | JSON | Yes |
| `*.scene` | Scene data: entity hierarchy, component data, references to assets | JSON | Yes |
| `editor_state.json` | Per-user editor state: window layout, camera position, selection, recently opened scenes within project | JSON | No (.gitignore) |
| `~/.config/vestige/settings.json` (Linux) or `%APPDATA%\Vestige\settings.json` (Windows) | Global editor preferences: recent projects list, theme, keybindings | JSON | N/A (user-global) |

**Key decisions:**
- Use **relative paths** within project files (relative to project root) for portability
- Use **resource path strings** (like Godot's `res://`) rather than GUIDs for simplicity -- GUIDs add complexity that is premature for this stage
- Store **only non-default values** in config files (like Godot's approach)
- Keep **build artifacts and caches** out of version control

---

## 2. File Menu Operations

### Standard Operations and Shortcuts

Research across Godot, Unity, Unreal, CryEngine, and GameMaker confirms these as the universal standard shortcuts:

| Operation | Shortcut | Notes |
|-----------|----------|-------|
| New Scene | Ctrl+N | Creates empty scene with default root node |
| Open Scene | Ctrl+O | Opens file dialog filtered to `.scene` files |
| Save Scene | Ctrl+S | Saves to current path; if untitled, behaves as Save As |
| Save Scene As | Ctrl+Shift+S | Always opens Save dialog for new path |
| Save All | (no universal standard) | Some engines use Ctrl+Shift+Alt+S |
| Close Scene | Ctrl+W | Closes current scene tab |
| Quit | Ctrl+Q or Alt+F4 | Exit the editor |
| Undo | Ctrl+Z | Undo last action |
| Redo | Ctrl+Y or Ctrl+Shift+Z | Redo last undone action |

Sources:
- [Godot Default Editor Shortcuts](https://docs.godotengine.org/en/stable/tutorials/editor/default_key_mapping.html)
- [CryEngine Keyboard Shortcuts](https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756816/pages/44965609)
- [GameMaker Keyboard Shortcuts](https://manual.gamemaker.io/beta/en/IDE_Navigation/Keyboard_Shortcuts.htm)
- [Unreal Engine Keyboard Shortcuts](https://www.unrealengine.com/en-US/tech-blog/designer-s-guide-to-unreal-engine-keyboard-shortcuts)

### Unsaved Changes Dialog Pattern

When the user attempts an action that would discard unsaved changes (New Scene, Open Scene, Close Scene, Quit), show a modal dialog.

**UX best practices from research:**
- Use **clear action verbs** on buttons, not "Yes/No/Cancel" -- users should understand the consequence without reading the dialog body
- The dialog should identify **which scene** has unsaved changes
- Standard three-button pattern:

```
┌─────────────────────────────────────────────┐
│  Unsaved Changes                            │
│                                             │
│  Scene "temple_interior.scene" has unsaved  │
│  changes. What would you like to do?        │
│                                             │
│  [Don't Save]         [Cancel]    [Save]    │
└─────────────────────────────────────────────┘
```

- **Save**: Save the scene, then proceed with the original action
- **Don't Save**: Discard changes, proceed with the original action
- **Cancel**: Abort the original action entirely, return to editing
- Place the destructive option ("Don't Save") on the far left, away from "Save"
- The **title bar asterisk** convention (e.g., `*temple_interior.scene - Vestige`) provides continuous visual feedback of unsaved state

Sources:
- [Communicating Unsaved Changes - Cloudscape Design](https://cloudscape.design/patterns/general/unsaved-changes/)
- [Cancel vs Close - Nielsen Norman Group](https://www.nngroup.com/articles/cancel-vs-close/)
- [Scene Management in Godot - DeepWiki](https://deepwiki.com/godotengine/godot/8.5-scene-management)

---

## 3. Recent Files List

### Storage Location

Following platform conventions:

| Platform | Location | Standard |
|----------|----------|----------|
| Linux | `$XDG_CONFIG_HOME/vestige/settings.json` (defaults to `$HOME/.config/vestige/settings.json`) | XDG Base Directory Spec |
| Windows | `%APPDATA%\Vestige\settings.json` | Windows AppData convention |

This is consistent with how Godot stores editor settings (`$HOME/.config/godot/editor_settings-4.x.tres` on Linux, `%APPDATA%\Godot\editor_settings-4.tres` on Windows).

Sources:
- [XDG Base Directory - ArchWiki](https://wiki.archlinux.org/title/XDG_Base_Directory)
- [XDG Base Directory Specification](https://xdgbasedirectoryspecification.com/)
- [PCGamingWiki: Game Data Glossary](https://www.pcgamingwiki.com/wiki/Glossary:Game_data)
- [Linux Game Shipping Guide - Best Practices](https://gradyvuckovic.gitlab.io/linux-game-shipping-guide/2-general-advice/best-practices/)

### Number of Entries

Research findings on MRU list sizes:
- Windows system MRU stores up to **25 items**
- Microsoft Office allows configuring between **1-50**, defaults vary by app
- Keyboard accelerators work for items **1-9** (Alt+1 through Alt+9)
- Most users interact primarily with the **first 5-10** items

**Recommendation: Store 10 entries.** This balances utility with menu readability. Matches the keyboard-accessible range. Can be increased later if needed.

Sources:
- [Windows MRU Registry - winreg-kb](https://winreg-kb.readthedocs.io/en/latest/sources/explorer-keys/Most-recently-used.html)
- [Specifying MRU Files - Microsoft Excel Tips](https://excelribbon.tips.net/T006238_Specifying_the_Number_of_MRU_Files.html)

### Handling Deleted/Moved Files

Research shows that MRU entries can persist long after files are deleted. The recommended approach:

1. **Validate on display**: Before rendering the Recent Files submenu, check `std::filesystem::exists()` for each entry
2. **Visual distinction**: Show missing files grayed out with a "(missing)" suffix, or remove them silently
3. **Lazy cleanup**: Remove invalid entries when the user clicks them (show a brief "File not found" message) or on each editor launch
4. **Never block startup**: Validation should be fast and non-blocking; use `std::filesystem::exists()` which is cheap

**Recommendation**: Gray out missing files in the menu. If the user clicks a missing file, show a toast notification "File not found: [path]" and offer to remove it from the list.

### Cross-Platform Path Storage

Store **absolute paths** in the recent files list (since these are machine-specific settings that are never committed to VCS). Use `std::filesystem::path` for manipulation, but serialize as UTF-8 strings with forward slashes for consistency. Convert to native path format on use.

Sources:
- [UWP: Track Recently Used Files - Microsoft Learn](https://learn.microsoft.com/en-us/windows/uwp/files/how-to-track-recently-used-files-and-folders)

### JSON Format Example

```json
{
    "recent_projects": [
        {
            "path": "/home/user/projects/temple/project.vestige",
            "last_opened": "2026-03-20T14:30:00Z",
            "name": "Temple of Solomon"
        }
    ],
    "recent_scenes": [
        {
            "project_path": "/home/user/projects/temple/project.vestige",
            "scene_path": "scenes/holy_of_holies.scene",
            "last_opened": "2026-03-20T14:35:00Z"
        }
    ]
}
```

---

## 4. Asset Directory Monitoring / File Watching

### Platform-Specific APIs

| Platform | Native API | Mechanism |
|----------|-----------|-----------|
| Linux | **inotify** (kernel 2.6.13+) | Kernel-level file system event notifications |
| Windows | **ReadDirectoryChangesW** / I/O Completion Ports | Win32 API for directory change monitoring |
| macOS | **FSEvents** or **kqueue** | CoreServices framework / BSD-level events |

### Cross-Platform Libraries

#### efsw (Entropia File System Watcher) -- RECOMMENDED
- **License**: MIT
- **Platforms**: Linux (inotify), Windows (IOCP), macOS (FSEvents/kqueue), FreeBSD (kqueue), generic polling fallback
- **Features**: Asynchronous monitoring, recursive directory watching, UTF-8 paths, no dependencies
- **Event types**: Add, Delete, Modified, Moved
- **Fallback**: Automatically falls back to generic polling if native backends fail
- **Limitations**:
  - Rapid modifications during copy operations may fire duplicate events -- application-level debouncing needed
  - Symlinks not followed on Windows or macOS FSEvents
  - kqueue hits file descriptor limits (~10,240 on macOS) and falls back to polling
- **Build integration**: CMake support available, also in vcpkg and Conan

```cpp
// efsw usage example
class AssetWatchListener : public efsw::FileWatchListener
{
    void handleFileAction(efsw::WatchID watchid,
                          const std::string& dir,
                          const std::string& filename,
                          efsw::Action action,
                          std::string oldFilename) override
    {
        // Queue event for main thread processing
    }
};

efsw::FileWatcher fileWatcher;
AssetWatchListener listener;
efsw::WatchID watchID = fileWatcher.addWatch("assets/", &listener, true); // true = recursive
fileWatcher.watch(); // Starts watching in a background thread
```

Sources:
- [efsw GitHub Repository](https://github.com/SpartanJ/efsw)
- [fswatch GitHub Repository](https://github.com/emcrisostomo/fswatch)
- [Sane C++ Libraries: File System Watcher](https://pagghiu.github.io/SaneCppLibraries/library_file_system_watcher.html)

#### Alternative: fswatch/libfswatch
- Multiple backends (FSEvents, kqueue, inotify, Windows, stat-based)
- C, C++, and Go bindings
- More complex API than efsw

#### Alternative: Polling
- Simplest approach: periodically scan directory tree and compare modification times
- No platform dependencies
- Higher latency (depends on poll interval) and CPU cost for large trees
- Suitable as a fallback or for small asset directories

### Debouncing / Event Coalescing

File system events are noisy. A single "Save" in an external editor can generate multiple events (write, truncate, write, close). Copying a large file generates a stream of Modified events. The solution is **debouncing**: collect events over a short window, then process the batch.

**Recommended debounce strategy:**
1. When an event arrives, record the file path and event type in a pending map
2. Reset a timer to **300ms** (common range: 150-500ms; 300ms balances responsiveness with coalescing)
3. When the timer fires with no new events, process the batch:
   - Coalesce multiple Modified events for the same file into one
   - If a file was Added then Deleted within the window, ignore both
   - If a file was Deleted then Added, treat as Modified
4. Process events on the **main thread** (efsw callbacks come from a background thread)

```
Timeline:
  t=0ms    Modified "texture.png"
  t=5ms    Modified "texture.png"   (coalesced with above)
  t=10ms   Modified "texture.png"   (coalesced with above)
  t=310ms  [timer fires] -> Process: Modified "texture.png" (once)
```

Sources:
- [File Watcher Debouncing in Rust - OneUptime](https://oneuptime.com/blog/post/2026-01-25-file-watcher-debouncing-rust/view)
- [Zola Feature Request: Configurable Debounce Timer](https://github.com/getzola/zola/issues/2937)
- [Bun Hot Reload Debounce Issue](https://github.com/oven-sh/bun/issues/2987)

---

## 5. Dirty Flag Pattern

### Core Concept

The dirty flag pattern tracks whether derived/saved data is out of sync with the current in-memory state. In an editor context, the "primary data" is the scene in memory, and the "derived data" is the file on disk.

As described in *Game Programming Patterns* by Robert Nystrom: the pattern defers expensive work (in this case, saving) until the result is actually needed, and a boolean flag tracks whether the saved state is stale.

### Implementation: Version Counter Approach

Rather than a simple boolean, use a **version counter** that integrates with the undo/redo system. This is how Godot implements it:

```
m_saveVersion = 0     // Version at last save
m_currentVersion = 0  // Current undo history version

On any undoable action:
    m_currentVersion++

On save:
    m_saveVersion = m_currentVersion

isDirty():
    return m_currentVersion != m_saveVersion
```

**Key advantage**: If the user makes a change (version 1), then undoes it (version goes conceptually back), the scene is clean again. With a simple boolean, you cannot detect this -- the flag stays dirty. With version counting tied to the undo stack, undoing back to the saved version correctly reports clean state.

**How Godot does it**: Every undoable action bumps the history version. Saving records the version at save time. A mismatch between `EditorUndoRedoManager::is_history_unsaved(id)` and the saved version means unsaved changes. Each open scene maintains its own independent undo history.

### Visual Indicators

- **Title bar**: Prepend asterisk when dirty: `*scene_name.scene - Vestige`
- **Scene tab**: Show a dot or asterisk on the tab for scenes with unsaved changes
- **File menu**: Gray out "Save" when the scene is clean (no changes to save)

### Interaction with Undo/Redo

The command pattern (see Section 5 research) maintains a list of executed commands with a "current" pointer:

```
[Cmd1] [Cmd2] [Cmd3] [Cmd4]
                       ^
                    current
```

- **Undo**: Move pointer back, call `undo()` on the command
- **Redo**: Move pointer forward, call `redo()` on the command
- **New command after undo**: Discard all commands after the pointer, append the new command
- **Dirty tracking**: Record the pointer position at save time. If the pointer ever returns to that position, the scene is clean.

**Edge case**: If the user undoes past the save point, then executes a new command that happens to produce the same state, the version counter will still report dirty. This is acceptable -- the alternative (deep comparison) is too expensive.

Sources:
- [Dirty Flag - Game Programming Patterns](https://gameprogrammingpatterns.com/dirty-flag.html)
- [Command Pattern - Game Programming Patterns](https://gameprogrammingpatterns.com/command.html)
- [Godot Scene Management - DeepWiki](https://deepwiki.com/godotengine/godot/8.5-scene-management)
- [C++ Undo/Redo Frameworks - Meld Studio](https://meldstudio.co/blog/c-undo-redo-frameworks-part-1/)
- [Custom Editor Undo/Redo System - GameDev.net](https://www.gamedev.net/forums/topic/678496-custom-editor-undoredo-system/)
- [How We Implement Undo - Wolfire Games](https://www.wolfire.com/blog/2009/02/how-we-implement-undo/)
- [Undo/Redo in Game Programming - Medium](https://medium.com/@xainulabideen600/undo-redo-in-game-programming-using-command-pattern-d49eba152ca3)

---

## 6. Cross-Platform Path Handling

### The Problem

Projects must be portable between machines (and potentially between Linux and Windows). File paths differ across platforms:
- Linux: `/home/user/projects/temple/assets/texture.png`
- Windows: `C:\Users\user\projects\temple\assets\texture.png`
- Path separators: `/` (Linux/macOS) vs `\` (Windows)
- Case sensitivity: Linux is case-sensitive, Windows is not

### Solution: std::filesystem::path (C++17)

The C++17 `<filesystem>` library provides platform-agnostic path manipulation:

- `std::filesystem::path` -- models a path with automatic separator handling
- `std::filesystem::relative(path, base)` -- compute a relative path from base to path
- `std::filesystem::canonical(path)` -- resolve symlinks and normalize `.`/`..`
- `std::filesystem::weakly_canonical(path)` -- like canonical but allows non-existent tail components
- `path.generic_string()` -- returns path with forward slashes on all platforms

**Important**: Windows accepts forward slashes in paths, so storing paths with `/` separators works on both platforms.

For older compilers or platforms where `std::filesystem` is incomplete, the **ghc::filesystem** library provides a drop-in replacement header that implements the C++17 API for C++11/14/17.

Sources:
- [std::filesystem::path - cppreference.com](https://en.cppreference.com/w/cpp/filesystem/path.html)
- [std::filesystem::relative - cppreference.com](https://en.cppreference.com/w/cpp/filesystem/relative.html)
- [C++17 Filesystem Library Guide - TheLinuxCode](https://thelinuxcode.com/file-system-library-in-c17-practical-guide-for-reliable-cross-platform-file-operations/)
- [ghc::filesystem - GitHub](https://github.com/gulrak/filesystem)
- [cwalk Path Library](https://likle.github.io/cwalk/)

### Path Strategy for Vestige

| Context | Path Type | Format | Example |
|---------|----------|--------|---------|
| Inside project files (`.vestige`, `.scene`) | Relative to project root | Forward slashes | `assets/textures/brick.png` |
| Recent files list (global settings) | Absolute | Native platform format | `/home/user/projects/temple/project.vestige` |
| Runtime asset loading | Resolved absolute | `std::filesystem::path` | Computed at load time from project root + relative path |
| Display to user | Abbreviated | Platform-native | `~/projects/temple/...` or `C:\Users\...` |

**Conversion functions needed:**
```cpp
// Convert absolute path to project-relative path for storage
std::string toProjectRelative(const std::filesystem::path& absolutePath,
                              const std::filesystem::path& projectRoot);

// Resolve project-relative path to absolute path for loading
std::filesystem::path resolveProjectPath(const std::string& relativePath,
                                         const std::filesystem::path& projectRoot);
```

---

## 7. File Dialogs

### Library Comparison

| Library | Type | License | Platforms | Dependencies | GLFW Integration | Dialog Style |
|---------|------|---------|-----------|-------------|-----------------|-------------|
| **nativefiledialog-extended (NFDe)** | C library with C++ wrapper | Zlib | Win32, Cocoa, GTK/Portal (Linux) | GTK3 or DBus (Linux) | Direct support via `nfd_glfw3.h` | Native OS dialogs |
| **tinyfiledialogs** | Single C/C++ file | Zlib | Windows, macOS, Linux (Zenity/KDialog) | None (uses OS tools) | Compatible (no init needed) | Native OS dialogs |
| **portable-file-dialogs** | Header-only C++11 | WTFPL | Windows, macOS, Linux (Zenity/KDialog) | None (uses OS tools) | Compatible | Native OS dialogs |
| **ImGuiFileDialog** | ImGui widget | MIT | All (renders via ImGui) | Dear ImGui | N/A (is ImGui) | ImGui-styled (not native) |

### Detailed Analysis

#### nativefiledialog-extended (NFDe) -- RECOMMENDED
- **Best GLFW integration**: Provides `NFD_GetNativeWindowFromGLFWWindow()` to properly parent dialogs to the GLFW window
- **UTF-8 throughout**: Consistent encoding on all platforms
- **Smart extension handling**: Automatically appends extensions based on selected filter
- **Friendly filter names**: `"C/C++ Source files (*.c;*.cpp)"` style display
- **Modern CMake**: `add_subdirectory()` + `target_link_libraries(MyProgram PRIVATE nfd)`
- **Linux options**: GTK3 backend (default, requires `libgtk-3-dev`) or Portal backend (requires `libdbus-1-dev`, works with any desktop environment)
- **Active maintenance**: Extended version by btzy, based on mlabbe's original

```cpp
// NFDe Open Dialog example
NFD_Init();

nfdu8filteritem_t filters[1] = { {"Scene Files", "scene"} };
nfdopendialogu8args_t args = {0};
args.filterList = filters;
args.filterCount = 1;

nfdu8char_t* outPath = nullptr;
nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);

if (result == NFD_OKAY)
{
    std::string path(outPath);
    NFD_FreePathU8(outPath);
    // Use path...
}

NFD_Quit();
```

#### tinyfiledialogs
- **Zero dependencies**: Single `.c` + `.h` file, no library linking needed
- **More than dialogs**: Also provides message boxes, input dialogs, color picker, notifications, beep
- **Linux**: Uses zenity, matedialog, qarma, or kdialog -- whatever is installed
- **Drawback**: No GLFW parent window support (dialogs float independently)
- **Drawback**: On Linux, spawns external processes (zenity/kdialog) rather than using library APIs

#### portable-file-dialogs
- **Header-only C++11**: Single header, no build system integration needed
- **Asynchronous support**: Dialogs can run non-blocking
- **Linux**: Same external-process approach as tinyfiledialogs (zenity/kdialog)
- **Drawback**: No GLFW parent window support

#### ImGuiFileDialog
- **Pure ImGui**: Renders entirely within the ImGui context, no native dialogs
- **Rich features**: Bookmarks, custom panes, file styling with icons/colors, regex filters
- **Consistent look**: Matches the rest of the ImGui-based editor
- **Drawback**: Does not look like a native OS dialog -- may feel unfamiliar
- **Drawback**: Must implement OS filesystem access yourself (dirent or std::filesystem)

### Recommendation

**Use nativefiledialog-extended (NFDe)** for file operations (Open, Save, Save As, Select Folder). It provides the best integration with GLFW, proper UTF-8 handling, and native OS dialog appearance. Users expect native file dialogs for these operations.

**Optionally add ImGuiFileDialog** later for in-editor asset browsing (the asset browser panel), where the ImGui-styled appearance is appropriate and the rich filtering/preview features are valuable.

For the "unsaved changes" and other confirmation dialogs, **use ImGui's own modal popup system** (`ImGui::OpenPopup` / `ImGui::BeginPopupModal`) since these are simple dialogs that should match the editor's look.

Sources:
- [nativefiledialog-extended - GitHub](https://github.com/btzy/nativefiledialog-extended)
- [tinyfiledialogs - SourceForge](https://sourceforge.net/projects/tinyfiledialogs/)
- [portable-file-dialogs - GitHub](https://github.com/samhocevar/portable-file-dialogs)
- [ImGuiFileDialog - GitHub](https://github.com/aiekick/ImGuiFileDialog)
- [Tiny file dialogs - GLFW Forum](https://discourse.glfw.org/t/tiny-file-dialogs-more-natives-boxes-cross-platform-c-c/133)

---

## Summary of Recommendations

| Topic | Recommendation | Rationale |
|-------|---------------|-----------|
| Project file format | JSON (`.vestige` project, `.scene` scenes) | Human-readable, easy to parse with nlohmann/json or similar, good VCS diffs |
| Asset referencing | Relative paths from project root | Simple, portable, avoids GUID complexity at this stage |
| File dialogs | nativefiledialog-extended (NFDe) | Best GLFW integration, native look, UTF-8, Zlib license |
| Confirmation dialogs | ImGui modal popups | Consistent with editor UI, no external dependency |
| File watching | efsw | MIT license, cross-platform, async, recursive, fallback polling |
| Debounce interval | 300ms | Balances responsiveness with event coalescing |
| Recent files count | 10 entries | Covers common use, keyboard-accessible range |
| Recent files storage | XDG config dir (Linux) / AppData (Windows) | Platform convention compliance |
| Path handling | std::filesystem::path (C++17) | Standard library, cross-platform, already using C++17 |
| Dirty tracking | Version counter tied to undo/redo stack | Correctly detects "undo back to saved state = clean" |
| Keyboard shortcuts | Ctrl+N/O/S/Shift+S/Z/Y/W/Q | Industry standard across all major editors |
