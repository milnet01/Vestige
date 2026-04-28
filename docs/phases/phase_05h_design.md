# Phase 5H: Performance Dashboard — Design Document

## Goal

Add a comprehensive real-time performance profiling system to the editor. Users see GPU pass timings, CPU scope timings, frame time history, draw call analysis, memory stats, and budget compliance — all in a live ImGui panel with no perceptible overhead when hidden.

**Milestone:** The user can open the Performance panel, see per-pass GPU timing, CPU hierarchical profiling, frame time graph with 16.67ms budget line, memory usage, and draw call stats — all updating in real-time at 60 FPS.

---

## Current State (End of Phase 5G)

The engine has:
- **CullingStats:** Draw calls, instanced batches, culled items (per frame)
- **Frame diagnostics:** F11 screenshot + text brightness analysis
- **Console panel:** Text log output
- **Timer:** DeltaTime, FPS counter, elapsed time

**What's missing:**
- No GPU pass-level timing (shadow, geometry, SSAO, bloom, etc.)
- No CPU hierarchical profiling (which functions are expensive?)
- No frame time history graph
- No memory tracking (CPU or GPU)
- No performance budget visualization
- No flame graph or stacked timing bars

---

## Architecture

```
engine/
├── profiler/
│   ├── gpu_timer.h / .cpp           — Double-buffered GL_TIMESTAMP queries
│   ├── cpu_profiler.h / .cpp        — RAII scope profiler with flat array storage
│   ├── memory_tracker.h / .cpp      — CPU heap + GPU VRAM tracking
│   └── performance_profiler.h / .cpp — Central hub, aggregates all profiling data
├── editor/
│   └── panels/
│       └── performance_panel.h / .cpp — ImGui dashboard with tabs
```

### Data Flow

```
Each frame:
  GpuTimer: insert GL_TIMESTAMP queries at pass boundaries
  CpuProfiler: RAII scopes record start/end times
  MemoryTracker: atomic counters for alloc/free (amortized VRAM read)
  → PerformanceProfiler collects and snapshots all data
  → PerformancePanel reads snapshot, renders ImGui widgets
```

---

## Component Design

### 1. GpuTimer

Double-buffered GL_TIMESTAMP query pool. Frame N reads results from Frame N-2 (avoids stalls).

```cpp
class GpuTimer
{
public:
    void init();
    void shutdown();

    /// Begin a named GPU timing section.
    void beginPass(const std::string& name);
    /// End the current GPU timing section.
    void endPass();

    /// Swap query buffers at end of frame. Call after endFrame().
    void swapBuffers();

    /// Get timing results from the completed frame (ms per pass).
    struct PassTiming { std::string name; float timeMs; };
    const std::vector<PassTiming>& getResults() const;

    /// Total GPU frame time in ms.
    float getTotalGpuTimeMs() const;

private:
    static constexpr int MAX_PASSES = 16;
    static constexpr int BUFFER_COUNT = 3; // Triple buffer for safety

    struct QueryPair { GLuint start, end; };
    QueryPair m_queries[BUFFER_COUNT][MAX_PASSES];
    std::string m_passNames[MAX_PASSES];
    int m_passCount = 0;
    int m_writeBuffer = 0;
    int m_readBuffer = 2;
    int m_frameCount = 0;
    std::vector<PassTiming> m_results;
};
```

### 2. CpuProfiler

RAII scope-based profiler. Flat array with parent indices for hierarchical display.

```cpp
/// Place at function/scope start to measure time.
#define VESTIGE_PROFILE_SCOPE(name) \
    Vestige::CpuProfileScope _profileScope##__LINE__(name)

struct ProfileEntry
{
    const char* name;       // String literal, no allocation
    float startMs;
    float endMs;
    int parentIndex;        // -1 = root
    int depth;
};

class CpuProfiler
{
public:
    void beginFrame();
    void endFrame();

    void pushScope(const char* name);
    void popScope();

    /// Snapshot of last completed frame (safe to read from render thread).
    const std::vector<ProfileEntry>& getLastFrame() const;

    /// Total CPU frame time.
    float getTotalCpuTimeMs() const;

private:
    std::vector<ProfileEntry> m_currentFrame;
    std::vector<ProfileEntry> m_lastFrame;
    std::vector<int> m_scopeStack;  // Index stack for parent tracking
};

/// RAII helper — pushes on construct, pops on destruct.
class CpuProfileScope
{
public:
    CpuProfileScope(const char* name);
    ~CpuProfileScope();
};
```

### 3. MemoryTracker

CPU heap tracking via atomic counters. GPU VRAM via AMD sysfs (amortized reads).

