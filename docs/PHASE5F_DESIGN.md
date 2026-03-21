# Phase 5F: Performance Overlay, Console & Editor Utilities — Design Document

## Goal

Add developer tools for profiling, debugging, and workflow: a performance overlay with GPU/CPU timing, a fully functional console/log panel replacing the placeholder, an enhanced screenshot tool, and a keyboard shortcuts reference. These tools make the engine self-diagnosing and improve the development experience.

**Milestone:** The user can open a performance overlay showing GPU pass timings and frame time graph, view filtered color-coded logs in the console panel, execute commands to toggle render settings, take high-resolution screenshots, and look up any keyboard shortcut via a searchable reference panel.

---

## Current State (End of Phase 5E)

The editor has (assuming 5E is complete):
- **Full scene authoring:** Entities, lights, materials, particles, water surfaces
- **Scene persistence:** Save/load, undo/redo, auto-save, crash recovery
- **Rendering pipeline:** Forward PBR, shadows, SSAO, bloom, tonemap, TAA, particles, water

**What exists for 5F already:**
- `Timer` class: CPU FPS and deltaTime
- `Renderer::getCullingStats()`: draw calls, instance batches, culled items
- `FrameDiagnostics`: F11 screenshot + text report with pixel analysis
- `Logger`: 6 severity levels (trace/debug/info/warning/error/fatal) to stdout/stderr
- Placeholder console panel: just `ImGui::TextWrapped("Log output will appear here.")`
- F11 key binding for diagnostic capture

**What's missing:**
- No GPU timing (zero visibility into per-pass GPU cost)
- No frame time graph (only instantaneous FPS number)
- No CPU scope profiling
- No console log panel (logs only visible in terminal)
- No console command input
- No screenshot resolution multiplier or format options
- No keyboard shortcuts reference

---

## Research Summary

Two research documents were produced (see `docs/` folder):
- `PERFORMANCE_OVERLAY_RESEARCH.md` — GPU timer queries, engine profiler designs, ImGui widgets, CPU profiling, memory tracking, pipeline statistics
- `PHASE5F_RESEARCH.md` — Console panel implementations, ring buffer storage, command systems, logger sinks, screenshot enhancements, shortcuts panels

### Key Design Decisions from Research

| Decision | Choice | Rationale |
|----------|--------|-----------|
| GPU timing | `GL_TIMESTAMP` via `glQueryCounter` with double-buffered queries | No nesting restrictions; measures multiple passes; 1-frame latency avoids stalls |
| Frame time graph | `ImGui::PlotLines` with 300-entry ring buffer | Built-in, simple, ~5 seconds of history at 60 FPS |
| CPU profiling | RAII scope macro (`VESTIGE_PROFILE_SCOPE`) with flat array | Lightweight; ~500 lines total; no external dependency |
| Memory tracking | Global `operator new`/`delete` override with atomics | Simple; gives heap allocation count, current usage, peak |
| GPU memory | Manual tracking (sum texture/buffer sizes) | Cross-vendor; `GL_ATI_meminfo` deprecated on AMD |
| Console storage | Ring buffer of structured `LogEntry` (16K capacity) | Fixed memory, O(1) insert, no fragmentation |
| Logger integration | Callback sink system (`Logger::addSink()`) | Observer pattern; console registers as a sink |
| Console rendering | `ImGuiListClipper` + `TextColored()` per severity | Handles 100K+ entries; only renders ~20-50 visible lines |
| Console commands | `unordered_map<string, CommandDescriptor>` | Simple, no external library; extensible |
| Screenshot enhancement | Resolution multiplier via oversized FBO | RX 6600 supports `GL_MAX_TEXTURE_SIZE` 16384; 4x at 1080p |
| Screenshot format | PNG (default) + JPEG option | JPEG via `stbi_write_jpg()` (already have stb) |
| Shortcuts panel | Static data table + ImGui searchable table | Low-effort, maintainable; modal on F1 |
| Compile-time stripping | `#ifdef VESTIGE_PROFILING` guards | Zero overhead in release builds |
| When to collect GPU times | Only when performance panel is visible | No overhead in normal editing/play |

---

## Architecture Overview

Phase 5F adds five new subsystems:

