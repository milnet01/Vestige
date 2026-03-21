# Performance Overlay & Profiling System Research

**Date:** 2026-03-21
**Engine:** Vestige (C++17, OpenGL 4.5, ImGui, GLM)
**Purpose:** Research for implementing a real-time performance overlay and profiling panel in the Vestige editor.

---

## Table of Contents

1. [GPU Timing Queries in OpenGL](#1-gpu-timing-queries-in-opengl)
2. [Performance Overlay Designs in Major Engines](#2-performance-overlay-designs-in-major-engines)
3. [Frame Time Graphs and Histograms in ImGui](#3-frame-time-graphs-and-histograms-in-imgui)
4. [CPU Profiling Approaches for Game Engines](#4-cpu-profiling-approaches-for-game-engines)
5. [Memory Tracking in C++ Engines](#5-memory-tracking-in-c-engines)
6. [OpenGL Pipeline Statistics Queries](#6-opengl-pipeline-statistics-queries)
7. [Displaying Real-Time Data Without Impacting Performance](#7-displaying-real-time-data-without-impacting-performance)
8. [ImGui-Based Profiler Widgets](#8-imgui-based-profiler-widgets)
9. [Recommendations for Vestige](#9-recommendations-for-vestige)

---

## 1. GPU Timing Queries in OpenGL

### Overview

OpenGL provides two mechanisms for measuring GPU execution time, both part of `ARB_timer_query` (core since OpenGL 3.3, well within Vestige's 4.5 target):

**Approach A: GL_TIME_ELAPSED (begin/end style)**
```
glBeginQuery(GL_TIME_ELAPSED, queryId);
// ... draw calls for one pass ...
glEndQuery(GL_TIME_ELAPSED);
```
- Measures elapsed GPU nanoseconds between begin and end.
- Cannot be nested -- only one GL_TIME_ELAPSED query can be active at a time.
- Cannot overlap with other GL_TIME_ELAPSED queries.

**Approach B: GL_TIMESTAMP (timestamp style -- preferred)**
```
glQueryCounter(queryStartId, GL_TIMESTAMP);
// ... draw calls ...
glQueryCounter(queryEndId, GL_TIMESTAMP);
// Elapsed = end - start
```
- Records absolute GPU timestamps (nanoseconds) at specific points.
- Multiple timestamps can be recorded without nesting restrictions.
- Better for measuring multiple passes (shadow, geometry, post-process, etc.) because you place timestamps between passes and subtract.

### Retrieving Results

```cpp
GLuint64 startTime, endTime;
glGetQueryObjectui64v(queryStartId, GL_QUERY_RESULT, &startTime);
glGetQueryObjectui64v(queryEndId, GL_QUERY_RESULT, &endTime);
double elapsedMs = (endTime - startTime) / 1000000.0;
```

### Critical: Avoiding Pipeline Stalls with Double-Buffering

The GPU executes commands asynchronously. If you call `glGetQueryObjectui64v` with `GL_QUERY_RESULT` immediately after issuing the query, the CPU stalls until the GPU finishes -- defeating the purpose of profiling. The standard solution is **double-buffering timer queries**:

1. Maintain two sets of query objects (set A and set B).
2. Each frame, issue queries into one set and read results from the other set.
3. This introduces a 1-frame latency in results but eliminates stalls entirely.
4. Optionally check `GL_QUERY_RESULT_AVAILABLE` before reading, but with double-buffering this is usually unnecessary since the previous frame's queries will have completed.

**Nathan Reed's pattern** (from his GPU Profiling 101 article): Define an enum of timestamp points in the frame (e.g., `GTS_BeginFrame`, `GTS_ShadowPass`, `GTS_GeometryPass`, `GTS_PostProcess`, `GTS_EndFrame`), create 2 sets of query objects, and average deltas over half-second intervals for stable display.

### Practical Implementation for Vestige

Vestige's render pipeline has distinct passes: shadow (cascaded + point), geometry, SSAO, bloom, tonemapping, TAA. Each pass boundary is a natural timestamp point. With GL_TIMESTAMP, place `glQueryCounter` calls between each pass:

```
timestamp[SHADOW_START]
  shadow pass
timestamp[GEOMETRY_START]
  geometry pass
timestamp[SSAO_START]
  ssao pass
timestamp[BLOOM_START]
  bloom pass
timestamp[TONEMAP_START]
  tonemap/composite
timestamp[FRAME_END]
```

Then compute per-pass GPU time as differences between consecutive timestamps.

### Sources
- [Lighthouse3d: OpenGL Timer Query Tutorial](https://www.lighthouse3d.com/tutorials/opengl-timer-query/) -- Best practical tutorial with code examples for both approaches and double-buffering
- [Khronos OpenGL Wiki: glQueryCounter](https://www.khronos.org/opengl/wiki/GlQueryCounter) -- Official API reference
- [docs.gl: glQueryCounter](https://docs.gl/gl3/glQueryCounter) -- Clean API documentation
- [NVIDIA ARB_timer_query Spec](https://developer.download.nvidia.com/opengl/specs/GL_ARB_timer_query.txt) -- Original extension specification
- [Nathan Reed: GPU Profiling 101](https://www.reedbeta.com/blog/gpu-profiling-101/) -- Excellent practical guide with enum-based timestamp pattern and double-buffering
- [Funto/OpenGL-Timestamp-Profiler](https://github.com/Funto/OpenGL-Timestamp-Profiler) -- Simple open-source CPU/GPU OpenGL-based profiler implementation
- [KDAB: OpenGL in Qt - Timer Queries](https://www.kdab.com/opengl-in-qt-5-1-part-3/) -- Discussion of QOpenGLTimeMonitor for measuring multiple intervals
- [Khronos Forum: Timer query result unavailable after swap buffers](https://forums.khronos.org/showthread.php/76522) -- Edge case discussion

---

## 2. Performance Overlay Designs in Major Engines

### Unreal Engine

Unreal's stat system is the gold standard for console-like performance overlays:

- **`stat fps`** -- Simple FPS counter and frame time.
- **`stat unit`** -- Breaks frame time into: Frame, Game (CPU game logic), Draw (CPU render thread), GPU, RHIT (render hardware interface thread), Swap. This is the key insight: showing CPU vs GPU time and where each frame's time goes.
- **`stat unitgraph`** -- Same data as `stat unit` but as a real-time scrolling graph.
- **`stat gpu`** -- Splits GPU time by render pass: EarlyZPass, BasePass, Translucency, ShadowDepths, PostProcessing, etc.
- **`stat game`** -- Game logic breakdown: AI, Animation, Physics, Scripting.
- **`stat scenerendering`** -- Detailed render pipeline breakdown.
- **Unreal Insights** -- Standalone profiling tool with timeline/flame-graph views, hierarchical call trees, and memory tracking.

**Key design takeaway:** Unreal categorizes metrics into layers: top-level (FPS, frame time) -> thread breakdown (game, draw, GPU) -> per-system breakdown (passes, subsystems).

### Unity

- **Unity Profiler** -- CPU Usage module divides time into categories: Rendering, Scripts, Physics, GarbageCollector, VSync, Animation, AI, Audio, Particles, Networking, Loading.
- **GPU Usage module** -- Hierarchical breakdown of GPU time by pass, viewable as a table.
- **Memory Profiler** -- Total committed memory broken down by subsystem.
- **Frame Debugger** -- Step through draw calls one by one.

**Key design takeaway:** Unity color-codes each category (rendering = green, scripts = blue, physics = orange, etc.) in a stacked area chart. The user can see at a glance which category dominates each frame.

### Godot Engine

- **Performance class** -- Provides monitors: FPS, frametime, process time, physics time, navigation time.
- **Monitor tab** -- Subtabs for Script, Rendering, Audio, Physics with detailed per-category metrics.
- **Video RAM tab** -- Shows memory usage for textures, materials, shaders, meshes.
- **Custom performance monitors** -- Users can register custom monitors that appear alongside built-in ones.
- **Debug Menu addon** -- Displays FPS, frametime, CPU/GPU time graphs with best/worst/average summaries.

**Key design takeaway:** Godot's custom monitor system is elegant -- any subsystem can register a named metric that automatically appears in the profiler UI.

### Common Patterns Across All Engines

1. **Hierarchical categories:** Top-level frame time -> CPU vs GPU -> per-system breakdown.
2. **Color-coded stacked graphs:** Each category gets a distinct color.
3. **Rolling history:** 120-300 frames of history shown as scrolling graphs.
4. **Budget lines:** Horizontal lines at 16.67ms (60 FPS) and 33.33ms (30 FPS).
5. **Min/Max/Avg display:** Statistical summary alongside live values.
6. **Togglable detail levels:** Simple overlay (FPS only) -> medium (category breakdown) -> full (hierarchical tree).

### Sources
- [Intel: Unreal Engine Optimization Profiling Fundamentals](https://www.intel.com/content/www/us/en/developer/articles/technical/unreal-engine-optimization-profiling-fundamentals.html)
- [AMD GPUOpen: Unreal Engine Performance Guide](https://gpuopen.com/learn/unreal-engine-performance-guide/)
- [Unreal Art Optimization: Measuring Performance](https://unrealartoptimization.github.io/book/process/measuring-performance/)
- [Outscal: UE5 stat fps vs stat unit](https://outscal.com/blog/unreal-engine-5-profiling-tools)
- [Epic: Performance and Profiling Overview (UE4)](https://docs.unrealengine.com/4.27/en-US/TestingAndOptimization/PerformanceAndProfiling/Overview/)
- [Unity: Best practices for profiling](https://unity.com/how-to/best-practices-for-profiling-game-performance)
- [Unity: CPU Usage Profiler module](https://docs.unity3d.com/2018.4/Documentation/Manual/ProfilerCPU.html)
- [Unity: GPU Usage Profiler module](https://docs.unity3d.com/Manual/ProfilerGPU.html)
- [Unity: Memory Profiler module](https://docs.unity3d.com/Manual/ProfilerMemory.html)
- [Godot: Performance class docs](https://docs.godotengine.org/en/stable/classes/class_performance.html)
- [Godot: Custom performance monitors](https://docs.godotengine.org/en/stable/tutorials/scripting/debug/custom_performance_monitors.html)
- [Godot: The Profiler](https://docs.godotengine.org/en/stable/tutorials/scripting/debug/the_profiler.html)
- [Godot Debug Menu addon](https://github.com/godot-extended-libraries/godot-debug-menu)
- [Generalist Programmer: Game Optimization Complete Guide 2025](https://generalistprogrammer.com/tutorials/game-optimization-complete-performance-guide-2025)

---

## 3. Frame Time Graphs and Histograms in ImGui

### Built-in ImGui Plotting

ImGui provides basic plotting out of the box:

- **`ImGui::PlotLines`** -- Draws a line graph from a float array. Supports labels, overlay text, min/max scale, and a configurable size. Good for simple frame time graphs.
- **`ImGui::PlotHistogram`** -- Draws a bar chart from a float array. Similar API to PlotLines. Useful for showing frame time distribution.

Both accept a `values_getter` callback, allowing data from a circular buffer without copying.

### Ring Buffer Pattern

The standard approach for frame time history uses a fixed-size circular buffer:

```cpp
struct RingBuffer
{
    static constexpr int CAPACITY = 300;  // ~5 seconds at 60 FPS
    float data[CAPACITY] = {};
    int offset = 0;  // Write position

    void push(float value)
    {
        data[offset] = value;
        offset = (offset + 1) % CAPACITY;
    }
};
```

ImGui's `PlotLines` can accept this directly with its `values_offset` parameter, which tells it where the "start" of the data is in the circular buffer.

ImGui itself uses this pattern internally -- it averages the last 120 frames with a running sum for its built-in framerate display.

### Intel MetricsGui Library

A purpose-built ImGui extension for performance metrics (archived but still useful as reference):

- **MetricsGuiMetric** -- Represents a single named metric with units and formatting options.
- **MetricsGuiPlot** -- Manages a collection of metrics and renders visualizations.
- **DrawList()** -- Compact current-value display with optional inline sparkline graphs.
- **DrawHistory()** -- Full temporal graph with line or bar style.
- Configuration: `mBarRounding`, `mRangeDampening` (0.95 default for smooth axis scaling), `mShowAverage`, `mStacked` (for stacked area charts), `mSharedAxis`.

```cpp
MetricsGuiMetric frameTimeMetric("Frame time", "s",
    MetricsGuiMetric::USE_SI_UNIT_PREFIX);
MetricsGuiPlot frameTimePlot;
frameTimePlot.AddMetric(&frameTimeMetric);

// Each frame:
frameTimeMetric.AddNewValue(deltaTime);
frameTimePlot.UpdateAxes();
frameTimePlot.DrawHistory();
```

### ImPlot Library

A more full-featured plotting library for ImGui:

- GPU-accelerated rendering.
- Real-time scrolling plots with `ScrollingBuffer` helper.
- Sparkline support (`MyImPlot::Sparkline` in the demo).
- Handles 30+ line plots with ~1000 points each without issue.
- For larger datasets, downsampling is recommended.
- Supports stacked area, bar, scatter, heatmaps, and more.

**Caveat:** ImPlot is a significant dependency. For a lightweight engine profiler, the built-in `PlotLines`/`PlotHistogram` or MetricsGui's approach may be more appropriate.

### Adam Sawicki's Frame Time Visualization Idea

An alternative to simple line graphs: represent each frame as a vertical bar where the height is proportional to frame time. This makes spikes visually obvious and frame pacing issues (micro-stutter) immediately visible, even if average FPS looks fine.

### Sources
- [Intel GameTechDev/MetricsGui](https://github.com/GameTechDev/MetricsGui) -- ImGui performance metric controls library (archived, MIT license)
- [epezent/ImPlot](https://github.com/epezent/implot) -- Full GPU-accelerated plotting library for ImGui
- [ImPlot low FPS discussion](https://github.com/epezent/implot/discussions/218) -- Performance considerations for real-time plots
- [ImGui Discussion #4138: Accurate framerate](https://github.com/ocornut/imgui/discussions/4138) -- Ring buffer and running average patterns
- [ImGui Issue #1157: PlotHistogram with custom functionality](https://github.com/ocornut/imgui/issues/1157) -- Customizing histogram behavior
- [Adam Sawicki: An Idea for Visualization of Frame Times](https://asawicki.info/news_1758_an_idea_for_visualization_of_frame_times) -- Alternative frame time visualization
- [CITMProject3 FPSGraph.cpp](https://github.com/CITMProject3/Project3/blob/master/FPSGraph.cpp) -- Complete FPS graph implementation example
- [Medium: Building Lightweight Profiling Tools in Unreal Using ImGui](https://medium.com/@GroundZer0/building-lightweight-profiling-visualization-tools-in-unreal-using-imgui-7329c4bd32e6)

---

## 4. CPU Profiling Approaches for Game Engines

### RAII Scope-Based Profiling

The dominant pattern across game engines is an RAII scope timer:

```cpp
// Usage:
void Renderer::renderScene(...)
{
    VESTIGE_PROFILE_SCOPE("Renderer::renderScene");
    // ... code ...
    {
        VESTIGE_PROFILE_SCOPE("Shadow Pass");
        renderShadowPass(...);
    }
    {
        VESTIGE_PROFILE_SCOPE("Geometry Pass");
        // ...
    }
}
```

The macro creates a local object whose constructor records the start time and whose destructor records the end time. The RAII pattern means:
- No manual start/stop calls.
- No cleanup code.
- Correct timing even when exceptions are thrown.
- Nesting naturally builds a hierarchical tree.

### Hierarchical Tree Structure

The parent-child relationship is tracked via a thread-local stack:

1. When a scope is entered, push it onto the stack. Its parent is the previous top of stack.
2. When a scope exits, pop it. Accumulate elapsed time into the node.
3. The result is a tree: `Frame -> Renderer::renderScene -> Shadow Pass`, etc.

**Storage approaches:**
- **Flat array (preferred for small engines):** Store all scope entries in a flat vector with parent indices. Simple, cache-friendly, low overhead. This is what the SFEX profiler uses (~500 lines of C++ total).
- **Tree structure:** Explicit node-based tree. More memory, more pointer chasing, but natural for deep hierarchies.
- **Per-thread flat arrays:** Each thread gets its own array (thread_local), avoiding synchronization.

### Notable Implementations

**Vittorio Romeo's SFEX Profiler (~500 lines C++):**
- RAII-based `SFEX_PROFILE_SCOPE("name")` macro.
- Flat array storage, reconstructed into a hierarchy for display.
- ImGui table API for column layout, tree node API for collapsible hierarchy.
- Color-coded bars proportional to time taken.
- Built for CppCon 2025 keynote demo.

**Tracy Profiler (external tool, zero-copy design):**
- `ZoneScoped` / `ZoneScopedN("name")` macros.
- Nanosecond resolution, frame profiler.
- Profiles CPU (C++, Lua), GPU (OpenGL, Vulkan), and memory.
- Separate viewer application connected via network.
- "Not completely free" -- each zone event has a cost, but lightweight enough for production.
- OpenGL GPU profiling via `TracyGpuContext`.

**Unreal Engine 5:**
- `TRACE_CPUPROFILER_EVENT_SCOPE_STR("name")` macro.
- Data flows to Unreal Insights for timeline/flame-graph visualization.

**Wildfire Games (0 A.D. / Pyrogenesis):**
- `PROFILE2("name")` macro for scoped profiling.
- `PROFILE2_IFSPIKE` for conditional recording of slow frames only.
- `PROFILE2_ATTR` to attach printf-style metadata to regions.
- Hierarchical display with drill-down (press digit keys to expand).
- Rows show calls/frame and msec/frame, averaged over 30 frames.
- White = C++, red = script, making it easy to spot scripting bottlenecks.
- Built-in web server serves profiling data to an HTML/JS viewer.

**CTRACK Library:**
- Single-header, RAII-based.
- Records events when the CTRACK object goes out of scope.
- Minimal overhead, multi-threaded support.

### Recommended Approach for Vestige

A lightweight, self-contained scope profiler (like the SFEX approach) is ideal:
- ~200-500 lines of code.
- Thread-local flat array for zero-contention recording.
- RAII macro that records name, start time, end time, parent index.
- ImGui tree table for visualization.
- No external dependencies.

### Sources
- [Vittorio Romeo: Building a lightweight ImGui profiler in ~500 lines of C++](https://vittorioromeo.com/index/blog/sfex_profiler.html) -- Best reference for a lightweight custom implementation
- [Tracy Profiler (wolfpld/tracy)](https://github.com/wolfpld/tracy) -- Industry-standard frame profiler for C++
- [Typevar: Introduction to Tracy Profiler](https://typevar.dev/articles/wolfpld/tracy)
- [Rick.me.uk: C++ Profiling in Unreal Engine 5](https://www.rick.me.uk/posts/2024/12/cpp-profiling-in-unreal-engine-5/) -- UE5 scoped profiling macros
- [Wildfire Games: EngineProfiling](https://trac.wildfiregames.com/wiki/EngineProfiling) -- 0 A.D.'s hierarchical profiler design
- [Wildfire Games: Profiler2](https://trac.wildfiregames.com/wiki/Profiler2) -- Second-generation profiler with web server
- [Riot Games: Profiling Measurement and Analysis](https://technology.riotgames.com/news/profiling-measurement-and-analysis) -- Hierarchical profiling in a shipping game
- [Riot Games: Down the Rabbit Hole of Performance Monitoring](https://technology.riotgames.com/news/down-rabbit-hole-performance-monitoring) -- Sub-frame timing metrics
- [Compaile/ctrack](https://github.com/Compaile/ctrack) -- Lightweight single-header C++ benchmarking library
- [Google/orbit](https://github.com/google/orbit) -- Full-featured C/C++ performance profiler
- [bombomby/optick](https://github.com/bombomby/optick) -- C++ profiler for games with frame-based views
- [GameDev.net: Profiling Release Build with own Profiler](https://gamedev.net/forums/topic/536261-profiling-release-build-with-own-profiler-c/) -- Discussion of custom profiler implementation

---

## 5. Memory Tracking in C++ Engines

### CPU Memory Tracking

**Global operator new/delete override:**
```cpp
static size_t s_totalAllocated = 0;
static size_t s_totalFreed = 0;
static size_t s_allocationCount = 0;

void* operator new(size_t size)
{
    s_totalAllocated += size;
    s_allocationCount++;
    return malloc(size);
}

void operator delete(void* ptr, size_t size) noexcept
{
    s_totalFreed += size;
    free(ptr);
}
```

This gives: current usage = `s_totalAllocated - s_totalFreed`, allocation count per frame (reset each frame), and leak detection (if `s_totalAllocated != s_totalFreed` at shutdown).

**Limitations:**
- Only one strategy for all allocations.
- `operator delete(void*, size_t)` with size is C++14; without size, you need to store the size yourself (header before the allocation).
- Does not track allocations made by third-party libraries that use `malloc` directly.
- Thread safety requires atomics or thread-local counters.

**Bitsquid/Stingray approach (more sophisticated):**
- Custom allocator base class with `allocate(size, align)` and `deallocate(ptr)`.
- Each subsystem gets its own allocator instance.
- Allocators track their own totals, enabling per-subsystem memory reporting.
- A "proxy allocator" wraps another allocator and adds tracking (allocation count, peak usage, leak detection) without changing the underlying allocator.

**For Vestige (lightweight recommendation):**
- Override global `operator new`/`delete` with atomic counters for total allocated, total freed, allocation count.
- Reset allocation count per frame to track allocations-per-frame (important for spotting allocation spikes).
- Display: current heap usage, allocations this frame, peak usage.

### GPU Memory Tracking

OpenGL has no standard API for querying GPU memory. Two vendor-specific extensions exist:

**GL_NVX_gpu_memory_info (NVIDIA only):**
```cpp
GLint totalMemKb = 0, availMemKb = 0;
glGetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &totalMemKb);
glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &availMemKb);
```
Returns values in KB. Also provides `GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX` and `GPU_MEMORY_INFO_EVICTED_MEMORY_NVX`.

**GL_ATI_meminfo (AMD, but deprecated in modern drivers):**
```cpp
GLint texFreeMemInfo[4];
glGetIntegerv(GL_TEXTURE_FREE_MEMORY_ATI, texFreeMemInfo);
// texFreeMemInfo[0] = total free memory in pool (KB)
// texFreeMemInfo[1] = largest available free block (KB)
// texFreeMemInfo[2] = total auxiliary free memory
// texFreeMemInfo[3] = largest auxiliary free block
```
Also provides `GL_VBO_FREE_MEMORY_ATI` and `GL_RENDERBUFFER_FREE_MEMORY_ATI`.

**AMD has reportedly abandoned this extension** from Catalyst drivers. On modern AMDGPU/Mesa drivers (which Vestige's dev hardware uses -- RX 6600), availability needs testing.

**Manual GPU memory tracking (cross-vendor alternative):**
Track allocations yourself by summing:
- Texture sizes (width * height * bytes_per_pixel * mip_levels)
- Vertex/index buffer sizes
- Framebuffer attachment sizes (including MSAA multiplier)
- Shadow map sizes

This won't match driver-reported totals exactly (drivers add overhead for page tables, alignment, double buffering, etc.) but gives a useful estimate.

**Linux-specific alternative:** Read `/sys/class/drm/card0/device/mem_info_vram_used` (AMD) or use `nvidia-smi` (NVIDIA) via a subprocess. This is heavier but accurate.

### Sources
- [Geeks3D: How to Know GPU Memory Size and Usage in OpenGL](https://www.geeks3d.com/20100531/programming-tips-how-to-know-the-graphics-memory-size-and-usage-in-opengl/) -- Comprehensive overview of both extensions with code
- [NVIDIA GL_NVX_gpu_memory_info Spec](https://developer.download.nvidia.com/opengl/specs/GL_NVX_gpu_memory_info.txt) -- Official extension specification
- [Khronos: GL_ATI_meminfo Spec](https://registry.khronos.org/OpenGL/extensions/ATI/ATI_meminfo.txt) -- Official AMD memory info extension
- [Khronos Forums: GPU memory check utility functions](https://community.khronos.org/t/gpu-memory-check-utility-functions/63885) -- Discussion of limitations
- [Khronos Forums: How to query total video memory](https://community.khronos.org/t/how-to-query-the-total-amount-of-video-memory/43975)
- [Bitsquid: Custom Memory Allocation in C++](http://bitsquid.blogspot.com/2010/09/custom-memory-allocation-in-c.html) -- Game engine memory allocator design
- [ModernEscpp: Overloading Operator new and delete](https://www.modernescpp.com/index.php/overloading-operator-new-and-delete/) -- C++ operator overloading details
- [John Farrier: Custom Allocators in C++](https://johnfarrier.com/custom-allocators-in-c-high-performance-memory-management/) -- High-performance memory management
- [Oroboro: Overloading Global operator new and delete](https://oroboro.com/overloading-operator-new/) -- Practical guide with file/line tracking
- [GameDev.net: Managing OpenGL free memory](https://gamedev.net/forums/topic/579312-managing-opengl-free-memory/4690262/) -- Discussion of manual GPU memory tracking

---

## 6. OpenGL Pipeline Statistics Queries

### ARB_pipeline_statistics_query Extension

This extension (core in OpenGL 4.6, available as an extension on 4.5) provides statistical counters for different pipeline stages:

| Query Target | What It Counts |
|---|---|
| `GL_VERTICES_SUBMITTED_ARB` | Total vertices transferred via draw commands |
| `GL_PRIMITIVES_SUBMITTED_ARB` | Total primitives (points/lines/triangles/patches) submitted |
| `GL_VERTEX_SHADER_INVOCATIONS_ARB` | Number of vertex shader executions |
| `GL_TESS_CONTROL_SHADER_PATCHES_ARB` | Patches processed by tessellation control |
| `GL_TESS_EVALUATION_SHADER_INVOCATIONS_ARB` | Tessellation evaluation shader executions |
| `GL_GEOMETRY_SHADER_INVOCATIONS` | Geometry shader invocations (including instanced) |
| `GL_GEOMETRY_SHADER_PRIMITIVES_EMITTED_ARB` | Primitives output by geometry shader |
| `GL_FRAGMENT_SHADER_INVOCATIONS_ARB` | Fragment shader executions |
| `GL_COMPUTE_SHADER_INVOCATIONS_ARB` | Compute shader executions |
| `GL_CLIPPING_INPUT_PRIMITIVES_ARB` | Primitives entering clipping |
| `GL_CLIPPING_OUTPUT_PRIMITIVES_ARB` | Primitives exiting clipping (after clip splitting) |

### Usage Pattern
```cpp
GLuint query;
glGenQueries(1, &query);

glBeginQuery(GL_PRIMITIVES_SUBMITTED_ARB, query);
// ... draw calls ...
glEndQuery(GL_PRIMITIVES_SUBMITTED_ARB);

GLuint64 primitiveCount;
glGetQueryObjectui64v(query, GL_QUERY_RESULT, &primitiveCount);
```

### Important Caveats
- **Values are approximate:** The spec states "none of the statistics have to be exact, thus implementations might return slightly different results." This is fine for profiling/diagnostics but not for correctness-critical logic.
- **Cannot nest queries of the same type:** Each query type can have at most one active query at a time.
- **Same double-buffering concerns** as timer queries -- read from previous frame.
- **Driver support varies:** Check `GL_ARB_pipeline_statistics_query` availability at startup.

### Also Available (Core since GL 3.0)
- **`GL_PRIMITIVES_GENERATED`** -- Counts primitives from geometry shader output (or VS output if no GS). Useful for transform feedback scenarios.
- **`GL_SAMPLES_PASSED` / `GL_ANY_SAMPLES_PASSED`** -- Occlusion queries. Vestige could repurpose these for overdraw analysis.

### Relevance for Vestige
The most useful counters for a profiler overlay:
- `GL_VERTICES_SUBMITTED_ARB` -- Total vertex throughput.
- `GL_FRAGMENT_SHADER_INVOCATIONS_ARB` -- Fragment throughput / overdraw indicator.
- `GL_PRIMITIVES_SUBMITTED_ARB` -- Correlates with draw call complexity.
- `GL_CLIPPING_INPUT_PRIMITIVES_ARB` vs `GL_CLIPPING_OUTPUT_PRIMITIVES_ARB` -- Shows how much clipping is happening.

### Sources
- [Khronos: ARB_pipeline_statistics_query Spec](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_pipeline_statistics_query.txt) -- Complete specification with all query targets
- [Khronos OpenGL Wiki: Query Object](https://www.khronos.org/opengl/wiki/Query_Object) -- General query object documentation
- [Magnum: PipelineStatisticsQuery class](https://doc.magnum.graphics/magnum/classMagnum_1_1GL_1_1PipelineStatisticsQuery.html) -- Clean C++ wrapper example
- [docs.gl: glBeginQuery](https://docs.gl/gl3/glBeginQuery) -- API reference with query types

---

## 7. Displaying Real-Time Data Without Impacting Performance

### Key Principles

1. **Measure from the previous frame, display this frame.** Never stall the current frame waiting for GPU query results. Double-buffer all queries and read N-1 results.

2. **Use ring buffers, not dynamic allocation.** Pre-allocate fixed-size arrays for history data. Never call `new`/`malloc` in the profiling hot path.

3. **Smooth displayed values.** Raw per-frame values are noisy. Options:
   - Running average over N frames (ImGui uses 120).
   - Exponential moving average: `smoothed = alpha * current + (1 - alpha) * smoothed`.
   - Report over fixed time intervals (e.g., update stats every 0.5 seconds, average all frames in that window).

4. **Conditional profiling.** Only collect detailed metrics when the profiler panel is visible. GPU timer queries and pipeline statistics queries have near-zero cost when not issued, but nonzero cost when active (especially on some drivers).

5. **ImGui rendering cost is minimal.** Dear ImGui's overhead is negligible -- "next to no overhead when the overlay is not visible, and very lightweight while in use." The library does not touch graphics state, so it can be called from anywhere in the rendering pipeline.

6. **Limit GPU queries per frame.** NVIDIA recommends under 200 GPU performance markers per frame for negligible CPU overhead. For a small engine like Vestige with ~10 render passes, this is not a concern.

7. **Amortize expensive queries.** Things like GPU memory queries or pipeline statistics can be collected every 30-60 frames rather than every frame, since they change slowly.

8. **Thread-local storage for CPU profiling.** Avoids contention between threads. Each thread writes to its own buffer; the UI thread reads from all buffers during display.

### What NOT to Do
- Do not call `glGetQueryObjectui64v` with `GL_QUERY_RESULT` immediately after `glQueryCounter` -- this causes a full pipeline flush.
- Do not allocate memory (std::string, std::vector growth) in the profiling hot path.
- Do not compute statistics (percentiles, histograms) every frame if the profiler is hidden.
- Do not use `glFinish()` for timing -- it stalls the entire pipeline.

### Sources
- [ImGui: Dear ImGui repository](https://github.com/ocornut/imgui) -- Documentation on minimal overhead characteristics
- [Vittorio Romeo: SFEX Profiler](https://vittorioromeo.com/index/blog/sfex_profiler.html) -- Flat array storage for cache-friendly, allocation-free profiling
- [NVIDIA: GPU Performance Events Best Practices](https://developer.nvidia.com/blog/best-practices-gpu-performance-events/) -- Under 200 markers recommendation
- [Open 3D Engine: ImGui Gem](https://docs.o3de.org/docs/user-guide/gems/reference/debug/imgui/) -- "Next to no overhead when overlay is not visible"
- [ThatOneGameDev: Using ImGui for Game Development](https://thatonegamedev.com/cpp/using-imgui-for-game-development/)
- [Nathan Reed: GPU Profiling 101](https://www.reedbeta.com/blog/gpu-profiling-101/) -- Double-buffering and averaging strategies

---

## 8. ImGui-Based Profiler Widgets

### imgui-flame-graph (bwrsandman)

A standalone Dear ImGui widget for flame graph visualization:

- Single header + source file (`imgui_widget_flamegraph.h/.cpp`).
- `PlotFlame` function with API signature similar to `ImGui::PlotEx`.
- Renders horizontal stacked bars representing nested scopes.
- Each bar's width is proportional to duration.
- Color-coded by scope depth or custom coloring.
- MIT license.

### SimpleImGuiFlameGraph (CheapMeow)

An alternative flame graph implementation:

- Uses scope timers to measure time.
- Draws flame graph with less than 100 lines of code.
- More minimal than bwrsandman's version.

### imgui-profiler-component (study-game-engines)

A simple ImGui component for rendering profiling data:

- Designed specifically for game engine profiling.
- Renders hierarchical profiling data with timing information.

### SFEX Profiler (Vittorio Romeo)

Complete lightweight profiler with ImGui visualization:

- ~500 lines total.
- Uses ImGui's `BeginTable`/`EndTable` for column layout (Name, Time, % of Parent).
- Uses `TreeNodeEx` for collapsible hierarchy.
- Color-coded progress bars showing time proportion.
- Scope data stored in a flat array -- cache-friendly and allocation-free.

### Built-in ImGui Capabilities for Custom Profiler Widgets

ImGui provides building blocks for custom profiler visualizations:

- **`ImGui::PlotLines` / `ImGui::PlotHistogram`** -- Basic graphs and bar charts for time series.
- **`ImGui::BeginTable` / `ImGui::TableNextColumn`** -- For structured data display (name, value, bar).
- **`ImGui::TreeNodeEx`** -- Collapsible hierarchy for nested scopes.
- **`ImGui::GetWindowDrawList()->AddRectFilled`** -- Custom drawing for flame graphs, timeline bars, or sparklines.
- **`ImGui::ProgressBar`** -- For showing proportional time usage.
- **`ImGui::ColorConvertHSVtoRGB`** -- For generating distinct colors per scope.

### Sparklines in ImGui

Sparklines (tiny inline graphs) can be implemented as:
- Miniature `PlotLines` calls with a small fixed size (e.g., 80x20 pixels).
- ImPlot provides a dedicated `Sparkline` function in its demo.
- Custom draw using `ImDrawList` for maximum control.

### Tracy Integration (External, Full-Featured)

Rather than building a flame graph viewer from scratch, Tracy can be integrated:
- `ZoneScoped` macros in engine code.
- `TracyGpuContext` / `TracyGpuZone` for OpenGL GPU profiling.
- Separate Tracy viewer application provides timeline, flame graph, statistics, memory tracking.
- Heavier dependency but vastly more powerful.

### Recommendation for Vestige

**Phase 1 (Minimal, in-engine):**
- Use ImGui's built-in `PlotLines` for frame time graph.
- Use `BeginTable` + `TreeNodeEx` for hierarchical CPU scope timing.
- Use simple colored rectangles (`AddRectFilled`) for GPU pass timing bars.
- No external dependencies beyond what Vestige already has.

**Phase 2 (Enhanced, if needed):**
- Add imgui-flame-graph widget (single header) for CPU scope visualization.
- Consider ImPlot for more sophisticated graphs (scrolling, multiple series).

**Phase 3 (External, optional):**
- Tracy integration for deep profiling sessions.

### Sources
- [bwrsandman/imgui-flame-graph](https://github.com/bwrsandman/imgui-flame-graph) -- Flame graph widget for ImGui
- [CheapMeow/SimpleImGuiFlameGraph](https://github.com/CheapMeow/SimpleImGuiFlameGraph) -- Minimal flame graph implementation
- [study-game-engines/imgui-profiler-component](https://github.com/study-game-engines/imgui-profiler-component) -- Simple profiler rendering component
- [ImGui Issue #2859: Flame Graph Widget](https://github.com/ocornut/imgui/issues/2859) -- Discussion of flame graph approaches in ImGui
- [Vittorio Romeo: SFEX Profiler](https://vittorioromeo.com/index/blog/sfex_profiler.html) -- Complete ~500 line profiler with ImGui integration
- [epezent/ImPlot](https://github.com/epezent/implot) -- Full plotting library with sparklines and real-time scrolling
- [Tracy Profiler](https://github.com/wolfpld/tracy) -- Full-featured frame profiler with OpenGL GPU support
- [O3DE: CPU Profiling Support](https://www.docs.o3de.org/docs/user-guide/profiling/cpu_profiling/) -- ImGui-based profiler in Open 3D Engine

---

## 9. Recommendations for Vestige

### What Vestige Already Has
- `Timer` class: CPU-side FPS and deltaTime (GLFW-based).
- `Renderer::CullingStats`: draw calls, instance batches, culled items, shadow casters.
- `FrameDiagnostics`: one-shot screenshot + text report with pixel brightness analysis.
- ImGui docking editor with viewport, hierarchy, inspector, asset browser panels.
- No GPU timing, no CPU scope profiling, no memory tracking, no real-time graphs.

### Proposed Architecture

```
PerformanceOverlay (new ImGui panel)
  |
  +-- GpuTimerSystem (new)
  |     Uses GL_TIMESTAMP with double-buffered queries
  |     Measures: shadow, geometry, SSAO, bloom, tonemap, total GPU
  |
  +-- CpuProfiler (new)
  |     RAII scope macro: VESTIGE_PROFILE_SCOPE("name")
  |     Thread-local flat array storage
  |     Hierarchical tree reconstruction for display
  |
  +-- MemoryTracker (new)
  |     Global operator new/delete override
  |     Per-frame allocation count, current usage, peak usage
  |
  +-- FrameTimeHistory (new)
  |     Ring buffer of 300 frames
  |     Stores: CPU frame time, GPU frame time, per-pass GPU times
  |
  +-- Existing data
        Timer::getFps(), Timer::getDeltaTime()
        Renderer::getCullingStats()
```

### Implementation Priority

1. **GPU Timer System** -- Highest value. Vestige has a complex multi-pass pipeline but zero GPU timing visibility. This tells you where GPU time actually goes.

2. **Frame Time History + Graph** -- Simple ring buffer + `ImGui::PlotLines`. Immediately useful for spotting frame time spikes and patterns.

3. **CPU Scope Profiler** -- RAII macro with flat array. Place scopes around major engine subsystems (input, physics/update, scene collect, render, editor UI).

4. **Performance Overlay Panel** -- ImGui panel combining all the above: text summary at top, frame time graph, GPU pass breakdown bar, CPU scope tree.

5. **Memory Tracking** -- Lower priority but easy to add. Global new/delete override with atomic counters.

6. **Pipeline Statistics** -- Lowest priority. Nice-to-have for debugging overdraw or excessive vertex count, but not needed for day-to-day profiling.

### Estimated Scope
- GpuTimerSystem: ~150-200 lines (header + source)
- CpuProfiler: ~200-300 lines (header + source + macro)
- MemoryTracker: ~100 lines (header + source)
- FrameTimeHistory: ~50-80 lines (ring buffer struct)
- PerformanceOverlay panel: ~300-400 lines (ImGui rendering)
- Total: ~800-1100 lines of new code

### Key Design Decisions to Make
1. Should the profiler be compile-time strippable (via `#ifdef VESTIGE_PROFILING`)? This is common for shipping builds.
2. Should GPU timer queries be always-on or only when the panel is visible? (Recommendation: only when visible, to avoid any overhead in play mode.)
3. Should CPU profiling data persist across frames for averaging, or show single-frame snapshots? (Recommendation: average over ~30 frames like 0 A.D., with option to freeze/capture a single frame.)
4. Should the overlay be available in both EDIT and PLAY modes? (Recommendation: yes, as a toggleable overlay. In PLAY mode it could be a simple top-corner HUD; in EDIT mode it's a dockable panel.)

---

## All Sources (Consolidated)

### GPU Timer Queries
- [Lighthouse3d: OpenGL Timer Query](https://www.lighthouse3d.com/tutorials/opengl-timer-query/)
- [Khronos: glQueryCounter](https://www.khronos.org/opengl/wiki/GlQueryCounter)
- [docs.gl: glQueryCounter](https://docs.gl/gl3/glQueryCounter)
- [glQueryCounter Reference](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glQueryCounter.xhtml)
- [NVIDIA ARB_timer_query Spec](https://developer.download.nvidia.com/opengl/specs/GL_ARB_timer_query.txt)
- [Nathan Reed: GPU Profiling 101](https://www.reedbeta.com/blog/gpu-profiling-101/)
- [Funto/OpenGL-Timestamp-Profiler](https://github.com/Funto/OpenGL-Timestamp-Profiler)
- [KDAB: OpenGL Timer Queries in Qt](https://www.kdab.com/opengl-in-qt-5-1-part-3/)
- [NVIDIA: GPU Performance Events Best Practices](https://developer.nvidia.com/blog/best-practices-gpu-performance-events/)

### Engine Profiler Design
- [Intel: UE Optimization Profiling Fundamentals](https://www.intel.com/content/www/us/en/developer/articles/technical/unreal-engine-optimization-profiling-fundamentals.html)
- [AMD GPUOpen: UE Performance Guide](https://gpuopen.com/learn/unreal-engine-performance-guide/)
- [Unreal Art Optimization: Measuring Performance](https://unrealartoptimization.github.io/book/process/measuring-performance/)
- [Outscal: UE5 stat fps vs stat unit](https://outscal.com/blog/unreal-engine-5-profiling-tools)
- [Epic: Performance and Profiling Overview](https://docs.unrealengine.com/4.27/en-US/TestingAndOptimization/PerformanceAndProfiling/Overview/)
- [Unity: Profiling Best Practices](https://unity.com/how-to/best-practices-for-profiling-game-performance)
- [Unity: CPU Usage Profiler](https://docs.unity3d.com/2018.4/Documentation/Manual/ProfilerCPU.html)
- [Unity: GPU Usage Profiler](https://docs.unity3d.com/Manual/ProfilerGPU.html)
- [Unity: Memory Profiler](https://docs.unity3d.com/Manual/ProfilerMemory.html)
- [Godot: Performance class](https://docs.godotengine.org/en/stable/classes/class_performance.html)
- [Godot: Custom performance monitors](https://docs.godotengine.org/en/stable/tutorials/scripting/debug/custom_performance_monitors.html)
- [Godot: The Profiler](https://docs.godotengine.org/en/stable/tutorials/scripting/debug/the_profiler.html)
- [Godot Debug Menu](https://github.com/godot-extended-libraries/godot-debug-menu)
- [Game Optimization Guide 2025](https://generalistprogrammer.com/tutorials/game-optimization-complete-performance-guide-2025)

### ImGui Performance Visualization
- [Intel GameTechDev/MetricsGui](https://github.com/GameTechDev/MetricsGui)
- [epezent/ImPlot](https://github.com/epezent/implot)
- [ImGui Discussion #4138: Framerate](https://github.com/ocornut/imgui/discussions/4138)
- [ImGui Issue #1157: PlotHistogram](https://github.com/ocornut/imgui/issues/1157)
- [Adam Sawicki: Frame Time Visualization](https://asawicki.info/news_1758_an_idea_for_visualization_of_frame_times)
- [CITMProject3 FPSGraph](https://github.com/CITMProject3/Project3/blob/master/FPSGraph.cpp)
- [Medium: ImGui Profiling in Unreal](https://medium.com/@GroundZer0/building-lightweight-profiling-visualization-tools-in-unreal-using-imgui-7329c4bd32e6)

### CPU Profiling
- [Vittorio Romeo: SFEX Profiler (~500 lines)](https://vittorioromeo.com/index/blog/sfex_profiler.html)
- [Tracy Profiler](https://github.com/wolfpld/tracy)
- [Typevar: Tracy Introduction](https://typevar.dev/articles/wolfpld/tracy)
- [Rick.me.uk: C++ Profiling in UE5](https://www.rick.me.uk/posts/2024/12/cpp-profiling-in-unreal-engine-5/)
- [Wildfire Games: EngineProfiling](https://trac.wildfiregames.com/wiki/EngineProfiling)
- [Wildfire Games: Profiler2](https://trac.wildfiregames.com/wiki/Profiler2)
- [Riot Games: Profiling Measurement](https://technology.riotgames.com/news/profiling-measurement-and-analysis)
- [Riot Games: Performance Monitoring](https://technology.riotgames.com/news/down-rabbit-hole-performance-monitoring)
- [Compaile/ctrack](https://github.com/Compaile/ctrack)
- [Google/orbit](https://github.com/google/orbit)
- [bombomby/optick](https://github.com/bombomby/optick)

### Memory Tracking
- [Geeks3D: GPU Memory in OpenGL](https://www.geeks3d.com/20100531/programming-tips-how-to-know-the-graphics-memory-size-and-usage-in-opengl/)
- [NVIDIA GL_NVX_gpu_memory_info](https://developer.download.nvidia.com/opengl/specs/GL_NVX_gpu_memory_info.txt)
- [Khronos: GL_ATI_meminfo](https://registry.khronos.org/OpenGL/extensions/ATI/ATI_meminfo.txt)
- [Bitsquid: Custom Memory Allocation](http://bitsquid.blogspot.com/2010/09/custom-memory-allocation-in-c.html)
- [ModernEscpp: Operator new/delete](https://www.modernescpp.com/index.php/overloading-operator-new-and-delete/)
- [John Farrier: Custom Allocators](https://johnfarrier.com/custom-allocators-in-c-high-performance-memory-management/)
- [Oroboro: Global operator new/delete](https://oroboro.com/overloading-operator-new/)

### Pipeline Statistics
- [Khronos: ARB_pipeline_statistics_query](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_pipeline_statistics_query.txt)
- [Khronos: Query Object Wiki](https://www.khronos.org/opengl/wiki/Query_Object)
- [Magnum: PipelineStatisticsQuery](https://doc.magnum.graphics/magnum/classMagnum_1_1GL_1_1PipelineStatisticsQuery.html)
- [docs.gl: glBeginQuery](https://docs.gl/gl3/glBeginQuery)

### ImGui Profiler Widgets
- [bwrsandman/imgui-flame-graph](https://github.com/bwrsandman/imgui-flame-graph)
- [CheapMeow/SimpleImGuiFlameGraph](https://github.com/CheapMeow/SimpleImGuiFlameGraph)
- [study-game-engines/imgui-profiler-component](https://github.com/study-game-engines/imgui-profiler-component)
- [ImGui Issue #2859: Flame Graph Widget](https://github.com/ocornut/imgui/issues/2859)
- [O3DE: CPU Profiling](https://www.docs.o3de.org/docs/user-guide/profiling/cpu_profiling/)
- [O3DE: ImGui Gem](https://docs.o3de.org/docs/user-guide/gems/reference/debug/imgui/)