```cpp
class MemoryTracker
{
public:
    void update();  // Called once per frame

    // CPU stats
    size_t getCpuAllocatedBytes() const;
    size_t getCpuAllocationCount() const;

    // GPU stats (updated every 60 frames)
    size_t getGpuUsedMB() const;
    size_t getGpuTotalMB() const;

private:
    size_t m_gpuUsedMB = 0;
    size_t m_gpuTotalMB = 0;
    int m_gpuUpdateCounter = 0;
};
```

### 4. PerformanceProfiler

Central hub that owns all profiling subsystems and provides a clean read API.

```cpp
class PerformanceProfiler
{
public:
    void init();
    void shutdown();

    void beginFrame();
    void endFrame();

    GpuTimer& getGpuTimer();
    CpuProfiler& getCpuProfiler();
    MemoryTracker& getMemoryTracker();

    // Frame history (ring buffer, 300 frames)
    float getFrameTimeMs() const;
    float getFps() const;
    const float* getFrameTimeHistory() const;
    int getFrameTimeHistorySize() const;
    int getFrameTimeHistoryOffset() const;

    // Stats
    float getAvgFrameTimeMs() const;
    float getMinFrameTimeMs() const;
    float getMaxFrameTimeMs() const;

    bool isEnabled() const;
    void setEnabled(bool enabled);

private:
    GpuTimer m_gpuTimer;
    CpuProfiler m_cpuProfiler;
    MemoryTracker m_memoryTracker;

    // Frame time history (ring buffer)
    static constexpr int HISTORY_SIZE = 300;
    float m_frameTimeHistory[HISTORY_SIZE] = {};
    int m_historyIndex = 0;
    float m_minFrameTime = 999.0f;
    float m_maxFrameTime = 0.0f;

    bool m_enabled = false;
};
```

### 5. PerformancePanel

ImGui panel with multiple views:

- **Overview:** Frame time graph (PlotLines), FPS, CPU vs GPU time, budget line at 16.67ms
- **GPU Passes:** Stacked horizontal bar showing pass proportions, per-pass timing table
- **CPU Scopes:** Hierarchical tree table with timing, percentage, sparklines
- **Memory:** CPU heap usage, GPU VRAM bar, per-category breakdown
- **Draw Calls:** Stats table (draws, instances, triangles, batch efficiency)

---

## Implementation Sub-Phases

### Phase 5H-1: GPU Timer + CPU Profiler Core
1. Implement GpuTimer with triple-buffered GL_TIMESTAMP queries
2. Implement CpuProfiler with RAII scope macro and flat array storage
3. Implement PerformanceProfiler hub with frame time ring buffer
4. Integrate into Engine: beginFrame/endFrame calls, GPU pass markers in renderer

### Phase 5H-2: Performance Panel (Overview + GPU)
1. Create PerformancePanel with Overview tab (frame time graph, FPS, min/max/avg)
2. Add GPU Passes tab (per-pass timing bars + table)
3. Add budget line at 16.67ms on the frame time graph
4. Add to Editor dock layout and View menu
5. Wire F12 key to toggle panel visibility

### Phase 5H-3: CPU Scopes + Draw Call Analysis
1. Add VESTIGE_PROFILE_SCOPE markers to key engine functions
2. Add CPU Scopes tab with hierarchical tree table
3. Extend CullingStats with per-pass breakdown and triangle counts
4. Add Draw Calls tab with stats table and batch efficiency

### Phase 5H-4: Memory Tracking + Polish
1. Implement MemoryTracker (CPU atomic counters + AMD sysfs VRAM)
2. Add Memory tab to the panel
3. Add color-coded budget bars (green/yellow/red)
4. Performance polish — verify profiler overhead < 0.5ms

---

## Performance Budget Reference

| System | Budget | Notes |
|--------|--------|-------|
| Shadow pass | 2.0 ms | Cascaded shadow maps |
| Geometry pass | 3.0 ms | Opaque + instanced |
| SSAO | 1.5 ms | Screen-space AO |
| Bloom | 1.0 ms | Mip-chain downsample/upsample |
| Foliage | 1.5 ms | Instanced grass + trees |
| Water | 0.5 ms | Water surfaces |
| Particles | 0.5 ms | Particle rendering |
| Tonemapping + AA | 1.0 ms | TAA resolve + tonemap |
| Editor UI | 1.0 ms | ImGui rendering |
| **Total budget** | **16.67 ms** | **60 FPS** |

---

## Accessibility

- Frame time graph uses both color AND a dashed budget line (not just color)
- All numeric values have text readouts alongside visual bars
- Panel text follows the editor's configured font size
- Timing values shown in milliseconds with 2 decimal places (precise, no guessing)