```
+------------------------------------------------------------------+
|                            Editor                                 |
|                                                                   |
|  Performance System              Console System                   |
|  +---------------------------+  +------------------------------+  |
|  | GpuTimerSystem            |  | ConsolePanel                 |  |
|  | (GL_TIMESTAMP queries,    |  | (ring buffer, severity       |  |
|  |  double-buffered,         |  |  filter, text search,        |  |
|  |  per-pass timing)         |  |  auto-scroll, color-coded)   |  |
|  +---------------------------+  +------------------------------+  |
|  | CpuProfiler               |  | CommandRegistry              |  |
|  | (RAII scope macro,        |  | (command map, parse/exec,    |  |
|  |  flat array storage)      |  |  tab completion, history)    |  |
|  +---------------------------+  +------------------------------+  |
|  | FrameTimeHistory          |                                    |
|  | (ring buffer, 300 frames) |  Utility Panels                   |
|  +---------------------------+  +------------------------------+  |
|  | PerformancePanel (ImGui)  |  | ShortcutsPanel (ImGui)       |  |
|  | (graphs, tables, GPU/CPU  |  | (static table, searchable,  |  |
|  |  breakdown, culling stats)|  |  F1 toggle)                  |  |
|  +---------------------------+  +------------------------------+  |
|                                                                   |
|  Enhanced Diagnostics                                             |
|  +---------------------------+                                    |
|  | FrameDiagnostics (ext.)   |                                    |
|  | (res multiplier, JPEG,    |                                    |
|  |  viewport capture, F12)   |                                    |
|  +---------------------------+                                    |
+------------------------------------------------------------------+
```

### Integration Points

- **Logger:** Modified to support callback sinks; console panel registers as a sink
- **Renderer:** GPU timer queries placed between existing render passes; no pipeline changes
- **Engine main loop:** CPU profile scopes around major subsystems
- **Editor:** New panels registered in `drawPanels()`; View menu items to toggle visibility
- **Keyboard:** F1 = shortcuts, F12 = quick screenshot, existing F11 = diagnostics

---

## Sub-Phase Breakdown

### 5F-1: Logger Sink System & Console Panel (~500 lines)

**Goal:** Replace the placeholder console panel with a fully functional log viewer with severity filtering, text search, color-coded output, and auto-scroll.

**New/Modified Files:**
| File | Action | Purpose |
|------|--------|---------|
| `engine/core/log_entry.h` | Create | `LogEntry` struct, `LogLevel` enum, `LogRingBuffer` class |
| `engine/core/logger.h` | Modify | Add sink callback API |
| `engine/core/logger.cpp` | Modify | Dispatch to registered sinks |
| `engine/editor/panels/console_panel.h` | Create | `ConsolePanel` class |
| `engine/editor/panels/console_panel.cpp` | Create | Log display, filtering, auto-scroll |
| `engine/editor/editor.h` | Modify | Add `ConsolePanel` member |
| `engine/editor/editor.cpp` | Modify | Replace placeholder with `ConsolePanel::draw()` |
| `tests/test_log_ring_buffer.cpp` | Create | Ring buffer unit tests |

**LogEntry & Ring Buffer:**
```cpp
enum class LogLevel : uint8_t
{
    TRACE, DEBUG, INFO, WARNING, ERROR, FATAL
};

struct LogEntry
{
    LogLevel level;
    float engineTime;       // Timer::getElapsedTime()
    uint32_t frameNumber;
    std::string message;
};

class LogRingBuffer
{
public:
    explicit LogRingBuffer(int capacity = 16384);
    void push(LogEntry entry);
    int size() const;
    const LogEntry& operator[](int index) const;  // 0 = oldest
    void clear();
private:
    std::vector<LogEntry> m_entries;
    int m_head = 0;
    int m_count = 0;
};
```

**Logger Sink API:**
```cpp
// Added to Logger:
using LogSink = std::function<void(LogLevel, const std::string&)>;
static void addSink(LogSink sink);
static void removeSink(const LogSink& sink);
// Existing stdout/stderr output becomes the "default sink"
// All log calls dispatch to registered sinks after formatting
```

