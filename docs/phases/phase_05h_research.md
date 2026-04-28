# Phase 5H Research: Performance Profiling & Dashboard System

**Date:** 2026-03-21
**Engine:** Vestige (C++17, OpenGL 4.5, GLFW, GLM, ImGui)
**Hardware Target:** AMD RX 6600 (RDNA2), 60 FPS minimum
**Purpose:** Comprehensive research for implementing a full-featured performance profiling and dashboard system in the Vestige editor.

---

## Table of Contents

1. [GPU Profiling in OpenGL](#1-gpu-profiling-in-opengl)
2. [CPU Frame Profiling](#2-cpu-frame-profiling)
3. [Real-Time Performance Overlays](#3-real-time-performance-overlays)
4. [ImGui Profiling Widgets](#4-imgui-profiling-widgets)
5. [Memory Tracking](#5-memory-tracking)
6. [Draw Call Analysis](#6-draw-call-analysis)
7. [LOD Tuning Panel](#7-lod-tuning-panel)
8. [Performance Budgets](#8-performance-budgets)
9. [Recommendations for Vestige](#9-recommendations-for-vestige)

---

## 1. GPU Profiling in OpenGL

### 1.1 Timer Queries: GL_TIME_ELAPSED vs GL_TIMESTAMP

OpenGL provides two mechanisms for measuring GPU execution time via `ARB_timer_query` (core since OpenGL 3.3, well within Vestige's 4.5 target).

**GL_TIME_ELAPSED (begin/end style):**
```cpp
glBeginQuery(GL_TIME_ELAPSED, queryId);
// ... draw calls for one pass ...
glEndQuery(GL_TIME_ELAPSED);
```
- Measures elapsed GPU nanoseconds between begin and end.
- **Cannot be nested** -- only one GL_TIME_ELAPSED query can be active at a time.
- Cannot overlap with other GL_TIME_ELAPSED queries.
- Simpler API but severely limited for multi-pass profiling.

**GL_TIMESTAMP (timestamp style -- preferred for Vestige):**
```cpp
glQueryCounter(queryStartId, GL_TIMESTAMP);
// ... draw calls ...
glQueryCounter(queryEndId, GL_TIMESTAMP);
// Elapsed = end - start
```
- Records absolute GPU timestamps (nanoseconds) at specific points.
- Multiple timestamps can be recorded without nesting restrictions.
- Better for measuring multiple passes because you place timestamps between passes and subtract.
- `glQueryCounter` causes the GL to record the current time after all previous commands have reached the GL server but have not yet necessarily executed.

### 1.2 Multi-Pass Profiling Strategy

Vestige's render pipeline has distinct passes: shadow (cascaded + point), geometry, SSAO, bloom, tonemapping, TAA. Each pass boundary is a natural timestamp point:

```
timestamp[FRAME_START]
  shadow pass (CSM + point lights)
timestamp[SHADOW_END]
  geometry pass (deferred G-buffer)
timestamp[GEOMETRY_END]
  SSAO pass
timestamp[SSAO_END]
  bloom pass (downsample + upsample chain)
timestamp[BLOOM_END]
  tonemapping + composite
timestamp[TONEMAP_END]
  TAA resolve
timestamp[TAA_END]
  UI / ImGui pass
timestamp[FRAME_END]
```

Per-pass GPU time is computed as the difference between consecutive timestamps. This gives a clear breakdown of where GPU time is spent each frame.

### 1.3 Avoiding Pipeline Stalls with Double-Buffered Queries

The GPU executes commands asynchronously. Calling `glGetQueryObjectui64v` with `GL_QUERY_RESULT` immediately after issuing the query causes the CPU to stall until the GPU finishes -- completely defeating the purpose of profiling.

**Double-buffering pattern (industry standard):**
1. Maintain two sets of query objects (set A and set B).
2. Each frame, issue queries into one set and read results from the other.
3. This introduces a 1-frame latency in results but eliminates stalls entirely.
4. With double-buffering, previous frame results are guaranteed to be ready, so checking `GL_QUERY_RESULT_AVAILABLE` is usually unnecessary.

**Nathan Reed's enum-based pattern:** Define an enum of timestamp points in the frame (e.g., `GTS_ShadowPass`, `GTS_GeometryPass`, `GTS_SSAO`, etc.), create 2 sets of query objects, and average deltas over half-second intervals for stable display.

**Triple-buffering variant:** For additional safety (e.g., when VSync might cause frame pacing irregularities), a third set can be added. This guarantees results are available even if one frame takes unusually long. The overhead of extra query objects is negligible.

### 1.4 Pipeline Statistics Queries

`ARB_pipeline_statistics_query` (core in OpenGL 4.6, available as extension on 4.5) provides counters for pipeline stages:

| Query Target | What It Counts |
|---|---|
| `GL_VERTICES_SUBMITTED_ARB` | Total vertices via draw commands |
| `GL_PRIMITIVES_SUBMITTED_ARB` | Total triangles/lines/points submitted |
| `GL_VERTEX_SHADER_INVOCATIONS_ARB` | Vertex shader executions |
| `GL_FRAGMENT_SHADER_INVOCATIONS_ARB` | Fragment shader executions |
| `GL_COMPUTE_SHADER_INVOCATIONS_ARB` | Compute shader work groups |
| `GL_CLIPPING_INPUT_PRIMITIVES_ARB` | Primitives entering clipping |
| `GL_CLIPPING_OUTPUT_PRIMITIVES_ARB` | Primitives after clip splitting |

**Caveats:**
- Values are approximate -- the spec states implementations might return slightly different results.
- Cannot nest queries of the same type.
- Same double-buffering concerns as timer queries.
- Driver support varies: check `GL_ARB_pipeline_statistics_query` availability at startup. On AMD Mesa, this extension is generally available.

**Most useful for Vestige:**
- `GL_FRAGMENT_SHADER_INVOCATIONS_ARB` -- overdraw indicator (compare against screen pixel count).
- `GL_VERTICES_SUBMITTED_ARB` -- total vertex throughput per pass.
- `GL_PRIMITIVES_SUBMITTED_ARB` -- correlates with scene complexity.

### 1.5 Debug Groups for External Profilers

OpenGL 4.3 `KHR_debug` provides debug groups that are recognized by Nsight Graphics, RenderDoc, and other capture tools:

```cpp
glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Shadow Pass");
// ... shadow rendering ...
glPopDebugGroup();

glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Geometry Pass");
// ... geometry rendering ...
glPopDebugGroup();
```

**Best practices (per NVIDIA):**
- Enable debug groups in all builds (including release) -- under 200 markers per frame results in no significant CPU overhead.
- Prefer shallow layouts over deeply nested hierarchies. Complex nesting increases analysis complexity and overhead without measurably adding value.
- Debug groups help external tools identify, examine, and profile sub-frame processing sections.

### 1.6 Query Pool Management

For a small engine like Vestige with ~8-10 render passes, the number of queries is modest. A practical approach:

```cpp
struct GpuTimerPool
{
    static constexpr int MAX_TIMESTAMPS = 16;
    static constexpr int BUFFER_COUNT = 2;  // double-buffered

    GLuint queries[BUFFER_COUNT][MAX_TIMESTAMPS];
    int currentBuffer = 0;
    int timestampCount = 0;
};
```

At frame start, swap buffers. Read results from the previous buffer. Issue new timestamps into the current buffer. Total queries: ~32 objects, negligible memory.

### Sources
- [Lighthouse3d: OpenGL Timer Query Tutorial](https://www.lighthouse3d.com/tutorials/opengl-timer-query/)
- [Nathan Reed: GPU Profiling 101](https://www.reedbeta.com/blog/gpu-profiling-101/)
- [NVIDIA: GPU Performance Events Best Practices](https://developer.nvidia.com/blog/best-practices-gpu-performance-events/)
- [Khronos: ARB_pipeline_statistics_query Spec](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_pipeline_statistics_query.txt)
- [Khronos: glQueryCounter Reference](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glQueryCounter.xhtml)
- [Khronos OpenGL Wiki: Query Object](https://www.khronos.org/opengl/wiki/Query_Object)
- [NVIDIA ARB_timer_query Spec](https://developer.download.nvidia.com/opengl/specs/GL_ARB_timer_query.txt)
- [Magnum: PipelineStatisticsQuery class](https://doc.magnum.graphics/magnum/classMagnum_1_1GL_1_1PipelineStatisticsQuery.html)
- [Wunk: GPU Debug Scopes](https://wunkolo.github.io/post/2024/09/gpu-debug-scopes/)
- [LearnOpenGL: Debugging](https://learnopengl.com/In-Practice/Debugging)
- [Funto/OpenGL-Timestamp-Profiler](https://github.com/Funto/OpenGL-Timestamp-Profiler)
- [4rknova: Triple Buffering in Rendering APIs](https://www.4rknova.com/blog/2025/09/12/triple-buffering)
- [Best Practices for Modern OpenGL](https://juandiegomontoya.github.io/modern_opengl.html)

---

## 2. CPU Frame Profiling

### 2.1 RAII Scope-Based Profiling

The dominant pattern across game engines is an RAII scope timer -- a macro that creates a local object whose constructor records the start time and whose destructor records the end time:

```cpp
// Usage:
void Renderer::renderScene(...)
{
    VESTIGE_PROFILE_SCOPE("Renderer::renderScene");
    {
        VESTIGE_PROFILE_SCOPE("Shadow Pass");
        renderShadowPass(...);
    }
    {
        VESTIGE_PROFILE_SCOPE("Geometry Pass");
        renderGeometryPass(...);
    }
}
```

Benefits of RAII:
- No manual start/stop calls.
- No cleanup code needed.
- Correct timing even with early returns or exceptions.
- Nesting naturally builds a hierarchical tree of call timings.

### 2.2 Hierarchical Tree Structure

Parent-child relationships are tracked via a thread-local stack:
1. When a scope is entered, push it onto the stack. Its parent is the previous top.
2. When a scope exits, pop it. Accumulate elapsed time into the node.
3. Result: a tree like `Frame -> renderScene -> Shadow Pass -> ...`.

**Storage approaches:**

| Approach | Pros | Cons |
|---|---|---|
| **Flat array with parent indices** | Cache-friendly, simple, low overhead, ~500 lines total | Requires reconstruction for display |
| **Explicit tree nodes** | Natural for deep hierarchies | More memory, pointer chasing |
| **Per-thread flat arrays** | Zero contention (thread_local) | Need to merge for display |

The flat array approach (used by the SFEX profiler) is ideal for small engines. Each entry stores: name, start time, end time, parent index, depth level. The array is walked linearly for display.

### 2.3 Implementation Data Structure

```cpp
struct ProfileEntry
{
    const char* name;       // String literal, no allocation
    double startTime;       // std::chrono::high_resolution_clock
    double endTime;
    int parentIndex;        // -1 for root
    int depth;              // Nesting level
};

// Per-thread storage
thread_local std::vector<ProfileEntry> t_profileEntries;
thread_local int t_currentParent = -1;
```

The RAII guard:
```cpp
struct ProfileScope
{
    int m_index;

    ProfileScope(const char* name)
    {
        m_index = static_cast<int>(t_profileEntries.size());
        auto& entry = t_profileEntries.emplace_back();
        entry.name = name;
        entry.startTime = getTimeNow();
        entry.parentIndex = t_currentParent;
        entry.depth = (t_currentParent >= 0)
            ? t_profileEntries[t_currentParent].depth + 1 : 0;
        t_currentParent = m_index;
    }

    ~ProfileScope()
    {
        t_profileEntries[m_index].endTime = getTimeNow();
        t_currentParent = t_profileEntries[m_index].parentIndex;
    }
};

#define VESTIGE_PROFILE_SCOPE(name) \
    ProfileScope _profileScope##__LINE__(name)
```

### 2.4 Flame Graph / Timeline Visualization

A flame graph displays nested scopes as horizontal stacked bars:
- X-axis = time within the frame.
- Y-axis = call depth (deeper nesting = higher bars).
- Bar width = proportional to duration.
- Color = scope category or depth-based hue.

This is the same visualization used by Chrome DevTools, Tracy, and Unreal Insights. It makes it immediately obvious which scope is consuming the most time and how the call hierarchy looks.

For ImGui, this can be rendered using `ImDrawList::AddRectFilled` to draw colored rectangles at computed positions. The bwrsandman imgui-flame-graph widget provides a ready-made `PlotFlame` function with an API similar to ImGui's `PlotEx`.

### 2.5 Notable Open-Source Profiler Implementations

**SFEX Profiler (~500 lines, by Vittorio Romeo):**
- RAII `SFEX_PROFILE_SCOPE("name")` macro.
- Flat array storage, reconstructed into hierarchy for ImGui display.
- Uses `ImGui::BeginTable` / `ImGui::TableNextColumn` for column layout.
- Uses `ImGui::TreeNodeEx` for collapsible hierarchy.
- Color-coded progress bars proportional to time taken.
- Built for CppCon 2025 keynote demo, proof it works at scale.

**Tracy Profiler (full-featured external tool):**
- `ZoneScoped` / `ZoneScopedN("name")` macros for CPU.
- `TracyGpuContext` + `TracyGpuZone("name")` for OpenGL GPU profiling.
- `TracyGpuCollect` called after swap buffers to gather GPU results.
- `FrameMark` macro after swap buffers to slice execution into frame-sized chunks.
- Nanosecond resolution, separate viewer via TCP connection.
- Profiles CPU, GPU, and memory. Ships in production at scale.

**MicroProfile (embeddable, by Jonas Meyer / Roblox):**
- Ships in Roblox client and editor for all platforms.
- Supports CPU and GPU regions (D3D11/D3D12/GL/VK) with timestamp synchronization.
- Built-in web server: point Chrome to `host:1338/` for live view.
- Captures are self-contained HTML files that can be saved and shared.
- Counters for global values that change over time (memory, entity count, etc.).
- Graphing any region or counter in real-time.

**rprof (scope-based, by RudjiGames):**
- Scope-based CPU profiling with threshold filtering -- only displays frames exceeding a time threshold.
- ImGui visualization with interactive zooming and panning.
- Browser-based inspector built with Emscripten for QA pipeline integration.
- Captured profiles can be saved to binary files for offline inspection.

**Wildfire Games (0 A.D. / Pyrogenesis):**
- `PROFILE2("name")` macro for scoped profiling.
- `PROFILE2_IFSPIKE` for conditional recording only on slow frames.
- `PROFILE2_ATTR` to attach printf-style metadata to profiling regions.
- Color-coded: white = C++, red = script, making bottleneck source obvious.
- Built-in web server serving profiling data to HTML/JS viewer.

### Sources
- [Vittorio Romeo: Building a lightweight ImGui profiler in ~500 lines of C++](https://vittorioromeo.com/index/blog/sfex_profiler.html)
- [Tracy Profiler (wolfpld/tracy)](https://github.com/wolfpld/tracy)
- [Tracy DeepWiki](https://deepwiki.com/wolfpld/tracy)
- [MicroProfile (jonasmr/microprofile)](https://github.com/jonasmr/microprofile)
- [MicroProfile (zeux/microprofile)](https://github.com/zeux/microprofile)
- [RudjiGames/rprof](https://github.com/RudjiGames/rprof)
- [Wildfire Games: EngineProfiling](https://trac.wildfiregames.com/wiki/EngineProfiling)
- [Wildfire Games: Profiler2](https://trac.wildfiregames.com/wiki/Profiler2)
- [Riot Games: Profiling Measurement and Analysis](https://technology.riotgames.com/news/profiling-measurement-and-analysis)
- [Compaile/ctrack](https://github.com/Compaile/ctrack)
- [Google/orbit](https://github.com/google/orbit)
- [bombomby/optick](https://github.com/bombomby/optick)
- [O3DE: CPU Profiling Support](https://www.docs.o3de.org/docs/user-guide/profiling/cpu_profiling/)

---

## 3. Real-Time Performance Overlays

### 3.1 Unreal Engine

Unreal's stat system is the gold standard for console-like overlays:

- **`stat fps`** -- Simple FPS counter and frame time.
- **`stat unit`** -- Breaks frame time into: Frame, Game (CPU game logic), Draw (CPU render thread), GPU, RHIT (render hardware interface thread), Swap. This is the key insight: showing CPU vs GPU time to identify the bottleneck.
- **`stat unitgraph`** -- Same data as `stat unit` but as a real-time scrolling graph.
- **`stat gpu`** -- Splits GPU time by render pass: EarlyZPass, BasePass, Translucency, ShadowDepths, PostProcessing, etc.
- **`stat scenerendering`** -- Detailed render pipeline breakdown.
- **Unreal Insights** -- Standalone profiling tool with timeline/flame-graph views.

**Design pattern:** Hierarchical categories -- top-level (FPS, frame time) -> thread breakdown (game, draw, GPU) -> per-system breakdown (passes, subsystems).

### 3.2 Unity

- **CPU Usage Profiler** -- Divides time into color-coded categories: Rendering (green), Scripts (blue), Physics (orange), GarbageCollector, VSync, Animation, AI, Audio, Particles, Networking, Loading.
- **GPU Usage Profiler** -- Hierarchical breakdown of GPU time by pass.
- **Memory Profiler** -- Total committed memory broken down by subsystem.
- **Frame Debugger** -- Step through draw calls one by one.

**Design pattern:** Color-coded stacked area chart. Users see at a glance which category dominates each frame. Categories are consistent across the entire profiler UI.

### 3.3 Godot Engine

- **Performance class** -- Built-in monitors: FPS, frametime, process time, physics time, navigation time.
- **Debugger > Monitors panel** -- Subtabs for Script, Rendering, Audio, Physics with per-category metrics. Graphs show evolution over time.
- **Custom performance monitors** -- Any subsystem can register a named metric via `Performance.add_custom_monitor()` that automatically appears in the profiler UI. This is an elegant pattern for extensibility.
- **Video RAM tab** -- Memory usage for textures, materials, shaders, meshes.
- **Debug Menu addon** -- Displays FPS, frametime, CPU/GPU time graphs with best/worst/average summaries over the last 150 rendered frames.

**Design pattern:** Extensible monitor registration system. New subsystems plug into the profiler UI without modifying the profiler code.

### 3.4 Valve Source Engine (Showbudget)

The Source engine's `+showbudget` panel is a particularly relevant reference for Vestige because it is an in-engine overlay (not an external tool):

- Bar graph showing each engine system's time consumption per frame.
- Categories include: Swap Buffers (fillrate), Static Prop Rendering, Other Model Rendering, Brush Rendering, Displacement Rendering, World Rendering, Overlay Rendering, Particle/Effect Rendering, Client/Server Think, Sound, VGUI, Game/Physics/Sound.
- Longer bars = more time consumed.
- Horizontal budget lines at 16.67ms (60 FPS) and 33.33ms (30 FPS).
- Categories are color-coded for quick identification.
- The "Unaccounted" bar captures costs not in any specific category.

**Design pattern:** Simple horizontal bar graph per system with a budget line. Immediately shows what is over budget.

### 3.5 Common Patterns Across All Engines

1. **Hierarchical categories:** Top-level frame time -> CPU vs GPU -> per-system breakdown.
2. **Color-coded stacked graphs:** Each category gets a distinct, consistent color.
3. **Rolling history:** 120-300 frames of history shown as scrolling graphs.
4. **Budget lines:** Horizontal lines at target frame time (16.67ms for 60 FPS).
5. **Min/Max/Avg display:** Statistical summary alongside live values.
6. **Togglable detail levels:** Simple overlay (FPS only) -> medium (category breakdown) -> full (hierarchical tree with flame graph).
7. **Conditional rendering:** Only collect detailed metrics when the profiler panel is visible.

### Sources
- [Intel: Unreal Engine Optimization Profiling Fundamentals](https://www.intel.com/content/www/us/en/developer/articles/technical/unreal-engine-optimization-profiling-fundamentals.html)
- [AMD GPUOpen: Unreal Engine Performance Guide](https://gpuopen.com/learn/unreal-engine-performance-guide/)
- [Unreal Art Optimization: Measuring Performance](https://unrealartoptimization.github.io/book/process/measuring-performance/)
- [Unity: Best practices for profiling](https://unity.com/how-to/best-practices-for-profiling-game-performance)
- [Unity: CPU Usage Profiler module](https://docs.unity3d.com/Manual/ProfilerCPU.html)
- [Godot: Performance class docs](https://docs.godotengine.org/en/stable/classes/class_performance.html)
- [Godot: Custom performance monitors](https://docs.godotengine.org/en/stable/tutorials/scripting/debug/custom_performance_monitors.html)
- [Godot Debug Menu addon](https://github.com/godot-extended-libraries/godot-debug-menu)
- [The Shaggy Dev: Custom Monitoring in Godot 4](https://shaggydev.com/2025/09/25/godot-custom-monitoring/)
- [Valve Developer Community: Showbudget](https://developer.valvesoftware.com/wiki/Showbudget)
- [Valve Developer Community: Budget](https://developer.valvesoftware.com/wiki/Budget)
- [Valve Developer Union: The Showbudget Panel](https://valvedev.info/guides/the-showbudget-panel-and-optimizing-source-levels/)

---

## 4. ImGui Profiling Widgets

### 4.1 Built-in ImGui Plotting

ImGui provides basic plotting out of the box:

- **`ImGui::PlotLines`** -- Line graph from a float array. Supports labels, overlay text, min/max scale, and configurable size. Accepts a `values_getter` callback for reading from circular buffers without copying.
- **`ImGui::PlotHistogram`** -- Bar chart from a float array. Same API as PlotLines.

Both are lightweight and sufficient for frame time graphs, GPU pass timing bars, and other simple time-series data.

**Ring buffer pattern for frame history:**
```cpp
struct RingBuffer
{
    static constexpr int CAPACITY = 300;  // ~5 seconds at 60 FPS
    float data[CAPACITY] = {};
    int offset = 0;

    void push(float value)
    {
        data[offset] = value;
        offset = (offset + 1) % CAPACITY;
    }
};
```

ImGui's `PlotLines` accepts a `values_offset` parameter that tells it where the "start" is in a circular buffer, making this pattern zero-copy.

### 4.2 Flame Graph Widgets

**bwrsandman/imgui-flame-graph:**
- Single header + source file (`imgui_widget_flamegraph.h/.cpp`).
- `PlotFlame` function with API similar to `ImGui::PlotEx`.
- Horizontal stacked bars representing nested scopes.
- Bar width proportional to duration, color-coded by depth or custom coloring.
- MIT license. Drop-in integration.

**CheapMeow/SimpleImGuiFlameGraph:**
- Less than 100 lines of code total.
- Uses scope timers to measure time, simple draw function for flame graph.
- More minimal than bwrsandman's version but functional.

**Custom flame graph using ImDrawList:**
```cpp
ImDrawList* drawList = ImGui::GetWindowDrawList();
for (const auto& entry : profileEntries)
{
    float x0 = frameStartX + (entry.startTime / frameDuration) * frameWidth;
    float x1 = frameStartX + (entry.endTime / frameDuration) * frameWidth;
    float y0 = baseY - entry.depth * barHeight;
    float y1 = y0 + barHeight;

    ImU32 color = depthColors[entry.depth % colorCount];
    drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), color);
    // Optionally add text label if bar is wide enough
    if (x1 - x0 > minLabelWidth)
        drawList->AddText(ImVec2(x0 + 2, y0 + 1), textColor, entry.name);
}
```

### 4.3 Hierarchical Table Display

The SFEX profiler pattern for hierarchical data in ImGui:

```cpp
ImGui::BeginTable("Profiler", 3, ImGuiTableFlags_BordersInner | ImGuiTableFlags_Sortable);
ImGui::TableSetupColumn("Name");
ImGui::TableSetupColumn("Time (ms)");
ImGui::TableSetupColumn("% of Parent");
ImGui::TableHeadersRow();

for (const auto& entry : profileEntries)
{
    if (entry.parentIndex != expectedParent) continue;
    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    bool open = ImGui::TreeNodeEx(entry.name,
        ImGuiTreeNodeFlags_SpanAllColumns);
    ImGui::TableNextColumn();
    ImGui::Text("%.3f", entry.durationMs);
    ImGui::TableNextColumn();
    ImGui::ProgressBar(entry.durationMs / parentDurationMs);

    if (open)
    {
        // Recurse for children
        renderChildren(entry.index);
        ImGui::TreePop();
    }
}
ImGui::EndTable();
```

This gives a collapsible tree with sortable columns -- Name, Time, % of Parent -- with color-coded progress bars.

### 4.4 Sparklines

Sparklines (tiny inline graphs) are effective for showing trends alongside metric tables:

- Miniature `PlotLines` calls with small fixed size (e.g., 80x20 pixels).
- Place inline in a table column next to the numeric value.
- Each metric gets its own ring buffer for sparkline history.
- ImPlot provides a dedicated `Sparkline` function in its demo.
- Custom draw using `ImDrawList` for maximum control over appearance.

### 4.5 Intel MetricsGui Library

A purpose-built ImGui extension for performance metrics (archived, MIT license):

- `MetricsGuiMetric` -- Named metric with units and formatting.
- `MetricsGuiPlot` -- Collection of metrics with visualizations.
- `DrawList()` -- Compact current-value display with optional inline sparklines.
- `DrawHistory()` -- Full temporal graph with line or bar style.
- `mRangeDampening` (default 0.95) for smooth Y-axis scaling.
- `mStacked` option for stacked area charts.
- Useful as a reference even though the project is archived.

### 4.6 LegitProfiler

A simple ImGui component for rendering profiler data:

- `ProfilerGraph` class renders one profiling histogram with legend (CPU or GPU).
- `ProfilersWindow` is a window with 2 graphs for CPU and GPU data respectively.
- MIT license, ~138 stars on GitHub.
- Lightweight, single-purpose, no external dependencies.

### 4.7 InAppGpuProfiler (aiekick)

An embedded GPU profiler specifically for Dear ImGui applications:

- `IAGPNewFrame` called once per frame.
- `IAGPScoped` macro for measuring specific GPU operations.
- `IAGPCollect` to gather measurements.
- Breadcrumb navigation for profiling results.
- Follows ImGui master and docking branches.
- Requires an OpenGL loader (GLAD compatible).
- Most directly relevant to Vestige's architecture.

### Sources
- [bwrsandman/imgui-flame-graph](https://github.com/bwrsandman/imgui-flame-graph)
- [CheapMeow/SimpleImGuiFlameGraph](https://github.com/CheapMeow/SimpleImGuiFlameGraph)
- [Raikiri/LegitProfiler](https://github.com/Raikiri/LegitProfiler)
- [aiekick/InAppGpuProfiler](https://github.com/aiekick/InAppGpuProfiler)
- [GameTechDev/MetricsGui](https://github.com/GameTechDev/MetricsGui)
- [epezent/ImPlot](https://github.com/epezent/implot)
- [study-game-engines/imgui-profiler-component](https://github.com/study-game-engines/imgui-profiler-component)
- [simco50/TimelineProfiler](https://github.com/simco50/TimelineProfiler)
- [ImGui Issue #2859: Flame Graph Widget](https://github.com/ocornut/imgui/issues/2859)
- [ImGui Discussion #4138: Accurate framerate](https://github.com/ocornut/imgui/discussions/4138)
- [Adam Sawicki: An Idea for Visualization of Frame Times](https://asawicki.info/news_1758_an_idea_for_visualization_of_frame_times)

---

## 5. Memory Tracking

### 5.1 CPU Memory: Global Operator Override

The simplest approach for tracking heap allocations:

```cpp
static std::atomic<size_t> s_totalAllocated{0};
static std::atomic<size_t> s_totalFreed{0};
static std::atomic<size_t> s_allocationCount{0};
static std::atomic<size_t> s_frameAllocations{0};

void* operator new(size_t size)
{
    s_totalAllocated += size;
    s_allocationCount++;
    s_frameAllocations++;
    return malloc(size);
}

void operator delete(void* ptr, size_t size) noexcept
{
    s_totalFreed += size;
    free(ptr);
}
```

This gives:
- **Current heap usage:** `s_totalAllocated - s_totalFreed`.
- **Allocations per frame:** Reset `s_frameAllocations` each frame. Important for spotting allocation spikes that cause GC-like stalls.
- **Peak usage:** Track high-water mark.
- **Leak detection:** If `s_totalAllocated != s_totalFreed` at shutdown.

**Limitations:**
- `operator delete(void*, size_t)` with size is C++14; without the size parameter, you need to store size in a header before the allocation.
- Does not track third-party libraries using `malloc` directly.
- Thread safety requires atomic counters (shown above) or thread-local accumulators merged periodically.
- Atomic operations have measurable overhead under high contention. Thread-local counters merged once per frame are faster for multi-threaded engines.

### 5.2 CPU Memory: Per-Subsystem Tracking

The Bitsquid/Stingray engine pattern provides more granular insight:
- Custom allocator base class with `allocate(size, align)` and `deallocate(ptr)`.
- Each subsystem (renderer, physics, audio, scene) gets its own allocator instance.
- Allocators track their own totals, enabling per-subsystem memory reporting.
- A "proxy allocator" wraps another allocator to add tracking without changing behavior.

For Vestige, a simpler variant: tag allocations by subsystem using a thread-local "current subsystem" enum, then bucket allocations in per-subsystem counters.

### 5.3 Frame Allocators

Game engines commonly use frame-based linear allocators for temporary per-frame data:
- A stack/linear allocator that gets reset every frame.
- Zero fragmentation, O(1) allocation cost, zero deallocation cost.
- Perfect for per-frame scratch data (visibility lists, sort keys, command buffers).
- The profiler dashboard should track frame allocator usage vs capacity.

### 5.4 GPU Memory: AMD sysfs Interface

OpenGL has no standard API for querying GPU memory. However, on Linux with AMD GPUs, the `amdgpu` kernel driver exposes memory information via sysfs:

| sysfs File | Description |
|---|---|
| `mem_info_vram_total` | Total VRAM in bytes |
| `mem_info_vram_used` | Currently used VRAM in bytes |
| `mem_info_vis_vram_total` | Total visible (CPU-accessible) VRAM |
| `mem_info_vis_vram_used` | Currently used visible VRAM |
| `mem_info_gtt_total` | Total GTT (system memory mapped for GPU) |
| `mem_info_gtt_used` | Currently used GTT |

**Location:** `/sys/class/drm/card0/device/mem_info_vram_used` (card number may vary).

**Reading from C++:**
```cpp
size_t readSysfsValue(const char* path)
{
    std::ifstream file(path);
    size_t value = 0;
    file >> value;
    return value;
}

size_t vramUsed = readSysfsValue(
    "/sys/class/drm/card0/device/mem_info_vram_used");
size_t vramTotal = readSysfsValue(
    "/sys/class/drm/card0/device/mem_info_vram_total");
```

This should be read infrequently (every 30-60 frames) since it involves filesystem I/O, but gives accurate system-wide VRAM usage on AMD hardware.

### 5.5 GPU Memory: OpenGL Extensions

**GL_ATI_meminfo (AMD, legacy):**
```cpp
GLint texFreeMemInfo[4];
glGetIntegerv(GL_TEXTURE_FREE_MEMORY_ATI, texFreeMemInfo);
// [0] = total free in pool (KB), [1] = largest free block (KB)
```
Also provides `GL_VBO_FREE_MEMORY_ATI` and `GL_RENDERBUFFER_FREE_MEMORY_ATI`. However, AMD has reportedly dropped this extension from modern Mesa/AMDGPU drivers. Availability on RX 6600 needs runtime testing.

**GL_NVX_gpu_memory_info (NVIDIA only):**
Not applicable for Vestige's AMD target but worth supporting for portability.

### 5.6 Manual GPU Resource Tracking

Track allocations by summing known resource sizes:
- **Textures:** width * height * bytes_per_pixel * mip_levels (compute mip chain size as `4/3 * base_size` for power-of-two textures).
- **Vertex/index buffers:** exact byte size from `glBufferData` calls.
- **Framebuffer attachments:** resolution * format_bytes * MSAA_samples.
- **Shadow maps:** resolution^2 * depth_format_bytes * cascade_count.

Wrap `glBufferData`, `glTexImage2D`, and similar calls to accumulate tracked sizes. This won't match driver totals (drivers add overhead for page tables, alignment, internal copies) but gives a useful estimate and per-category breakdown.

### Sources
- [Linux Kernel: Misc AMDGPU driver information](https://docs.kernel.org/gpu/amdgpu/driver-misc.html)
- [Geeks3D: GPU Memory Size in OpenGL](https://www.geeks3d.com/20100531/programming-tips-how-to-know-the-graphics-memory-size-and-usage-in-opengl/)
- [Khronos: GL_ATI_meminfo Spec](https://registry.khronos.org/OpenGL/extensions/ATI/ATI_meminfo.txt)
- [Khronos Forums: GPU memory check utility functions](https://community.khronos.org/t/gpu-memory-check-utility-functions/63885)
- [Bitsquid: Custom Memory Allocation in C++](http://bitsquid.blogspot.com/2010/09/custom-memory-allocation-in-c.html)
- [Gamedeveloper.com: Writing a Game Engine -- Part 2: Memory](https://www.gamedeveloper.com/programming/writing-a-game-engine-from-scratch---part-2-memory)
- [Palos Publishing: Custom Allocators for Game Engines](https://palospublishing.com/managing-c-memory-for-game-engines-with-custom-allocators/)
- [Palos Publishing: Memory Management Techniques](https://palospublishing.com/memory-management-techniques-for-real-time-game-engines-in-c/)
- [Isetta Engine: Memory](https://isetta.io/compendium/Memory/)
- [ArchWiki: AMDGPU](https://wiki.archlinux.org/title/AMDGPU)
- [propelrc.com: GPU VRAM Usage on Linux](https://www.propelrc.com/gpu-vram-usage-on-linux/)

---

## 6. Draw Call Analysis

### 6.1 Draw Call Metrics

The profiler dashboard should track and display:

| Metric | Description | Why It Matters |
|---|---|---|
| **Total draw calls** | All `glDraw*` calls per frame | CPU overhead scales with draw call count |
| **Instanced batches** | `glDrawElementsInstanced` calls | Shows batching effectiveness |
| **Instance count** | Total instances drawn via instancing | High instance count with low batch count = good |
| **Triangles rendered** | Total triangle count after culling | Scene complexity indicator |
| **Vertices submitted** | Total vertex throughput | Bandwidth indicator |
| **State changes** | Shader/texture/material switches | Each switch has CPU cost |
| **Per-pass draw calls** | Draw calls broken down by render pass | Shows which pass is most expensive on CPU |

Vestige already tracks draw calls, instanced batches, and culled items via `CullingStats`. The dashboard should extend this with per-pass breakdowns and historical trends.

### 6.2 Batching Efficiency Metrics

Key ratios for evaluating batching quality:

- **Batch efficiency:** `instances_drawn / draw_calls`. Higher = better. If you are drawing 1000 trees with 1000 draw calls, efficiency is 1.0x. With GPU instancing, 1000 trees in 5 batches = 200x efficiency.
- **Triangles per draw call:** Average triangles per draw. Low values (< 100) suggest many small objects not being batched. Efficient batching yields thousands of triangles per call.
- **State change ratio:** `state_changes / draw_calls`. Close to 1.0 = every draw call changes state (bad). Close to 0.0 = draws are well-sorted by state (good).

Display these as ratios with color coding: green (good), yellow (warning), red (poor).

### 6.3 Overdraw Visualization

Overdraw occurs when multiple fragments are written to the same pixel. High overdraw wastes GPU fragment processing bandwidth.

**Technique 1: Stencil Buffer Counting**

The classic approach uses the stencil buffer to count how many times each pixel is written:

1. Clear stencil buffer to 0.
2. Enable stencil testing with `glStencilFunc(GL_ALWAYS, 0, 0xFF)`.
3. Set stencil ops to increment on every fragment: `glStencilOp(GL_KEEP, GL_INCR, GL_INCR)`. This increments the stencil value for every fragment regardless of depth test result.
4. Render the entire scene normally.
5. In a second pass, read back the stencil buffer and visualize it as a color ramp:
   - 1 draw = dark blue (no overdraw).
   - 2 draws = green.
   - 3 draws = yellow.
   - 4+ draws = red/white (severe overdraw).

The stencil buffer can be read via `glReadPixels(x, y, w, h, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, data)` for quantitative analysis, or rendered as a fullscreen quad using stencil test to mask different stencil values.

**Technique 2: Additive Blending with Flat Color**

A simpler visual approach:
1. Render all geometry with a simple flat shader outputting a low-alpha color (e.g., `vec4(0.1, 0.1, 0.1, 0.1)`).
2. Enable additive blending: `glBlendFunc(GL_ONE, GL_ONE)`.
3. Disable depth writing (but keep depth testing to match actual rendering).
4. Areas with more overdraw accumulate brighter colors.

Simpler to implement but less precise than stencil counting. Note: Unity's built-in overdraw view uses additive blending without z-writing, which produces false positives on opaque objects because it does not account for per-pixel depth rejection.

**Technique 3: Fragment Shader Invocation Count (via pipeline statistics)**

Use `GL_FRAGMENT_SHADER_INVOCATIONS_ARB` query to get total fragment invocations per frame. Compare against `screenWidth * screenHeight` to get an overdraw ratio:
- Ratio 1.0 = perfect (every pixel drawn exactly once).
- Ratio 2.0 = average 2x overdraw.
- Ratio 3.0+ = significant overdraw, investigate.

This is a whole-frame metric. Per-pass fragment counts give more granular insight.

### 6.4 Shader Complexity Visualization

Shader complexity heatmaps show how computationally expensive each pixel is to shade.

**How Unreal Engine does it:**
- The Shader Complexity view mode colors each pixel based on the total number of shader instructions. The color scale goes from green (very inexpensive) through red (expensive) to pink/white (very expensive).
- Total complexity = instructions for emissive + instructions per lighting pass + translucency + distortion + fog.
- Colors are defined in `BaseEngine.ini` and linearly interpolated based on instruction count.

**Limitation:** Instruction count alone is not fully accurate. A shader with 16 texture lookups is much slower than one with 16 ALU instructions. But as a quick visual diagnostic, it is very useful.

**Implementation approach for Vestige:**
1. Compile a variant of each shader that outputs a color based on instruction count (this can be a preprocessor define that replaces the main fragment output).
2. Alternatively, use a uniform that encodes the known instruction count for each shader, and a fullscreen pass that maps the instruction count to a color ramp.
3. Or render with a special debug shader that counts dynamic branches taken (more accurate but more complex).

A simpler first pass: categorize shaders into complexity tiers (simple/medium/complex) and render objects with color-coded flat shading matching their tier.

### Sources
- [PulseGeek: Optimize Draw Calls in a Game Engine](https://pulsegeek.com/articles/optimize-draw-calls-in-a-game-engine-practical-steps/)
- [Handmade Network: Why are Lots of Small Draw Calls Bad](https://handmade.network/forums/t/7260-why_are_lots_of_small_draw_calls_bad,_and_how_does_batching_help)
- [TheGamedev.Guru: Unity Overdraw Optimization](https://thegamedev.guru/unity-gpu-performance/overdraw-optimization/)
- [GameDev.net: How to Stop the Spread of Overdraw](https://www.gamedev.net/tutorials/programming/graphics/how-to-stop-the-spread-of-overdraw-before-it-kills-your-game-r5371/)
- [Rusty Blog: Calculating Overdraw with Stencil Buffer](https://russellday.wordpress.com/2012/01/26/calculating-overdraw-with-the-stencil-buffer-in-opengl/)
- [Excamera: Visualizing overdraw via stencil](https://www.excamera.com/articles/2/)
- [Unreal Art Optimization: Optimization View Modes](https://unrealartoptimization.github.io/book/profiling/view-modes/)
- [Kseniia Shestakova: Shader Complexity and Optimisation](https://kseniia-shestakova.medium.com/materials-compilation-shader-complexity-and-optimisation-f60be9a9357a)
- [Arm Developer: Profile and Visualize Shader Complexity](https://developer.arm.com/documentation/102676/0100/Profile-and-visualize-shader-complexity)
- [Unity: Draw Call Batching](https://docs.unity3d.com/Manual/DrawCallBatching.html)
- [TheGamedev.Guru: Unity Draw Call Batching Guide 2026](https://thegamedev.guru/unity-performance/draw-call-optimization/)
- [Khronos: Batching for the masses (SIGGRAPH 2013)](https://www.khronos.org/assets/uploads/developers/library/2013-siggraph-opengl-bof/Batch-and-Cull-in-OpenGL-BOF_SIGGRAPH-2013.pdf)

---

## 7. LOD Tuning Panel

### 7.1 How Major Engines Handle LOD Editing

**Unreal Engine:**
- LOD transitions are controlled by **screen size** (fraction of viewport the object occupies), not raw distance.
- The Static Mesh Editor Details Panel has "LOD Settings" with:
  - "Auto Compute LOD Distances" checkbox (disable for manual control).
  - Per-LOD "Screen Size" slider that sets the transition threshold.
  - Current Screen Size displayed in the viewport, helping set appropriate thresholds.
  - LOD Picker with "Custom" checkbox to view all LODs simultaneously.
  - Per-platform LOD overrides (e.g., mobile gets aggressive LOD).
- LOD transitions can use **dithered opacity masking** (stochastic cross-dissolve between LOD levels) to avoid popping. When combined with TAA, the dithering noise is resolved into smooth transitions.

**Unity:**
- LOD Group component on game objects.
- Visual slider showing LOD transition percentages (0-100% of screen).
- Camera distance preview showing effective LOD at current camera position.
- LOD levels indexed from 0 (highest detail) upward.

**Godot:**
- LOD settings accessible in the inspector per mesh.
- Performance monitors can be registered for tracking LOD statistics.

### 7.2 LOD Tuning Panel Design for Vestige

A dedicated LOD tuning panel in the editor should provide:

**Distance/Screen-Size Sliders:**
- One row per LOD level (LOD0, LOD1, LOD2, Cull).
- Slider for the transition threshold (either distance or screen-size percentage).
- Numeric input for precise values.
- Visual indicator showing the current object's screen size based on camera position.

**Live Preview:**
- When an object is selected, the viewport shows which LOD level is currently active.
- Option to force a specific LOD level for inspection.
- Wireframe overlay toggle showing triangle density at each LOD level.
- Side-by-side comparison mode: show LOD0 and current LOD simultaneously.

**LOD Statistics:**
- Triangle count per LOD level.
- Memory savings (vertex/index buffer size reduction per LOD).
- Transition distance visualization: colored rings on the ground plane showing LOD switch boundaries.
- Frame-level stats: how many objects are at each LOD level right now.

**LOD Transition Quality:**
- Dithering/crossfade toggle for smooth transitions.
- Dithering uses a screen-space noise pattern to dissolve between LODs. During the transition zone, fragments from both LODs are randomly selected based on a threshold that ramps over the transition distance.
- Preview mode that slowly moves the camera to show the transition in action.

### 7.3 Screen-Size vs Distance-Based LOD

| Approach | Pros | Cons |
|---|---|---|
| **Distance-based** | Simple to understand, easy to implement | Does not account for object size or FOV changes |
| **Screen-size-based** | Accounts for object size, FOV, and resolution; visually consistent | Slightly more complex to compute |

Unreal uses screen-size because it produces consistent visual results regardless of object scale or FOV. The screen size is computed as:

```
screenSize = objectBoundingSphereRadius / (distance * tan(fov/2))
```

For Vestige, screen-size-based LOD is recommended. The tuning panel should display both the raw distance and the computed screen size percentage.

### 7.4 Debug Visualization Modes

The LOD panel should integrate with debug rendering modes:

- **LOD Level Colorization:** Render each object with a flat color based on its current LOD level. LOD0 = green, LOD1 = yellow, LOD2 = orange, culled = red. This provides instant visual feedback about LOD distribution.
- **Wireframe Overlay:** Show triangle wireframes to make LOD density differences visible.
- **Transition Bands:** Draw colored rings or bands on the ground showing where LOD transitions occur relative to the camera.
- **Triangle Count Overlay:** Display the triangle count above each object in the viewport.

### Sources
- [Unreal Engine 5.7: Optimizing LOD Screen Size Per-Platform](https://dev.epicgames.com/documentation/en-us/unreal-engine/optimizing-lod-screen-size-per-platform-in-unreal-engine)
- [Unreal Engine 4.27: Per-Platform LOD Screen Size](https://docs.unrealengine.com/4.27/en-US/WorkingWithContent/Types/StaticMeshes/HowTo/PerPlatformLODScreenSize/)
- [Unreal Engine: Creating and Using LODs](https://docs.unrealengine.com/4.26/en-US/WorkingWithContent/Types/StaticMeshes/HowTo/LODs)
- [Unreal Engine: Get Lod Screen Sizes API](https://docs.unrealengine.com/4.26/en-US/BlueprintAPI/EditorScripting/StaticMesh/GetLodScreenSizes/)
- [Unreal Engine: Viewport Modes](https://docs.unrealengine.com/en-US/BuildingWorlds/LevelEditor/Viewports/ViewModes/index.html)
- [Unity: Introduction to Level of Detail](https://docs.unity3d.com/Manual/LevelOfDetail.html)
- [Sloyd.ai: Mastering Level of Detail](https://www.sloyd.ai/blog/mastering-level-of-detail-lod-balancing-graphics-and-performance-in-game-development)
- [Wikipedia: Level of Detail](https://en.wikipedia.org/wiki/Level_of_detail_(computer_graphics))
- [Cesium: Smoother LOD Transitions with Dithered Opacity Masking](https://cesium.com/blog/2022/10/20/smoother-lod-transitions-in-cesium-for-unreal/)
- [Couch Learn: Fading Between LODs in UE4](https://couchlearn.com/fading-between-lods-in-unreal-engine-4/)
- [NVIDIA: Implementing Stochastic LOD](https://developer.nvidia.com/blog/implementing-stochastic-lod-with-microsoft-dxr/)

---

## 8. Performance Budgets

### 8.1 Frame Budget Fundamentals

At 60 FPS, each frame must complete in **16.67 milliseconds** or less. This is the total budget for all CPU work (game logic, physics, audio, rendering submission) and all GPU work (shadow passes, geometry, post-processing).

Key insight: CPU and GPU execute in parallel. The frame time is limited by whichever is slower:
- **GPU-bound:** CPU finishes early, waits for GPU. Optimize shaders, reduce overdraw, simplify geometry.
- **CPU-bound:** GPU finishes early, waits for draw calls / game logic. Optimize algorithms, batch draw calls, reduce allocations.

### 8.2 Typical Budget Allocations

There is no universal standard -- allocations depend heavily on the game type. However, several sources provide reference points:

**General purpose engine at 60 FPS (16.67ms total):**

| System | Budget | Notes |
|---|---|---|
| **Rendering (GPU)** | 8-10 ms | Shadow passes, G-buffer, post-processing |
| **Rendering (CPU)** | 2-3 ms | Culling, draw call submission, state management |
| **Physics** | 1-2 ms | Collision detection, simulation step |
| **Animation** | 0.5-1 ms | Skeletal animation, blending, IK |
| **AI / Scripting** | 0.5-1 ms | Pathfinding, behavior trees, scripts |
| **Audio** | 0.3-0.5 ms | Mixing, 3D spatialization |
| **Scene Management** | 0.5-1 ms | Transform updates, spatial structures |
| **UI** | 0.3-0.5 ms | ImGui / overlay rendering |
| **Contingency** | 1-2 ms | Buffer for spikes and variability |

**Naughty Dog reference (from a lighting artist's breakdown):**
- At 60 FPS with 16ms budget: ~10ms for "basic costs" (rendering, scene management, core systems), leaving ~6ms for effects (particles, reflections, post-processing).
- At 30 FPS with 33ms budget: ~10ms for basics, leaving ~23ms for effects and higher quality.

**ARM's GPU budget calculation:**
- Available cycles = shader_cores * clock_speed * 0.8 (realistic utilization).
- Required pixels = width * height * FPS * 2.5 (average overdraw factor).
- Budget per pixel = available_cycles / required_pixels.
- This determines how many shader cycles each fragment can afford.

**Valve Source Engine categories:**
- Swap Buffers (fillrate/resolution), Static Prop Rendering, Other Model Rendering, Brush Rendering, Displacement Rendering, World Rendering, Overlay Rendering, Particle/Effect Rendering, Client/Server Think, Sound, VGUI (UI).
- Budget line drawn at 16.67ms for 60 FPS target.
- "Unaccounted" bar captures costs not assigned to any category.

### 8.3 Budget Enforcement Strategies

**Soft budgets (warnings):**
- Display budget bars with color changes: green (under 50% of budget), yellow (50-80%), orange (80-100%), red (over budget).
- Log warnings when a system exceeds its budget for N consecutive frames.
- Show "over budget by X ms" in the dashboard.

**Hard budgets (throttling):**
- Some engines enforce budgets by throttling systems. For example:
  - AI: only update N agents per frame, spread others across frames.
  - Physics: cap substep count or reduce simulation frequency.
  - Particles: cap particle count, reduce spawn rate dynamically.
  - LOD: force more aggressive LOD when frame budget is tight.
- This is more advanced and appropriate for shipping titles.

**Budget alerts:**
- Define per-system caps (e.g., physics must stay under 2ms).
- Check in automated tests and dev builds.
- Focus optimization efforts on systems exceeding 10% of total frame time.
- CPU utilization target: keep main thread under 70% to leave headroom for spikes.

### 8.4 Budget Dashboard Design

The performance budget panel should display:

1. **Frame time bar:** Total frame time as a horizontal bar with budget line at 16.67ms.
2. **CPU vs GPU split:** Two sub-bars showing which is the bottleneck.
3. **Per-system bars:** Horizontal bars for each system with their allocated budget as a reference line.
4. **Stacked area chart:** 300-frame rolling history showing per-system time as stacked colored areas. Budget line as a horizontal reference. This is the format Unity and Unreal use.
5. **Budget compliance indicator:** Simple red/green per system showing if it is within budget.

### 8.5 Vestige-Specific Budget Allocation

For Vestige (architectural walkthrough, no AI, no physics simulation, emphasis on rendering quality):

| System | Proposed Budget | Rationale |
|---|---|---|
| **Shadow Pass (GPU)** | 2-3 ms | CSM + point light shadows |
| **Geometry Pass (GPU)** | 2-3 ms | Deferred G-buffer fill |
| **SSAO (GPU)** | 1-1.5 ms | Screen-space effect |
| **Bloom (GPU)** | 0.5-1 ms | Downsample/upsample chain |
| **Tonemapping (GPU)** | 0.3-0.5 ms | Fullscreen pass |
| **TAA (GPU)** | 0.3-0.5 ms | Temporal resolve |
| **Water/Particles (GPU)** | 1-2 ms | Surface simulation, effects |
| **CPU Culling + Submission** | 1-2 ms | Frustum/occlusion culling, draw calls |
| **Scene Management (CPU)** | 0.5-1 ms | Transform updates, spatial structures |
| **Audio (CPU)** | 0.3 ms | Ambient soundscapes |
| **Editor/UI (CPU+GPU)** | 0.5-1 ms | ImGui panels |
| **Contingency** | 1-2 ms | Frame time variability buffer |
| **Total** | ~13-16 ms | Within 16.67ms budget |

### Sources
- [Generalist Programmer: Game Optimization Complete Performance Guide 2025](https://generalistprogrammer.com/tutorials/game-optimization-complete-performance-guide-2025)
- [Generalist Programmer: Game Performance Optimization Technical Guide 2025](https://generalistprogrammer.com/tutorials/game-performance-optimization-complete-technical-guide-2025)
- [PulseGeek: What Is a Frame Time Budget in Optimization?](https://pulsegeek.com/articles/what-is-a-frame-time-budget-in-optimization/)
- [PulseGeek: Game Performance Optimization Checklist](https://pulsegeek.com/articles/game-performance-optimization-a-complete-checklist/)
- [ARM: GPU Processing Budget Approach to Game Development](https://community.arm.com/developer/tools-software/graphics/b/blog/posts/gpu-processing-budget-approach-to-game-development)
- [Unity: Best Practices for Profiling Game Performance](https://unity.com/how-to/best-practices-for-profiling-game-performance)
- [Valve Developer Community: Budget](https://developer.valvesoftware.com/wiki/Budget)
- [Valve Developer Community: Showbudget](https://developer.valvesoftware.com/wiki/Showbudget)
- [Dre Dyson: Fix Game Engine Performance Bottlenecks](https://dredyson.com/fix-game-engine-performance-bottlenecks-a-senior-developers-step-by-step-guide-to-optimizing-aaa-development-pipelines-in-unreal-and-unity/)
- [Moldstud: Game Engine Optimization Tips](https://moldstud.com/articles/p-game-engine-optimization-tips-for-enhanced-performance-and-efficiency)

---

## 9. Recommendations for Vestige

### 9.1 What Vestige Already Has

- **GPU timer queries:** Basic support (needs extension to multi-pass, double-buffered).
- **CPU profiler:** Basic support (needs hierarchical scope-based profiling).
- **Console panel:** Can output performance data as text.
- **Frame diagnostics:** F11 screenshot + text report with pixel brightness analysis.
- **CullingStats:** Draw calls, instanced batches, culled items, shadow casters.

### 9.2 What Needs to Be Built

**Tier 1 -- Core Dashboard (must-have):**
1. **GPU Pass Timer:** Double-buffered `GL_TIMESTAMP` queries at each render pass boundary. Display per-pass GPU time in the dashboard.
2. **CPU Scope Profiler:** RAII macro (`VESTIGE_PROFILE_SCOPE`), flat array storage with parent indices, thread-local recording.
3. **Performance Overlay Panel:** ImGui panel with:
   - Frame time graph (PlotLines with ring buffer, 300 frames).
   - CPU vs GPU breakdown (which is the bottleneck?).
   - Per-system timing table (hierarchical, collapsible via TreeNodeEx).
   - Budget lines at 16.67ms.
   - Min/Max/Avg statistics.
4. **Draw Call Stats Table:** Extend existing CullingStats with per-pass breakdown, batch efficiency ratio, triangles per draw call.

**Tier 2 -- Enhanced Profiling (high value):**
5. **Flame Graph View:** CPU scope data rendered as horizontal stacked bars using ImDrawList. Use bwrsandman's imgui-flame-graph or build custom (~100-200 lines).
6. **GPU Pass Breakdown Bar:** Horizontal stacked bar showing shadow/geometry/SSAO/bloom/tonemap/TAA proportions, color-coded.
7. **Memory Dashboard:** CPU heap tracking (global operator override), GPU VRAM via AMD sysfs, per-category resource tracking.
8. **Budget Compliance Panel:** Per-system budget bars with green/yellow/red color coding and budget lines.
9. **Sparklines:** Tiny inline graphs next to each metric in the timing table.

**Tier 3 -- Debug Visualization Modes (nice-to-have):**
10. **Overdraw Visualization:** Stencil-based overdraw heatmap as a debug render mode.
11. **LOD Tuning Panel:** Screen-size sliders, LOD level colorization, transition preview, wireframe overlay, triangle count display.
12. **Shader Complexity View:** Color-coded heatmap based on shader instruction count tiers.
13. **Depth Buffer Visualization:** Linearized depth display for debugging Z-buffer issues.

### 9.3 Proposed Architecture

```
PerformanceProfiler (singleton, manages all profiling data)
├── GpuTimerManager
│   ├── Double-buffered GL_TIMESTAMP query pool
│   ├── Per-pass timing results (shadow, geometry, SSAO, bloom, etc.)
│   └── Pipeline statistics queries (fragment count, vertex count)
├── CpuProfiler
│   ├── Thread-local flat arrays of ProfileEntry
│   ├── VESTIGE_PROFILE_SCOPE("name") RAII macro
│   └── Frame snapshot for display (swap buffers each frame)
├── MemoryTracker
│   ├── CPU heap stats (global operator new/delete)
│   ├── GPU VRAM stats (AMD sysfs or GL extensions)
│   └── Per-category resource tracking
├── BudgetManager
│   ├── Per-system budget definitions
│   ├── Compliance checking
│   └── Alert/warning system
└── DrawCallAnalyzer
    ├── Per-pass draw call counts
    ├── Batch efficiency metrics
    └── State change tracking

PerformanceDashboardPanel (ImGui panel, reads from PerformanceProfiler)
├── Overview Tab (frame time graph, CPU/GPU split, FPS)
├── GPU Tab (per-pass timing bars, pipeline stats)
├── CPU Tab (hierarchical tree table, flame graph toggle)
├── Memory Tab (CPU heap, GPU VRAM, per-category breakdown)
├── Draw Calls Tab (stats table, batch efficiency, overdraw ratio)
├── Budget Tab (per-system bars with budget lines)
└── Debug Modes (overdraw, LOD colors, shader complexity, depth)

LODTuningPanel (ImGui panel, separate from dashboard)
├── Per-LOD distance/screen-size sliders
├── LOD level forced preview
├── Transition quality settings (dithering toggle)
└── Statistics (triangle count per LOD, memory savings)
```

### 9.4 Key Implementation Principles

1. **Measure from previous frame, display this frame.** Never stall the current frame waiting for results.
2. **Use ring buffers, not dynamic allocation.** Pre-allocate all history arrays.
3. **Smooth displayed values.** Exponential moving average or fixed-interval reporting for stable readouts.
4. **Conditional profiling.** Only collect GPU timer queries and pipeline statistics when the profiler panel is visible.
5. **Zero-allocation hot path.** No `new`/`malloc` in the profiling macros. String literals only for scope names (const char*).
6. **Amortize expensive queries.** GPU memory reads and pipeline statistics every 30-60 frames, not every frame.
7. **ImGui cost is negligible.** The library has near-zero overhead when the overlay is not visible, and is lightweight while in use.
8. **Debug groups always on.** `glPushDebugGroup`/`glPopDebugGroup` at each render pass for external profiler compatibility, even in release builds (under 200 markers = negligible overhead).

### 9.5 Dependencies

- **No new external libraries required** for Tier 1 and 2. Everything can be built with ImGui (already integrated) and OpenGL 4.5 APIs.
- **Optional:** bwrsandman/imgui-flame-graph (single header, MIT license) for flame graph if custom implementation is not preferred.
- **Optional:** Tracy integration for deep profiling sessions (Tier 3, separate tool).

### 9.6 Performance Impact of the Profiler Itself

| Component | Estimated Overhead |
|---|---|
| GPU timer queries (16 timestamps) | < 0.01 ms |
| CPU scope profiling (50-100 scopes) | < 0.05 ms |
| Memory tracking (atomic counters) | < 0.01 ms |
| ImGui dashboard rendering | 0.1-0.3 ms (when visible) |
| AMD sysfs VRAM read | 0.01-0.05 ms (every 60 frames) |
| Pipeline statistics queries | < 0.01 ms |
| **Total when active** | **< 0.5 ms** |
| **Total when hidden** | **~0 ms** |

Well within Vestige's 16.67ms frame budget.
