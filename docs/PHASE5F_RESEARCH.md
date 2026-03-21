# Phase 5F Research: Console/Log Panel, Screenshot Tools, and Editor Utilities

**Date:** 2026-03-21
**Scope:** Console/log panel, screenshot enhancements, keyboard shortcuts reference
**Engine:** Vestige (C++17, OpenGL 4.5, ImGui docking)
**Existing systems:** Logger (6 severity levels, stdout/stderr), FrameDiagnostics (F11 PNG + text report), placeholder Console panel in editor

---

## Table of Contents

1. [ImGui Console/Log Panel Implementations](#1-imgui-consolelog-panel-implementations)
2. [Ring Buffer Log Storage](#2-ring-buffer-log-storage)
3. [Console Command Systems](#3-console-command-systems)
4. [Redirecting Logger Output to an ImGui Panel](#4-redirecting-logger-output-to-an-imgui-panel)
5. [Color-Coded Log Levels in ImGui](#5-color-coded-log-levels-in-imgui)
6. [Screenshot Tool Enhancements](#6-screenshot-tool-enhancements)
7. [Keyboard Shortcuts Panel/Reference](#7-keyboard-shortcuts-panelreference)
8. [Recommendations for Vestige](#8-recommendations-for-vestige)

---

## 1. ImGui Console/Log Panel Implementations

### 1.1 imgui_demo.cpp: ExampleAppLog (Simple Log Window)

The official ImGui demo contains two console-related examples that serve as the foundation for nearly every ImGui-based log panel in the wild.

**ExampleAppLog** is a minimal log viewer with the following structure:

- **ImGuiTextBuffer** -- a single contiguous text buffer holding all log text. New log entries are appended with `AddLog()` (printf-style formatting). This is efficient because there is no per-entry allocation; everything is one giant string.
- **ImVector<int> LineOffsets** -- an index of byte offsets into the text buffer where each line begins. This enables O(1) access to any line by index, which is essential for the clipper.
- **ImGuiTextFilter** -- a built-in ImGui utility that provides a text input field and a `PassFilter(const char*)` method. Supports comma-separated sub-filters and negation with `-` prefix (e.g., `error,-texture` shows lines containing "error" but not "texture").
- **Auto-scroll** -- a boolean flag `AutoScroll` (default true). When set, after appending items, the panel calls `ImGui::SetScrollHereY(1.0f)` at the end of the frame to scroll to the bottom. The flag is automatically disabled if the user scrolls up manually, and re-enabled if they scroll to the bottom.
- **Copy button** -- copies the entire filtered log to the clipboard via `ImGui::LogToClipboard()`.
- **Clear button** -- empties the text buffer and line offsets.

**Key limitation:** ExampleAppLog stores only raw text -- no metadata per line (no severity level, no timestamp, no source tag). Colored text or per-line filtering by severity requires a different data structure.

**Sources:**
- [imgui_demo.cpp on GitHub (master)](https://github.com/ocornut/imgui/blob/master/imgui_demo.cpp)
- [imgui_demo.cpp on Codebrowser (searchable)](https://codebrowser.dev/imgui/imgui/imgui_demo.cpp.html)
- [Tip/Demo: Log example as helper class -- Issue #300](https://github.com/ocornut/imgui/issues/300)

### 1.2 imgui_demo.cpp: ExampleAppConsole (Interactive Console)

A more advanced example that adds a command input line, command history, and tab completion.

**Structure:**

- `char InputBuf[256]` -- text input buffer for the command line.
- `ImVector<char*> Items` -- log output lines (dynamically allocated C strings). Each line is a separate allocation.
- `ImVector<const char*> Commands` -- list of known commands for tab completion.
- `ImVector<char*> History` -- previously entered commands, navigable with Up/Down arrows.
- `int HistoryPos` -- current position in the history stack (`-1` = new line, `0` = most recent).
- `ImGuiTextFilter Filter` -- text filter for the output log.
- `bool AutoScroll`, `bool ScrollToBottom` -- scroll management.

**Callback system:** Uses `ImGui::InputText()` with the flags `ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory`. The callback function handles:

- **Tab completion (`ImGuiInputTextFlags_CallbackCompletion`):** Parses the word at the cursor, searches the command list for matches. If one match, auto-completes. If multiple, prints all candidates and completes the common prefix.
- **History navigation (`ImGuiInputTextFlags_CallbackHistory`):** Up arrow decrements `HistoryPos`, Down arrow increments it. The current input is replaced with the history entry.

**Built-in commands:** `HELP`, `HISTORY`, `CLEAR`, `CLASSIFY` (as examples).

**Sources:**
- [imgui_demo.cpp -- ExampleAppConsole](https://github.com/ocornut/imgui/blob/master/imgui_demo.cpp)
- [How to implement the console system from demo.cpp -- Discussion #5889](https://github.com/ocornut/imgui/discussions/5889)
- [Use of struct ExampleAppLog from imgui_demo.cpp -- Issue #999](https://github.com/ocornut/imgui/issues/999)

### 1.3 rmxbalanque/imgui-console

A standalone C++ console widget library built on top of Dear ImGui. More feature-complete than the demo examples.

**Features:**
- Command registration via `RegisterCommand(name, description, callback)` -- commands are functions that receive parsed arguments.
- Tab completion for commands.
- Command history with Up/Down navigation.
- Smart scrolling (auto-scrolls only when already at bottom).
- Timestamps on log entries.
- Log filtering by text search.
- Colored console output by severity.
- Settings persistence through `imgui.ini`.
- Integrates a command system library (`csys`) for parsing, variable binding, and scripting.

**Architecture:** Separates the console widget (ImGui rendering) from the command system backend. The command system (`csys`) handles parsing, argument type checking, and dispatch. The ImGui widget handles rendering, input, and display.

**Sources:**
- [GitHub: rmxbalanque/imgui-console](https://github.com/rmxbalanque/imgui-console)
- [imgui-console example_main.cpp](https://github.com/rmxbalanque/imgui-console/blob/master/example/src/example_main.cpp)
- [imgui-console src/imgui_console.cpp](https://github.com/rmxbalanque/imgui-console/blob/master/src/imgui_console.cpp)

### 1.4 ForrestTheWoods: fts_remote_console

A cross-platform remote logging console in C++. While its focus is on remote (network) logging, the architectural design is highly relevant.

**Key design decisions:**
- One console can connect to multiple game instances simultaneously.
- Games broadcast their presence for auto-discovery.
- Two-way communication between game and console.
- External tool (not in-game overlay), advantageous for headless servers and multi-platform debugging.

**Relevance to Vestige:** While we want an in-editor panel (not a remote tool), the data model concepts -- log entries as structured data with metadata, filtering by source/severity, multiple output targets -- transfer directly to an in-editor console.

**Sources:**
- [Blog post: Writing a Cross-Platform Remote Logging Console in C++](https://www.forrestthewoods.com/blog/writing_a_crossplatform_remote_logging_console_in_cpp/)
- [GitHub: forrestthewoods/fts_remote_console](https://github.com/forrestthewoods/fts_remote_console)

### 1.5 BYGImguiLogger (Unreal Engine)

Brace Yourself Games' ImGui log viewer for Unreal Engine. Heavily based on imgui_demo.cpp's Console example but extended with real-world features.

**Features:**
- Filters by verbosity level (Error, Warning, Display, Log, Verbose, VeryVerbose).
- Filters by log category (LogTemp, LogAI, LogPhysics, etc.).
- Toggle buttons per severity level -- click to show/hide all messages of that level.
- Text search filter on top of severity filters.
- Called once per tick in the game loop.

**Relevance to Vestige:** This is a production-tested ImGui log viewer in a real game engine. The dual-filter approach (severity toggles + text search) is the standard pattern we should follow.

**Sources:**
- [GitHub: BraceYourselfGames/UE-BYGImguiLogger](https://github.com/BraceYourselfGames/UE-BYGImguiLogger)

### 1.6 Cog (Unreal Engine ImGui Debug Tools)

A comprehensive set of debug tools for Unreal Engine built on ImGui.

**Log-related features:**
- Displays output log filtered by category and verbosity.
- Per-category verbosity configuration in a separate "Log Categories" window.
- Demonstrates how a professional-grade ImGui-based debug toolset organizes logging.

**Sources:**
- [GitHub: 3CM-Games/UnrealPlugin-ImGui-Cog](https://github.com/3CM-Games/UnrealPlugin-ImGui-Cog)

---

## 2. Ring Buffer Log Storage

### 2.1 Why a Ring Buffer?

Game engine consoles must handle potentially thousands of log messages per second (especially at Trace/Debug level) without unbounded memory growth. A ring buffer (circular buffer) provides:

- **Fixed memory footprint** -- allocate once at startup, never grow.
- **O(1) insertion** -- always write to the next slot; when full, overwrite the oldest entry.
- **No memory fragmentation** -- unlike `std::vector<std::string>` which allocates per entry.
- **Cache-friendly iteration** -- contiguous memory layout for sequential reads.

### 2.2 Implementation Approaches

**Approach A: Ring buffer of structured log entries**

```
struct LogEntry
{
    LogLevel    level;
    float       timestamp;      // seconds since engine start
    uint32_t    frameNumber;
    // Small buffer optimization: store short messages inline
    char        message[256];   // or use std::string if messages vary widely
};

// Fixed-size ring buffer
static constexpr size_t MAX_LOG_ENTRIES = 16384;
LogEntry    m_entries[MAX_LOG_ENTRIES];
size_t      m_head = 0;     // next write position
size_t      m_count = 0;    // number of valid entries (up to MAX_LOG_ENTRIES)
```

When `m_count == MAX_LOG_ENTRIES`, oldest entries are silently overwritten. The display layer iterates from `(m_head - m_count) % MAX_LOG_ENTRIES` to `m_head - 1`.

**Approach B: Ring buffer of offsets into a contiguous text buffer**

Closer to the `ExampleAppLog` approach -- a single large `char[]` buffer plus a ring buffer of `{offset, length, level}` metadata records. More memory-efficient for variable-length messages.

**Approach C: std::deque with a max size**

Simpler to implement: push_back new entries, pop_front when size exceeds the limit. Not as cache-friendly as a true ring buffer but adequate for most game engines with a cap of 10K-50K entries.

### 2.3 Recommended Capacity

- **10,000-50,000 entries** is typical for game engine consoles (Unity uses ~10K, Unreal defaults vary).
- At 256 bytes per entry, 16K entries = 4 MB -- well within budget.
- The capacity should be configurable at compile time or startup.

**Sources:**
- [Creating a Circular Buffer in C and C++ -- Embedded Artistry](https://embeddedartistry.com/blog/2017/05/17/creating-a-circular-buffer-in-c-and-c/)
- [Efficient Ring Buffer Implementation in C++ -- Medium (Devedium)](https://medium.com/@devedium/efficient-ring-buffer-implementation-in-c-for-high-performance-data-handling-77c416ce5359)
- [How to Implement a Circular Buffer Using std::vector -- GeeksforGeeks](https://www.geeksforgeeks.org/cpp/implement-circular-buffer-using-std-vector-in-cpp/)
- [An asynchronous lock-free ring buffer for logging](https://steven-giesel.com/blogPost/11f0ded8-7119-4cfc-b7cf-317ff73fb671)
- [Circular buffer -- Wikipedia](https://en.wikipedia.org/wiki/Circular_buffer)

---

## 3. Console Command Systems

### 3.1 Architecture Patterns

Game engine consoles universally follow this pattern:

```
User types command  -->  Parser  -->  Command Registry  -->  Execute  -->  Output
                                          |
                                   Tab Completion
                                   Help/Description
```

**Command Registry:** A map from command name (string) to a command descriptor containing:
- Name (e.g., `"set_exposure"`)
- Description (e.g., `"Set camera exposure. Usage: set_exposure <float>"`)
- Callback function (`std::function<void(const std::vector<std::string>&)>`)
- Argument metadata (optional, for validation and auto-complete)

**Parsing:** Split input string by whitespace. First token is the command name; remaining tokens are arguments. Simple and sufficient for engine consoles. No need for a full expression parser.

**History:** Store executed command strings in a `std::vector<std::string>` (or ring buffer). Navigate with Up/Down arrow keys. Store up to 100-200 entries.

### 3.2 Tab Completion Strategies

1. **Command name completion:** Match the partial input against all registered command names. If one match, auto-complete. If multiple, show candidates and complete the common prefix.
2. **Argument completion:** Some commands can provide context-sensitive argument completions (e.g., `load_scene` could complete to scene file names). Requires per-command completion callbacks.
3. **Variable name completion:** If the console supports reading/writing engine variables (like Quake's cvar system), complete against variable names.

### 3.3 ImGui InputText Callbacks for Console

ImGui's `InputText` with callback flags is the foundation:

- `ImGuiInputTextFlags_CallbackCompletion` -- fires when Tab is pressed. The callback receives `ImGuiInputTextCallbackData*` with the current buffer, cursor position, and methods to insert/delete text.
- `ImGuiInputTextFlags_CallbackHistory` -- fires when Up/Down arrows are pressed. The callback replaces the buffer with the history entry.
- `ImGuiInputTextFlags_EnterReturnsTrue` -- returns true when Enter is pressed, at which point you execute the command and clear the input.
- `ImGuiInputTextFlags_EscapeClearsAll` -- Escape clears the input field.

### 3.4 Third-Party Libraries

**VirtuosoConsole:**
- Single-file header-only C++ Quake-style console backend.
- Binds C++ variables and functions with minimal code using template metaprogramming.
- Automatically parses argument lists of any type readable from `std::istream`.
- Call `console.commandExecute(inputStream, outputStream)` when the user presses Enter.
- ImGui widget planned/available.

**ImTerm:**
- C++17 header-only library implementing a terminal for ImGui applications.
- You supply a `TerminalHelper` class that defines available commands.
- Built-in completion callback system -- you return `std::vector<std::string>` of completions.
- Optional spdlog integration.
- UTF-8 friendly.

**Sources:**
- [GitHub: VirtuosoChris/VirtuosoConsole](https://github.com/VirtuosoChris/VirtuosoConsole)
- [GitHub: Organic-Code/ImTerm](https://github.com/Organic-Code/ImTerm)
- [ImGui Discussion #5889 -- Implementing the console system](https://github.com/ocornut/imgui/discussions/5889)
- [ImGui Issue #456 -- Shortcuts API](https://github.com/ocornut/imgui/issues/456)

---

## 4. Redirecting Logger Output to an ImGui Panel

### 4.1 The Callback/Sink Pattern

The core challenge: Vestige's `Logger` class currently writes to `stdout`/`stderr`. We need to also capture those messages into an in-memory buffer for the ImGui panel, without changing every call site.

**Pattern: Add a callback (sink) to the Logger.**

The Logger maintains a list of sink functions. Every `log()` call invokes all registered sinks in addition to (or instead of) the console output.

```cpp
// Conceptual API
using LogCallback = std::function<void(LogLevel level, const std::string& message)>;

class Logger
{
public:
    static void addSink(LogCallback callback);
    static void removeSink(LogCallback callback); // or by ID/handle
    // ... existing methods unchanged ...
private:
    static std::vector<LogCallback> s_sinks;
};
```

When `Logger::log()` is called, it iterates `s_sinks` and calls each one. The ImGui console panel registers a sink at startup that appends to its ring buffer.

### 4.2 Sink with Structured Data

Rather than passing just the message string, pass a structured log entry:

```cpp
struct LogEntry
{
    LogLevel    level;
    float       engineTime;
    uint32_t    frameNumber;
    std::string message;
};

using LogSink = std::function<void(const LogEntry&)>;
```

This lets the console panel store all metadata needed for filtering, coloring, and display without parsing the formatted string.

### 4.3 Existing Third-Party Approaches

**spdlog callback_sink_mt:**
spdlog (a popular C++ logging library) provides `callback_sink_mt` which takes a lambda that fires on every log call. If Vestige ever migrates to spdlog, this is the standard approach. Each spdlog logger holds a vector of sinks, and each sink's `log()` method is called on every message.

**Lima-X's spdlog sink for ImGui:**
A high-performance, filtered, configurable spdlog sink specifically designed for Dear ImGui. Adds itself to the spdlog pipeline and provides a `DrawLogWindow()` call for the main loop. Demonstrates double-buffering of filter results for performance.

**dear_spdlog:**
An ImGui window that acts as a sink for spdlog. You create a sink, add it to your logger, and call `sink->draw_imgui()` during ImGui rendering.

### 4.4 Thread Safety Considerations

If Vestige ever logs from background threads (e.g., the async texture loader), the sink must be thread-safe:
- Use a mutex around the ring buffer's write path, **or**
- Use a lock-free single-producer/single-consumer ring buffer, **or**
- Use a thread-safe queue that the main thread drains each frame.

For now, since Vestige logs only from the main thread, thread safety is not an immediate concern but should be designed in from the start.

**Sources:**
- [Lima-X's spdlog sink for ImGui (Gist)](https://gist.github.com/Lima-X/73f2bbf9ac03818ab8ef42ab15d09935)
- [GitHub: awegsche/dear_spdlog](https://github.com/awegsche/dear_spdlog)
- [spdlog Wiki: Sinks](https://github.com/gabime/spdlog/wiki/Sinks)
- [spdlog Issue #1668 -- Custom sink help](https://github.com/gabime/spdlog/issues/1668)
- [ImGui Discussion #6187 -- Log/Console for Vulkan renderer integration](https://github.com/ocornut/imgui/discussions/6187)
- [Observer Pattern in C++ -- Refactoring.guru](https://refactoring.guru/design-patterns/observer/cpp/example)
- [Observer Pattern in C++ -- ACCU Overload](https://accu.org/journals/overload/10/52/bass_372/)

---

## 5. Color-Coded Log Levels in ImGui

### 5.1 Standard Color Conventions

Across logging libraries, game engines, and IDEs, the following color conventions are near-universal:

| Level   | Color                  | Rationale                                              |
|---------|------------------------|--------------------------------------------------------|
| Trace   | Gray (dim)             | Most verbose, least important -- should not distract   |
| Debug   | White / Light gray     | Development info, normal but not critical               |
| Info    | Green / Cyan           | Positive confirmation, normal operation                 |
| Warning | Yellow / Orange        | Attention needed but not broken                         |
| Error   | Red                    | Something is broken                                     |
| Fatal   | Bright red / Red on bg | Unrecoverable, demands immediate attention              |

**Specific ImVec4 values (for Vestige's dark theme):**

```cpp
ImVec4 getLogColor(LogLevel level)
{
    switch (level)
    {
        case LogLevel::Trace:   return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);  // Gray
        case LogLevel::Debug:   return ImVec4(0.8f, 0.8f, 0.8f, 1.0f);  // Light gray
        case LogLevel::Info:    return ImVec4(0.4f, 0.9f, 0.4f, 1.0f);  // Green
        case LogLevel::Warning: return ImVec4(1.0f, 0.9f, 0.3f, 1.0f);  // Yellow
        case LogLevel::Error:   return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);  // Red
        case LogLevel::Fatal:   return ImVec4(1.0f, 0.0f, 0.0f, 1.0f);  // Bright red
    }
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}
```

### 5.2 Rendering Colored Lines Efficiently

**Problem:** The simple `ExampleAppLog` approach (one big text buffer with `ImGui::TextUnformatted()`) cannot color individual lines because `TextUnformatted` renders everything in the default text color.

**Solution: Use ImGuiListClipper with per-line TextColored().**

```cpp
// Pseudocode for the draw loop
ImGuiListClipper clipper;
clipper.Begin(visibleEntryCount);
while (clipper.Step())
{
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
    {
        const LogEntry& entry = getVisibleEntry(i);
        ImGui::TextColored(getLogColor(entry.level), "%s", entry.message.c_str());
    }
}
clipper.End();
```

**ImGuiListClipper** is essential for performance. Without it, rendering 10,000+ colored text lines would be expensive. The clipper calculates which lines are visible in the scroll region and only renders those (typically 20-50 lines). This keeps the cost constant regardless of total log size.

**Performance note from ImGui issues:** Even 100,000 lines with `ImGuiListClipper` perform well, as the clipper skips all vertex generation for off-screen items. The bottleneck shifts to the data model (building the filtered view), not the rendering.

### 5.3 Accessibility Considerations

Since the Vestige user is partially sighted:
- Colors alone must not be the only indicator -- also prepend level tags like `[WARN]`, `[ERROR]`.
- Ensure sufficient contrast against the dark background.
- Consider making the Fatal level use a colored background (not just text color) for maximum visibility.
- The colors listed above were chosen for high contrast on a dark theme.

**Sources:**
- [Colors in logging example -- ImGui Issue #5470](https://github.com/ocornut/imgui/issues/5470)
- [Change color within text -- ImGui Issue #902](https://github.com/ocornut/imgui/issues/902)
- [Colorful logger not printing gradually -- ImGui Issue #5796](https://github.com/ocornut/imgui/issues/5796)
- [ImGui Issue #124 -- Optimization: visible portion clipping](https://github.com/ocornut/imgui/issues/124)
- [ImGui performance discussion -- Issue #7065](https://github.com/ocornut/imgui/issues/7065)
- [Recommended best practice for virtual list of unlimited size](https://discourse.dearimgui.org/t/recommended-best-practice-for-virtual-list-of-unlimited-size/223)

---

## 6. Screenshot Tool Enhancements

### 6.1 Current State in Vestige

`FrameDiagnostics::capture()` currently:
- Reads the default framebuffer via `glReadPixels(0, 0, w, h, ...)`.
- Saves a PNG at the window's native resolution.
- Writes a companion text diagnostic report.
- Triggered by F11.

### 6.2 Resolution Multiplier (Super-Resolution Screenshots)

**How Unreal Engine does it:**
Unreal's High Resolution Screenshot tool includes a "Screenshot Size Multiplier" slider. Setting it to 2 renders at 2x resolution, 3 at 3x, etc. The screenshot is rendered to an offscreen buffer at the higher resolution, then saved to disk. The viewport display is unaffected.

**How to implement in OpenGL:**

1. **Create an oversized FBO:** Allocate a framebuffer with a color attachment texture at `width * multiplier` x `height * multiplier`.
2. **Render the scene to the oversized FBO:** Set `glViewport(0, 0, w * multiplier, h * multiplier)` and render as normal. The camera's projection matrix should use the same aspect ratio as normal, but the higher pixel count captures more detail.
3. **Read pixels from the oversized FBO:** `glReadPixels()` on the oversized FBO gives you the high-res image.
4. **Save to disk:** Write the high-res pixel data directly as PNG.
5. **Clean up:** Delete the temporary FBO and texture.

**No downscaling needed if saving to file** -- the purpose is a high-res file, not a display buffer. If you wanted to display a preview, you would blit/downsample to the screen-size FBO.

**Practical multiplier limits:**
- 2x is safe on all hardware (e.g., 1920x1080 becomes 3840x2160 = ~25 MB texture).
- 4x (7680x4320) = ~100 MB texture -- check `GL_MAX_TEXTURE_SIZE` (typically 16384 on modern GPUs, including RX 6600).
- 8x (15360x8640) may exceed GPU memory -- use a tiled approach for very large captures.

**Tiled rendering for extreme resolutions:**
Divide the final image into FBO-sized tiles. For each tile, adjust `glViewport` and the projection matrix to render only that portion, read it back, and stitch the tiles together on CPU. This approach can produce arbitrarily large screenshots.

### 6.3 Viewport-Only Capture

Currently, `FrameDiagnostics` reads the default framebuffer, which includes the ImGui editor UI. For a clean viewport-only capture:

- Read from the **renderer's output FBO** (before ImGui compositing), not the default framebuffer.
- Vestige already has `renderer->getOutputTextureId()` -- the texture can be read with `glGetTexImage()` or by binding its FBO and using `glReadPixels()`.
- This gives a clean scene image without editor panels.

### 6.4 Format Options

**PNG** (current): Lossless, large files (~5-15 MB at 1080p). Best for quality, diagnostic use.
**JPEG**: Lossy, much smaller files (~200-500 KB). Good for quick sharing. Quality parameter 80-95 is typical.
**BMP**: Uncompressed, very large. Rarely useful except for debugging.
**EXR/HDR**: Floating-point formats that preserve the full HDR range. Useful for capturing the HDR buffer before tonemapping. Vestige uses `stb_image_write.h` which does not support EXR; would need TinyEXR or similar.

**stb_image_write.h** already supports PNG, BMP, TGA, and JPG -- so adding JPEG output is trivial (just call `stbi_write_jpg()` with a quality parameter).

### 6.5 Screenshot UI

A screenshot dialog or toolbar could offer:
- **Resolution multiplier:** Dropdown or slider (1x, 2x, 4x).
- **Capture source:** "Viewport only" vs "Full window (with UI)".
- **Format:** PNG / JPEG (with quality slider for JPEG).
- **Include diagnostics:** Toggle for the text report.
- **Shortcut:** F12 for quick capture (use current settings), Shift+F12 to open the settings dialog.

**Sources:**
- [High-Resolution Screenshots in Unreal Engine 5](https://www.unreal-university.blog/how-to-take-high-resolution-screenshots-in-unreal-engine-5-complete-guide/)
- [Taking Screenshots -- Unreal Engine 5.7 Documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/taking-screenshots-in-unreal-engine)
- [LearnOpenGL -- Framebuffers](https://learnopengl.com/Advanced-OpenGL/Framebuffers)
- [Capture Screenshot in OpenGL -- Christian Vallentin](https://vallentin.dev/blog/post/opengl-screenshot)
- [How to Take a Screenshot in OpenGL -- W3Tutorials](https://www.w3tutorials.net/blog/how-to-take-screenshot-in-opengl/)
- [Capturing screenshots with Rust + OpenGL (transferable concepts)](https://tonyfinn.com/blog/opengl-rust-screenshots/)
- [OpenGL FBO -- Song Ho Ahn](https://www.songho.ca/opengl/gl_fbo.html)
- [CMU 15-466: Framebuffers, Offscreen Rendering](https://15466.courses.cs.cmu.edu/lesson/framebuffers)
- [FRAMED Screenshot Community -- Basics](https://framedsc.com/basics.htm)

---

## 7. Keyboard Shortcuts Panel/Reference

### 7.1 How Existing Editors Handle This

**Godot Engine:**
- Comprehensive keyboard shortcut reference in the official docs, organized by category (General, 2D editor, 3D editor, Script editor, etc.).
- Shortcuts are customizable in Editor Settings > Shortcuts.
- A community proposal exists for a "Command Palette" (Ctrl+Shift+P, like VS Code) that searches all editor actions and shows their shortcuts.
- No built-in in-editor shortcut cheatsheet popup, but the documentation page serves this purpose.

**Blender:**
- Has an in-app "Keymap" editor in Preferences.
- The F3 key opens a search dialog that finds any operator by name and shows its shortcut.
- Spacebar (in some keymaps) opens a similar search menu.

**VS Code / Sublime Text:**
- Ctrl+Shift+P opens the Command Palette -- a searchable list of every command with its keybinding shown inline.
- Ctrl+K Ctrl+S opens the full keyboard shortcut editor.

**Open 3D Engine (O3DE):**
- Uses ImGui for its debug overlay. The tilde key (~) toggles the ImGui debug menu.
- Arrow keys navigate menus, Space activates options.

### 7.2 Implementation for Vestige

**Approach: Modal/popup shortcut reference window.**

A window triggered by Help > Controls (or a keyboard shortcut like `?` or `F1`) that shows a categorized, searchable table of all keyboard shortcuts.

**Data structure:**
```cpp
struct ShortcutEntry
{
    const char* category;     // "Viewport", "File", "Entity", "Gizmo", etc.
    const char* shortcut;     // "Ctrl+Z", "W", "Delete", etc.
    const char* description;  // "Undo last action"
};
```

Store all shortcuts in a `static const` array. The panel iterates this array, optionally filtered by a text search field.

**ImGui rendering with BeginTable:**
```cpp
ImGui::InputText("Search", searchBuf, sizeof(searchBuf));
if (ImGui::BeginTable("Shortcuts", 3, tableFlags))
{
    ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 150.0f);
    ImGui::TableSetupColumn("Action",   ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    for (const auto& entry : shortcuts)
    {
        if (!filter.PassFilter(entry.description) && !filter.PassFilter(entry.shortcut))
            continue;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("%s", entry.category);
        ImGui::TableSetColumnIndex(1); ImGui::TextColored(accentColor, "%s", entry.shortcut);
        ImGui::TableSetColumnIndex(2); ImGui::Text("%s", entry.description);
    }
    ImGui::EndTable();
}
```

**Table flags:** `ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable`

### 7.3 Vestige's Current Shortcuts (to populate the reference)

From the codebase analysis:

| Category  | Shortcut           | Action                                    |
|-----------|--------------------|-------------------------------------------|
| File      | Ctrl+N             | New scene                                 |
| File      | Ctrl+O             | Open scene                                |
| File      | Ctrl+S             | Save scene                                |
| File      | Ctrl+Shift+S       | Save scene as                             |
| File      | Ctrl+Q             | Quit                                      |
| File      | Ctrl+I             | Import model                              |
| Edit      | Ctrl+Z             | Undo                                      |
| Edit      | Ctrl+Y             | Redo                                      |
| Edit      | Ctrl+Shift+Z       | Redo (alternate)                          |
| Viewport  | W                  | Translate gizmo                           |
| Viewport  | E                  | Rotate gizmo                              |
| Viewport  | R                  | Scale gizmo                               |
| Viewport  | L                  | Toggle local/world space                  |
| Viewport  | Ctrl+drag          | Snap to grid                              |
| Entity    | Delete             | Delete selected                           |
| Entity    | Ctrl+D             | Duplicate selected                        |
| Entity    | Ctrl+G             | Group selected                            |
| Entity    | H                  | Toggle visibility                         |
| Entity    | Ctrl+Shift+C       | Copy transform                            |
| Entity    | Ctrl+Shift+V       | Paste transform                           |
| Camera    | Alt+LMB            | Orbit camera                              |
| Camera    | MMB drag           | Pan camera                                |
| Camera    | Scroll             | Zoom camera                               |
| Debug     | F11                | Capture frame diagnostic                  |
| Mode      | Escape             | Toggle editor/play mode                   |

**Sources:**
- [Default editor shortcuts -- Godot Engine docs](https://docs.godotengine.org/en/stable/tutorials/editor/default_key_mapping.html)
- [Command Palette proposal for Godot -- Issue #1444](https://github.com/godotengine/godot-proposals/issues/1444)
- [Keyboard shortcuts info in debug builds -- IMGUI style (Koshmaar)](https://jahej.com/alt/2014_03_15_keyboard-shortcuts-info-in-debug-builds-imgui-style.html)
- [Creating configurable hotkeys/shortcuts -- ImGui Issue #1684](https://github.com/ocornut/imgui/issues/1684)
- [ImGui Shortcuts API discussion -- Issue #456](https://github.com/ocornut/imgui/issues/456)
- [O3DE ImGui Gem documentation](https://docs.o3de.org/docs/user-guide/gems/reference/debug/imgui/)

---

## 8. Recommendations for Vestige

### 8.1 Console/Log Panel Architecture

**Recommended approach: Structured log entries in a ring buffer, displayed with ImGuiListClipper and TextColored.**

1. **Extend Logger with a callback sink system.** Add `Logger::addSink(LogSink)` and `Logger::removeSink()`. The existing `stdout`/`stderr` output becomes the "default sink." The console panel registers its own sink at initialization.

2. **Define a LogEntry struct:**
   ```cpp
   struct LogEntry
   {
       LogLevel    level;
       float       engineTime;     // Timer::getElapsedTime()
       uint32_t    frameNumber;
       std::string message;        // the raw message (without [Vestige][LEVEL] prefix)
   };
   ```

3. **Store entries in a ring buffer** with a capacity of 16,384 entries (configurable). Use a simple array + head/count approach rather than a third-party library.

4. **Build a filtered view** -- a secondary `std::vector<int>` of indices into the ring buffer that pass the current filters. Rebuild this view when filters change. Cache it between frames when filters are unchanged.

5. **Render with ImGuiListClipper** iterating the filtered view. Each visible line uses `ImGui::TextColored()` with the severity color.

6. **Console UI layout** (top to bottom):
   - Toolbar row: severity toggle buttons (Trace/Debug/Info/Warn/Error/Fatal), Clear button, Copy button, Auto-scroll toggle.
   - Filter text input (ImGuiTextFilter).
   - Scrollable log region (the bulk of the panel).
   - Command input line at the bottom (InputText with Enter/Tab/History callbacks).

### 8.2 Console Commands (Phase 1 -- Minimal)

Start with a small set of built-in commands:

| Command              | Description                          |
|----------------------|--------------------------------------|
| `help`               | List all commands                    |
| `clear`              | Clear the log                        |
| `history`            | Show command history                 |
| `set_exposure <f>`   | Set camera exposure                  |
| `set_fov <f>`        | Set camera FOV                       |
| `wireframe [on/off]` | Toggle wireframe mode                |
| `bloom [on/off]`     | Toggle bloom                         |
| `ssao [on/off]`      | Toggle SSAO                          |
| `screenshot`         | Take a screenshot                    |
| `stats`              | Print FPS, draw calls, memory usage  |

Use a simple `std::unordered_map<std::string, CommandDescriptor>` registry. No need for a third-party command system library at this stage.

### 8.3 Screenshot Enhancements

1. **Add JPEG output** -- trivial with `stbi_write_jpg()`. Add a format selection.
2. **Add resolution multiplier** -- render to an oversized FBO, `glReadPixels()`, save. Support 1x, 2x, 4x. Check `GL_MAX_TEXTURE_SIZE` at startup to determine maximum.
3. **Add viewport-only capture** -- read from the renderer's output FBO instead of the default framebuffer.
4. **Assign F12 for quick screenshot** (distinct from F11 diagnostics). Shift+F12 opens settings.
5. **Keep F11 for diagnostics** as-is (PNG + text report).

### 8.4 Keyboard Shortcuts Panel

1. **Define shortcuts in a static data table** (array of `ShortcutEntry` structs).
2. **Render as a searchable ImGui table** in a modal window triggered by Help > Controls or F1.
3. **Three columns:** Category, Shortcut, Description. Filterable by text search.
4. **Keep it in sync** -- when new shortcuts are added to the engine, add them to the table. (In a future iteration, this could be auto-generated from a central shortcut registry.)

### 8.5 Implementation Order

1. **Logger sink system** -- modify `Logger` to support callbacks (small, foundational change).
2. **LogEntry struct + ring buffer** -- the data layer.
3. **Console panel UI** -- replace the placeholder with the real panel (severity toggles, text filter, colored log, auto-scroll).
4. **Command input** -- add the input line, command registry, and built-in commands.
5. **Screenshot enhancements** -- JPEG support, resolution multiplier, viewport capture.
6. **Keyboard shortcuts panel** -- static data table + ImGui table rendering.

### 8.6 Files to Create/Modify

| File                                  | Action  | Purpose                                      |
|---------------------------------------|---------|----------------------------------------------|
| `engine/core/logger.h`               | Modify  | Add sink callback API                         |
| `engine/core/logger.cpp`             | Modify  | Implement sink dispatch                       |
| `engine/editor/console_panel.h`      | Create  | ConsolePanel class declaration                |
| `engine/editor/console_panel.cpp`    | Create  | Console panel rendering + command system      |
| `engine/core/log_entry.h`            | Create  | LogEntry struct + ring buffer                 |
| `engine/editor/shortcuts_panel.h`    | Create  | ShortcutsPanel class declaration              |
| `engine/editor/shortcuts_panel.cpp`  | Create  | Shortcuts reference table                     |
| `engine/renderer/frame_diagnostics.h`| Modify  | Add format/multiplier/source parameters       |
| `engine/renderer/frame_diagnostics.cpp`| Modify | Implement enhanced screenshot capture         |
| `engine/editor/editor.cpp`           | Modify  | Wire new panels into the editor               |
| `engine/editor/editor.h`             | Modify  | Add ConsolePanel + ShortcutsPanel members     |
| `tests/test_log_ring_buffer.cpp`     | Create  | Unit tests for ring buffer                    |
| `tests/test_console_commands.cpp`    | Create  | Unit tests for command parsing/execution      |

---

## All Sources Referenced

### ImGui Official
- [imgui_demo.cpp (master branch)](https://github.com/ocornut/imgui/blob/master/imgui_demo.cpp)
- [imgui_demo.cpp (Codebrowser -- searchable)](https://codebrowser.dev/imgui/imgui/imgui_demo.cpp.html)
- [Tip/Demo: Log example as helper class -- Issue #300](https://github.com/ocornut/imgui/issues/300)
- [How to implement the console system -- Discussion #5889](https://github.com/ocornut/imgui/discussions/5889)
- [Use of struct ExampleAppLog -- Issue #999](https://github.com/ocornut/imgui/issues/999)
- [Colors in logging example -- Issue #5470](https://github.com/ocornut/imgui/issues/5470)
- [Change color within text -- Issue #902](https://github.com/ocornut/imgui/issues/902)
- [Colorful logger printing -- Issue #5796](https://github.com/ocornut/imgui/issues/5796)
- [Console auto-scroll -- Issue #3299](https://github.com/ocornut/imgui/issues/3299)
- [Few bugs in console log example -- Issue #5721](https://github.com/ocornut/imgui/issues/5721)
- [Auto-scroll commit](https://gitea.dresselhaus.cloud/Drezil/imgui/commit/2206df9e7a092f15e7a5c552d4691c232fbf1007)
- [Log/Console for Vulkan renderer integration -- Discussion #6187](https://github.com/ocornut/imgui/discussions/6187)
- [SetScrollHere behavior -- Issue #1804](https://github.com/ocornut/imgui/issues/1804)
- [Optimization: visible portion clipping -- Issue #124](https://github.com/ocornut/imgui/issues/124)
- [ImGui performance -- Issue #7065](https://github.com/ocornut/imgui/issues/7065)
- [Virtual list best practices -- Dear ImGui discourse](https://discourse.dearimgui.org/t/recommended-best-practice-for-virtual-list-of-unlimited-size/223)
- [Debug Tools wiki](https://github.com/ocornut/imgui/wiki/Debug-Tools)
- [Creating configurable hotkeys -- Issue #1684](https://github.com/ocornut/imgui/issues/1684)
- [Shortcuts API -- Issue #456](https://github.com/ocornut/imgui/issues/456)

### Third-Party ImGui Console Libraries
- [rmxbalanque/imgui-console](https://github.com/rmxbalanque/imgui-console)
- [VirtuosoChris/VirtuosoConsole](https://github.com/VirtuosoChris/VirtuosoConsole)
- [Organic-Code/ImTerm (C++17)](https://github.com/Organic-Code/ImTerm)

### Logging Integration
- [Lima-X spdlog sink for ImGui (Gist)](https://gist.github.com/Lima-X/73f2bbf9ac03818ab8ef42ab15d09935)
- [awegsche/dear_spdlog](https://github.com/awegsche/dear_spdlog)
- [spdlog Wiki: Sinks](https://github.com/gabime/spdlog/wiki/Sinks)
- [ForrestTheWoods: Writing a Cross-Platform Remote Logging Console](https://www.forrestthewoods.com/blog/writing_a_crossplatform_remote_logging_console_in_cpp/)
- [forrestthewoods/fts_remote_console](https://github.com/forrestthewoods/fts_remote_console)

### Game Engine Log Viewers
- [BraceYourselfGames/UE-BYGImguiLogger](https://github.com/BraceYourselfGames/UE-BYGImguiLogger)
- [3CM-Games/UnrealPlugin-ImGui-Cog](https://github.com/3CM-Games/UnrealPlugin-ImGui-Cog)

### Ring Buffer / Circular Buffer
- [Creating a Circular Buffer in C and C++ -- Embedded Artistry](https://embeddedartistry.com/blog/2017/05/17/creating-a-circular-buffer-in-c-and-c/)
- [Efficient Ring Buffer Implementation in C++ -- Medium](https://medium.com/@devedium/efficient-ring-buffer-implementation-in-c-for-high-performance-data-handling-77c416ce5359)
- [Circular Buffer Using std::vector -- GeeksforGeeks](https://www.geeksforgeeks.org/cpp/implement-circular-buffer-using-std-vector-in-cpp/)
- [Asynchronous lock-free ring buffer for logging](https://steven-giesel.com/blogPost/11f0ded8-7119-4cfc-b7cf-317ff73fb671)
- [Circular buffer -- Wikipedia](https://en.wikipedia.org/wiki/Circular_buffer)

### Screenshots / OpenGL Rendering
- [High-Resolution Screenshots in Unreal Engine 5 (Unreal University)](https://www.unreal-university.blog/how-to-take-high-resolution-screenshots-in-unreal-engine-5-complete-guide/)
- [Taking Screenshots -- Unreal Engine 5.7 Documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/taking-screenshots-in-unreal-engine)
- [LearnOpenGL -- Framebuffers](https://learnopengl.com/Advanced-OpenGL/Framebuffers)
- [LearnOpenGL -- Anti-Aliasing (supersampling)](https://learnopengl.com/Advanced-OpenGL/Anti-Aliasing)
- [OpenGL FBO Tutorial -- Song Ho Ahn](https://www.songho.ca/opengl/gl_fbo.html)
- [Capture Screenshot in OpenGL -- Christian Vallentin](https://vallentin.dev/blog/post/opengl-screenshot)
- [How to Take a Screenshot in OpenGL -- W3Tutorials](https://www.w3tutorials.net/blog/how-to-take-screenshot-in-opengl/)
- [Capturing screenshots with Rust + OpenGL -- Tony Finn](https://tonyfinn.com/blog/opengl-rust-screenshots/)
- [CMU 15-466: Framebuffers and Offscreen Rendering](https://15466.courses.cs.cmu.edu/lesson/framebuffers)
- [FRAMED Screenshot Community -- Basics](https://framedsc.com/basics.htm)

### Keyboard Shortcuts
- [Default editor shortcuts -- Godot Engine docs](https://docs.godotengine.org/en/stable/tutorials/editor/default_key_mapping.html)
- [Command Palette proposal for Godot -- Issue #1444](https://github.com/godotengine/godot-proposals/issues/1444)
- [Keyboard shortcuts info in debug builds -- IMGUI style](https://jahej.com/alt/2014_03_15_keyboard-shortcuts-info-in-debug-builds-imgui-style.html)
- [O3DE ImGui Gem documentation](https://docs.o3de.org/docs/user-guide/gems/reference/debug/imgui/)
- [Quake-style Sliding Console -- DEV Community](https://dev.to/jeansberg/quake-style-sliding-console-4j2m)

### Design Patterns
- [Observer Pattern in C++ -- Refactoring.guru](https://refactoring.guru/design-patterns/observer/cpp/example)
- [Observer Pattern in C++ -- ACCU Overload](https://accu.org/journals/overload/10/52/bass_372/)
- [Observer Pattern -- Software Patterns Lexicon](https://softwarepatternslexicon.com/patterns-cpp/6/8/)