**Console Panel UI Layout:**
```
┌─Console──────────────────────────────────────────────────┐
│ [T] [D] [I] [W] [E] [F]  │  [Clear]  [Copy]  [⇓ Auto] │
│ Filter: [________________________]                       │
├──────────────────────────────────────────────────────────┤
│ 0.12s [INFO]  Engine initialized                        │
│ 0.12s [INFO]  OpenGL 4.6 loaded                         │
│ 0.13s [DEBUG] Window state restored: 1920x1080          │
│ 0.15s [WARN]  Texture not found: missing.png            │
│ 0.20s [ERROR] Shader compile failed: custom.frag        │
│ ...                                                      │
├──────────────────────────────────────────────────────────┤
│ > [command input here...]                                │
└──────────────────────────────────────────────────────────┘
```

**Color Scheme:**
| Level | Color | ImVec4 |
|-------|-------|--------|
| TRACE | Gray | (0.5, 0.5, 0.5, 1.0) |
| DEBUG | Light Gray | (0.7, 0.7, 0.7, 1.0) |
| INFO | Green | (0.4, 0.8, 0.4, 1.0) |
| WARNING | Yellow | (1.0, 0.8, 0.2, 1.0) |
| ERROR | Red | (1.0, 0.3, 0.3, 1.0) |
| FATAL | Bright Red | (1.0, 0.1, 0.1, 1.0) |

**Filtered View:** A `std::vector<int>` of indices into the ring buffer that pass the current severity toggles and text filter. Rebuilt when filters change; cached between frames otherwise. `ImGuiListClipper` iterates this filtered view.

**Tests:**
- Ring buffer: push, wrap-around, capacity, indexing, clear
- Filtered view: severity toggle, text filter, combined filters

---

### 5F-2: Console Command System (~400 lines)

**Goal:** Add a command input line to the console panel with built-in commands, tab completion, and history.

**New/Modified Files:**
| File | Action | Purpose |
|------|--------|---------|
| `engine/editor/console_commands.h` | Create | `CommandRegistry` class, `CommandDescriptor` |
| `engine/editor/console_commands.cpp` | Create | Built-in commands, parse/dispatch |
| `engine/editor/panels/console_panel.h` | Modify | Add command input, history navigation |
| `engine/editor/panels/console_panel.cpp` | Modify | InputText with callbacks |
| `tests/test_console_commands.cpp` | Create | Command parsing/execution tests |

**CommandRegistry:**
```cpp
struct CommandDescriptor
{
    std::string name;
    std::string description;
    std::string usage;          // e.g., "set_exposure <float>"
    std::function<void(const std::vector<std::string>& args)> execute;
};

class CommandRegistry
{
public:
    void registerCommand(CommandDescriptor desc);
    bool execute(const std::string& input);     // parse and dispatch
    std::vector<std::string> getCompletions(const std::string& prefix) const;
    const std::vector<CommandDescriptor>& getAllCommands() const;
private:
    std::unordered_map<std::string, CommandDescriptor> m_commands;
};
```

**Built-in Commands (Phase 1):**
| Command | Args | Description |
|---------|------|-------------|
| `help` | `[command]` | List all commands or show help for one |
| `clear` | — | Clear the log |
| `history` | — | Show command history |
| `set_exposure` | `<float>` | Set camera exposure value |
| `set_fov` | `<float>` | Set camera FOV |
| `wireframe` | `[on\|off]` | Toggle wireframe mode |
| `bloom` | `[on\|off]` | Toggle bloom |
| `ssao` | `[on\|off]` | Toggle SSAO |
| `pom` | `[on\|off]` | Toggle parallax occlusion mapping |
| `screenshot` | `[filename]` | Take a screenshot |
| `stats` | — | Print FPS, draw calls, memory |
| `quit` | — | Request quit |

**Input Handling:** Uses `ImGui::InputText()` with:
- `ImGuiInputTextFlags_EnterReturnsTrue` — execute on Enter
- `ImGuiInputTextFlags_CallbackCompletion` — Tab completion
- `ImGuiInputTextFlags_CallbackHistory` — Up/Down for command history

**Tests:**
- Command registration and lookup
- Parse with arguments (single arg, multiple args, quoted strings)
- Tab completion (unique match, multiple matches, no match)
- History navigation

---

### 5F-3: GPU Timer System & Frame Time History (~400 lines)

**Goal:** Measure per-pass GPU time using OpenGL timestamp queries and display a frame time history graph.

**New Files:**
| File | Purpose |
|------|---------|
| `engine/renderer/gpu_timer.h` | `GpuTimerSystem` class — double-buffered timestamp queries |
| `engine/renderer/gpu_timer.cpp` | Query creation, timestamp placement, result retrieval |
| `engine/core/frame_time_history.h` | Ring buffer of frame time samples (CPU + GPU + per-pass) |

**GpuTimerSystem:**
```cpp
class GpuTimerSystem
{
public:
    // Timestamp points in the frame
    enum class TimestampPoint
    {
        FRAME_START,
        SHADOW_END,
        GEOMETRY_END,
        SSAO_END,
        BLOOM_END,
        TONEMAP_END,
        FRAME_END,
        COUNT
    };

    void init();
    void shutdown();

    /// @brief Call at the start of each frame. Swaps query buffer sets.
    void beginFrame();

    /// @brief Place a timestamp at a pipeline point.
    void timestamp(TimestampPoint point);

    /// @brief Read results from the previous frame. Call after beginFrame().
    void readResults();

    /// @brief Get elapsed ms between two consecutive timestamp points.
    float getPassTimeMs(TimestampPoint start, TimestampPoint end) const;

    /// @brief Get total GPU frame time.
    float getTotalGpuMs() const;

    /// @brief Enable/disable query collection.
    void setEnabled(bool enabled);
    bool isEnabled() const;

private:
    static constexpr int NUM_POINTS = static_cast<int>(TimestampPoint::COUNT);
    static constexpr int NUM_BUFFERS = 2;  // double-buffer

    GLuint m_queries[NUM_BUFFERS][NUM_POINTS] = {};
    GLuint64 m_results[NUM_POINTS] = {};
    int m_currentBuffer = 0;
    bool m_enabled = false;
    bool m_hasResults = false;
};
```

**FrameTimeHistory:**
```cpp
struct FrameTimeSample
{
    float cpuMs;        // total CPU frame time
    float gpuMs;        // total GPU frame time
    float shadowMs;     // GPU shadow pass
    float geometryMs;   // GPU geometry pass
    float ssaoMs;       // GPU SSAO pass
    float bloomMs;      // GPU bloom pass
    float tonemapMs;    // GPU tonemap pass
};

class FrameTimeHistory
{
public:
    static constexpr int CAPACITY = 300;  // ~5 seconds at 60 FPS

    void push(const FrameTimeSample& sample);
    const FrameTimeSample& operator[](int index) const;
    int size() const;

    // Convenience arrays for ImGui::PlotLines
    void getCpuTimes(float* out, int count) const;
    void getGpuTimes(float* out, int count) const;

private:
    FrameTimeSample m_samples[CAPACITY] = {};
    int m_head = 0;
    int m_count = 0;
};
```

**Integration into Renderer:**
```cpp
// In Renderer::render():
m_gpuTimer.beginFrame();
m_gpuTimer.readResults();

m_gpuTimer.timestamp(TimestampPoint::FRAME_START);
renderShadows();
m_gpuTimer.timestamp(TimestampPoint::SHADOW_END);
renderGeometry();
m_gpuTimer.timestamp(TimestampPoint::GEOMETRY_END);
renderSSAO();
m_gpuTimer.timestamp(TimestampPoint::SSAO_END);
renderBloom();
m_gpuTimer.timestamp(TimestampPoint::BLOOM_END);
renderTonemap();
m_gpuTimer.timestamp(TimestampPoint::TONEMAP_END);
m_gpuTimer.timestamp(TimestampPoint::FRAME_END);
```

**Tests:**
- Frame time history: push, capacity, ordering, convenience getters

---

### 5F-4: Performance Overlay Panel (~400 lines)

**Goal:** ImGui panel displaying GPU/CPU timing breakdown, frame time graph, culling stats, and render state.

**New/Modified Files:**
| File | Action | Purpose |
|------|--------|---------|
| `engine/editor/panels/performance_panel.h` | Create | `PerformancePanel` class |
| `engine/editor/panels/performance_panel.cpp` | Create | ImGui rendering of all performance data |
| `engine/editor/editor.h` | Modify | Add `PerformancePanel` member |
| `engine/editor/editor.cpp` | Modify | Add to View menu, draw in panel loop |

**Panel Layout:**
```
┌─Performance──────────────────────────────────────────────┐
│ FPS: 60  │  CPU: 8.2ms  │  GPU: 6.4ms  │  Budget: 61%  │
├──────────────────────────────────────────────────────────┤
│ Frame Time (ms)                                          │
│ 16.67 ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─  (60 FPS)     │
│     ╱╲  ╱╲     ╱╲                                       │
│ ───╱──╲╱──╲───╱──╲──────────────────── CPU              │
│     ╱╲  ╱╲     ╱╲                                       │
│ ───╱──╲╱──╲───╱──╲──────────────────── GPU              │
│  0 ─────────────────────────────────────────── 300 frames│
├──────────────────────────────────────────────────────────┤
│ GPU Pass Breakdown                                       │
│ ████████░░░░░░░░░░░░░░░░░░░░░░░░  Shadows:    2.1ms     │
│ ░░░░░░░░████████████░░░░░░░░░░░░  Geometry:   2.5ms     │
│ ░░░░░░░░░░░░░░░░░░░░████░░░░░░░░  SSAO:       0.8ms     │
│ ░░░░░░░░░░░░░░░░░░░░░░░░██░░░░░░  Bloom:      0.5ms     │
│ ░░░░░░░░░░░░░░░░░░░░░░░░░░██░░░░  Tonemap:    0.3ms     │
│                            Total:  6.4ms                 │
├──────────────────────────────────────────────────────────┤
│ Rendering                                                │
│ Draw calls:     135 (0 instanced)                        │
│ Opaque:         6/10 visible (culled 4)                  │
│ Transparent:    1/1 visible                              │
│ Shadow casters: 5/9 per cascade (avg)                    │
│ Resolution:     1783x999                                 │
│ Anti-aliasing:  MSAA 4x                                  │
└──────────────────────────────────────────────────────────┘
```

**Frame Time Graph:** Two overlaid `ImGui::PlotLines` — one for CPU, one for GPU. A horizontal dashed line at 16.67ms marks the 60 FPS budget. Colors: CPU = blue, GPU = green.

**GPU Pass Breakdown:** Horizontal stacked bar using `ImGui::GetWindowDrawList()->AddRectFilled()`. Each pass has a unique color. Hover shows exact ms value.

**Budget percentage:** `(max(cpuMs, gpuMs) / 16.67) * 100`. Shows green below 75%, yellow at 75-90%, red above 90%.

**Compile-time guards:**
```cpp
#ifdef VESTIGE_PROFILING
    #define VESTIGE_GPU_TIMESTAMP(system, point) (system).timestamp(point)
#else
    #define VESTIGE_GPU_TIMESTAMP(system, point) ((void)0)
#endif
```

---

### 5F-5: Screenshot Enhancements (~300 lines)

**Goal:** Extend the existing F11 diagnostic screenshot with a quick-capture key (F12), resolution multiplier, JPEG format option, and viewport-only capture.

**Modified Files:**
| File | Action | Purpose |
|------|--------|---------|
| `engine/renderer/frame_diagnostics.h` | Modify | Add capture options struct |
| `engine/renderer/frame_diagnostics.cpp` | Modify | Resolution multiplier FBO, JPEG output |
| `engine/core/engine.cpp` | Modify | F12 binding, Shift+F12 for settings |
| `engine/editor/editor.cpp` | Modify | Screenshot settings in View menu |

**CaptureOptions:**
```cpp
struct CaptureOptions
{
    enum class Format { PNG, JPEG };
    enum class Source { FULL_FRAMEBUFFER, VIEWPORT_ONLY };

    Format format = Format::PNG;
    Source source = Source::VIEWPORT_ONLY;
    int resolutionMultiplier = 1;    // 1, 2, or 4
    int jpegQuality = 90;           // 1-100
    bool includeDiagnostics = false; // text report alongside image
};
```

**Resolution Multiplier Implementation:**
1. Check `GL_MAX_TEXTURE_SIZE` at startup (RX 6600 = 16384)
2. Create temporary FBO at `width * multiplier × height * multiplier`
3. Render the scene to this FBO (same camera, adjusted viewport)
4. `glReadPixels` the oversized FBO
5. Save via `stbi_write_png()` or `stbi_write_jpg()`
6. Delete the temporary FBO

**Key Bindings:**
- **F11** (unchanged): Diagnostic capture (PNG + text report)
- **F12**: Quick screenshot (current settings, no text report)
- **Shift+F12**: Open screenshot settings popup

**Screenshot Settings Popup:**
```
┌─Screenshot Settings─────────────────┐
│ Format:      [PNG ▼]  Quality: [90] │
│ Source:      [Viewport ▼]           │
│ Resolution:  [1x ▼]  (1783×999)    │
│              [2x]    (3566×1998)    │
│              [4x]    (7132×3996)    │
│ [Take Screenshot]                   │
└─────────────────────────────────────┘
```

---

### 5F-6: Keyboard Shortcuts Panel (~200 lines)

**Goal:** A searchable reference panel showing all editor keyboard shortcuts, accessible via F1 or Help menu.

**New Files:**
| File | Purpose |
|------|---------|
| `engine/editor/panels/shortcuts_panel.h` | `ShortcutsPanel` class |
| `engine/editor/panels/shortcuts_panel.cpp` | Static shortcut data + ImGui table |

**Shortcut Data Structure:**
```cpp
struct ShortcutEntry
{
    const char* category;     // "Camera", "Editor", "File", "View", etc.
    const char* shortcut;     // "Ctrl+S", "W", "F11", etc.
    const char* description;  // "Save scene", "Translate mode", etc.
};

static const ShortcutEntry SHORTCUTS[] = {
    // File
    {"File", "Ctrl+N", "New scene"},
    {"File", "Ctrl+O", "Open scene"},
    {"File", "Ctrl+S", "Save scene"},
    {"File", "Ctrl+Shift+S", "Save scene as"},
    {"File", "Ctrl+Q", "Quit"},

    // Edit
    {"Edit", "Ctrl+Z", "Undo"},
    {"Edit", "Ctrl+Y", "Redo"},
    {"Edit", "Ctrl+D", "Duplicate selected"},
    {"Edit", "Delete", "Delete selected"},
    {"Edit", "Ctrl+G", "Group selected"},
    {"Edit", "F2", "Rename selected"},

    // Camera
    {"Camera", "RMB + WASD", "Fly camera"},
    {"Camera", "MMB drag", "Orbit camera"},
    {"Camera", "Shift+MMB", "Pan camera"},
    {"Camera", "Scroll", "Zoom"},
    {"Camera", "F", "Focus on selected"},

    // Transform
    {"Transform", "W", "Translate mode"},
    {"Transform", "E", "Rotate mode"},
    {"Transform", "R", "Scale mode"},
    {"Transform", "Ctrl (held)", "Snap to grid"},

    // View
    {"View", "F1", "Show shortcuts"},
    {"View", "F5", "Toggle Edit/Play mode"},
    {"View", "F11", "Diagnostic screenshot"},
    {"View", "F12", "Quick screenshot"},
    {"View", "Shift+F12", "Screenshot settings"},

    // Rendering
    {"Rendering", "G", "Toggle wireframe"},
    {"Rendering", "B", "Toggle bloom"},
};
```

**Panel Layout:**
```
┌─Keyboard Shortcuts───────────────────────────────────────┐
│ Filter: [________________________]                       │
├──────────┬──────────────┬────────────────────────────────┤
│ Category │ Shortcut     │ Description                    │
├──────────┼──────────────┼────────────────────────────────┤
│ File     │ Ctrl+S       │ Save scene                     │
│ File     │ Ctrl+O       │ Open scene                     │
│ Edit     │ Ctrl+Z       │ Undo                           │
│ Camera   │ F            │ Focus on selected              │
│ ...      │ ...          │ ...                            │
└──────────┴──────────────┴────────────────────────────────┘
```

Uses `ImGui::BeginTable("Shortcuts", 3)` with `ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg`. Filtered by `ImGuiTextFilter` searching across all three columns.

**Triggered by:** F1 key or Help > Keyboard Shortcuts menu item. Opens as a modal or dockable window.

---

## Performance Impact

| Component | Overhead | Notes |
|-----------|----------|-------|
| GPU timer queries | ~0.01ms | Negligible; only active when panel visible |
| Frame time ring buffer | ~0 | One struct push per frame |
| Console log sink | ~0 | String copy on log call; rare in hot path |
| Console rendering | ~0.1ms | ImGuiListClipper; only visible lines rendered |
| CPU profile scopes | ~0.01ms | chrono::high_resolution_clock at scope boundaries |
| Performance panel | ~0.2ms | PlotLines + DrawList rectangles |
| **Total 5F overhead** | **< 0.5ms** | Negligible impact on 60 FPS target |

---

## Accessibility Considerations

- All log text uses minimum 18px font (consistent with editor)
- Severity level tags `[INFO]`, `[WARN]`, `[ERROR]` always accompany color (not color-only)
- Frame time graph has a labeled Y-axis and budget line
- Performance numbers are displayed as text alongside visual bars
- Shortcuts panel has keyboard-navigable table with text filter
- Screenshot settings popup has keyboard-accessible controls

---

## Implementation Order

1. **5F-1:** Logger sink system + console panel (foundational — all other features log to console)
2. **5F-2:** Console command system (builds on 5F-1 panel)
3. **5F-3:** GPU timer system + frame time history (independent of console)
4. **5F-4:** Performance overlay panel (combines 5F-3 data with existing stats)
5. **5F-5:** Screenshot enhancements (independent)
6. **5F-6:** Keyboard shortcuts panel (independent, lowest priority)

Sub-phases 5F-3/5F-5/5F-6 are independent of each other and could be reordered.

---

## Sources

### Performance Profiling
- [Nathan Reed: GPU Profiling 101](https://www.reedbeta.com/blog/gpu-profiling-101/)
- [Lighthouse3d: OpenGL Timer Query](https://www.lighthouse3d.com/tutorials/opengl-timer-query/)
- [Vittorio Romeo: SFEX Profiler](https://vittorioromeo.com/index/blog/sfex_profiler.html)
- [Intel GameTechDev/MetricsGui](https://github.com/GameTechDev/MetricsGui)
- [bwrsandman/imgui-flame-graph](https://github.com/bwrsandman/imgui-flame-graph)
- [Funto/OpenGL-Timestamp-Profiler](https://github.com/Funto/OpenGL-Timestamp-Profiler)
- [Godot Performance class](https://docs.godotengine.org/en/stable/classes/class_performance.html)
- [Unity Profiling Best Practices](https://unity.com/how-to/best-practices-for-profiling-game-performance)
- [Wildfire Games Profiler2](https://trac.wildfiregames.com/wiki/Profiler2)

### Console & Logging
- [imgui_demo.cpp ExampleAppConsole](https://github.com/ocornut/imgui/blob/master/imgui_demo.cpp)
- [rmxbalanque/imgui-console](https://github.com/rmxbalanque/imgui-console)
- [Embedded Artistry: Circular Buffer in C/C++](https://embeddedartistry.com/blog/2017/05/17/creating-a-circular-buffer-in-c-and-c/)
- [ForrestTheWoods: Remote Logging Console](https://www.forrestthewoods.com/blog/writing_a_crossplatform_remote_logging_console_in_cpp/)
- [BraceYourselfGames/UE-BYGImguiLogger](https://github.com/BraceYourselfGames/UE-BYGImguiLogger)

### Screenshots
- [LearnOpenGL: Framebuffers](https://learnopengl.com/Advanced-OpenGL/Framebuffers)
- [Capture Screenshot in OpenGL (Christian Vallentin)](https://vallentin.dev/blog/post/opengl-screenshot)
- [UE5 High-Resolution Screenshots](https://www.unreal-university.blog/how-to-take-high-resolution-screenshots-in-unreal-engine-5-complete-guide/)

### Keyboard Shortcuts
- [Godot Default Key Mapping](https://docs.godotengine.org/en/stable/tutorials/editor/default_key_mapping.html)

See also: `docs/PERFORMANCE_OVERLAY_RESEARCH.md` and `docs/PHASE5F_RESEARCH.md` for full research documents with all sources.
