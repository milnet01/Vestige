# Vestige Coding Standards

Mandatory rules. All new code must conform.

---

## 1. File Naming

| Type | Convention | Example |
|------|-----------|---------|
| C++ source/header | `snake_case.{cpp,h}` | `scene_manager.cpp` |
| Shader | `snake_case.{vert,frag,geom,comp}.glsl` | `basic_lighting.vert.glsl` |
| Test | `test_snake_case.cpp` | `test_event_bus.cpp` |
| CMake | standard | `CMakeLists.txt` |

**One class per file.** File name = class name in snake_case (`SceneManager` → `scene_manager.{h,cpp}`).

---

## 2. Naming Conventions

| Kind | Style | Example |
|------|-------|---------|
| Class / struct / type alias | `PascalCase` | `class SceneManager`, `using TextureHandle = uint32_t` |
| Function / method | `camelCase` | `void loadMesh()`, `bool isVisible() const` |
| Local variable | `camelCase` | `int vertexCount` |
| Member variable | `m_camelCase` | `glm::vec3 m_position` |
| Static member | `s_camelCase` | `static Renderer* s_instance` |
| Global (avoid) | `g_camelCase` | `int g_windowWidth` |
| Constant | `UPPER_SNAKE_CASE` | `constexpr int MAX_LIGHTS = 16` |
| Macro | `VESTIGE_UPPER_SNAKE` | `#define VESTIGE_ASSERT(...)` |
| Enum class + values | `PascalCase` / `PascalCase` | `enum class RenderMode { Wireframe, Solid }` |
| Namespace | `PascalCase` | `namespace Vestige` |
| Template param | single uppercase or `PascalCase` | `template <typename T>` |
| Boolean | `is/has/can/should/was` prefix | `bool m_isVisible`, `bool hasTexture()` |

Short identifiers are fine in usual idioms: loop counters `i/j/k`, math locals `x/y/z/s/t`, iterators `it/end`, abbreviations `Hz/ts/rgb`. The `readability-identifier-length` check is disabled for this reason.

**Return-type style.** Classical (`float getDuration() const`) is the default and matches the ~2000 existing declarations. Use trailing-return (`auto name(args) -> T`) only when it genuinely helps: templates with dependent return types, lambdas, or nested-types scoped by the enclosing class. Do not convert simple declarations. `modernize-use-trailing-return-type` is disabled.

**Magic numbers.** Numeric literals tied to business-logic or tuning semantics must be named constants — apply in new code and during natural edits (hybrid adoption; no codebase-wide rewrite). Inline literals are fine for obvious-context math (matrix strides, vec3 loop bounds, 0.5/0.25/2.0 in geometry), small loop bounds, and test data. `cppcoreguidelines-avoid-magic-numbers` is disabled because it drowns real cases in noise — apply judgment per-site.

---

## 3. Code Formatting

- **Indentation:** 4 spaces, no tabs.
- **Line length:** soft 120 chars; break after commas / before operators.
- **Pointer/reference:** attach to type — `int* ptr`, `const std::string& name`, not `int *ptr`.
- **Spacing:** spaces around binary operators and after keywords (`if (x)`); no space before `(` in calls, no space inside `()`.
- **Blank lines:** one between methods, two between top-level sections, none trailing.

**Brace style — Allman, braces around every control-flow body:**
```cpp
if (base + 2 >= values.size())
{
    return glm::vec3(0.0f);
}
```
Rationale: prevents the Apple "goto fail" class of bug (CVE-2014-1266). Hybrid adoption: new code always braces; fix unbraced sites you touch; don't go out of your way to churn unmodified files. `readability-braces-around-statements` stays off until a dedicated cleanup sweep lands.

---

## 4. Header Files

Use `#pragma once`.

**Include order** (blank-line separated):
1. Corresponding header (in `.cpp` only)
2. Vestige engine headers
3. Third-party
4. Standard library

Prefer forward declarations over includes in headers when pointers/references suffice.

---

## 5. Comments

- `//` single-line, `/* ... */` multi-line with aligned asterisks.
- Inline comment: two spaces before `//`.
- Doxygen `///` with `@brief` / `@param` / `@return` on public APIs.
- File header: `/// @file foo.h` + `/// @brief ...` on every source/header.
- **Why, not what.** The code shows what happens. Don't comment obvious code.

---

## 6. Class Structure Order

```
public:     type aliases → ctors/dtor → methods → members (rare)
protected:  same order
private:    same order, members last
```

---

## 7. General Rules

**Must do:** `nullptr` (not `NULL`/`0`), `enum class`, `const`/`constexpr` wherever possible, `explicit` on single-arg ctors, RAII, Rule of Five when managing resources.

**Must not:** `using namespace` in headers, raw `new`/`delete`, C-style casts, unresolved warnings (`-Wall -Wextra -Wpedantic`).

**Prefer:** `auto` only when the type is obvious; range-based `for`; `std::string_view` for read-only string params; structured bindings where they improve clarity.

---

## 8. Shaders

File suffixes: `.vert.glsl`, `.frag.glsl`, `.geom.glsl`, `.comp.glsl`.

GLSL naming: uniforms `u_camelCase`, in/out `camelCase`, constants `UPPER_SNAKE_CASE`.

---

## 9. Scene Construction & Spatial Integrity

Placed objects must respect spatial constraints. Violations cause clipping, z-fighting, cloth crumpling, physics explosions.

### 9.1 Placement

| Rule | Description |
|------|-------------|
| No spawning inside colliders | Overlap at spawn causes constraint fighting (crumpling/explosion). |
| No overlapping static geometry | Check AABB overlap before placement. |
| Respect interior boundaries | Children fit within containing structure (account for wall thickness). |
| Collider margins | Dynamic objects ≥ 5 cm clearance from collider surfaces at rest. |
| Z-fighting prevention | Coplanar surfaces offset ≥ 1 cm, or use polygon offset. |

### 9.2 Collision Setup

| Rule | Description |
|------|-------------|
| Match colliders to geometry | Approximate actual shape; 2–5 cm padding. Oversized → float; undersized → clip. |
| Shared dimension constants | Collider dims reference the same variables as the mesh — single source of truth. |
| Avoid caging | Don't enclose dynamic objects in colliders on all sides; guide motion, don't trap it. |
| Cost awareness | Sphere = 1 dot + 1 length per particle per substep; plane = 1 dot. Trivially cheap — prefer correctness. |

### 9.3 Validation Checklist

- [ ] No dynamic objects spawn inside colliders
- [ ] ≥ 5 cm clearance from colliders
- [ ] Children fit within parents
- [ ] Collision shapes reference shared dimension constants
- [ ] No coplanar surfaces at identical depths
- [ ] Cloth panels sized to fit attachment points; collider spheres at nearby pillars/posts
- [ ] Visual inspection from multiple angles, rest + under load

---

## 10. Asset Naming

| Type | Convention | Example |
|------|-----------|---------|
| Texture | `snake_case` + type suffix | `gold_surface_diffuse.png` |
| Model | `snake_case` | `ark_of_covenant.glb` |
| Shader | `snake_case` + stage | `phong_lighting.vert.glsl` |
| Font | original name preserved | `roboto_regular.ttf` |
| Scene | `snake_case` | `tent_of_meeting.scene` |

---

## 11. Error Handling

| Situation | Use | Example |
|-----------|-----|---------|
| Value may be absent (no error semantics) | `std::optional<T>` | `std::optional<MeshHandle> findMesh(StringView)` |
| Operation succeeds with `T` or fails with `E` | `Result<T, E>` (custom) or `std::expected<T, E>` once GCC 13 / Clang 16 are baseline | `Result<Texture, IoError> loadTexture(...)` |
| System boundary (file I/O, OS, syscalls) | error code / `errno`-style return | wrap into `Result` at the next layer up |
| Programmer error / invariant breach | `VESTIGE_ASSERT` | not for user input |

No exceptions in 60-FPS hot paths (render loop, physics step, audio mix). Throws are acceptable in tools, loaders, and editor-only code where the budget is loose. `std::expected<T, E>` is the C++23 idiom but the engine's baseline still has to support older toolchains — track adoption via a single `engine/utils/result.h` alias so the migration is one find-and-replace.

Why: zero-cost happy path, errors are values not control flow, no `try/catch` clutter at every call-site.

Refs: [ISOCpp Guidelines E.1–E.6](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#S-errors) · [Sy Brand on functional error handling with C++23 optional/expected](https://devblogs.microsoft.com/cppblog/cpp23s-optional-and-expected/) · [cppreference std::expected](https://en.cppreference.com/cpp/utility/expected).

---

## 12. Memory Management

| Ownership | Use |
|-----------|-----|
| Single owner, heap | `std::unique_ptr<T>` + `std::make_unique<T>(...)` |
| Genuinely shared (rare) | `std::shared_ptr<T>` + `std::make_shared<T>(...)` |
| Non-owning observation | raw `T*` or `T&` (never `std::weak_ptr` unless paired with a real `shared_ptr`) |
| Optional non-owner that may outlive | check pattern: pointer + explicit null checks, or a generational handle |

Never `new` / `delete` directly in engine code. OpenGL / Jolt / GLFW handles wrap in RAII (see section 24). `std::shared_ptr` is the wrong default — if two systems both think they own a thing, the real bug is the missing single-owner.

Why: leaks are construction-site bugs; ownership in the type system means the compiler enforces it.

Ref: [ISOCpp Guidelines R.20–R.23](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#S-resource).

---

## 13. Threading & Concurrency

| Need | Use |
|------|-----|
| Cross-thread flag / counter | `std::atomic<T>` with explicit memory order when not seq-cst |
| Single mutex, scoped | `std::scoped_lock` (preferred over `std::lock_guard` since C++17 — same cost, deadlock-safe across multiple mutexes) |
| Wait / notify | `std::condition_variable` + `std::unique_lock` |
| Worker pool | subsystem-owned (job system, asset loader); no naked `std::thread` spawned from feature code |

Each subsystem's spec must answer: *which threads enter this code, and which locks do they hold*. If you cannot answer, the subsystem is not thread-safe; document it as main-thread-only.

`std::scoped_lock` accepts zero args and silently locks nothing — pass at least one mutex.

Refs: [ISOCpp Guidelines CP.20–CP.43](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#S-concurrency) · [cppreference std::scoped_lock](https://en.cppreference.com/w/cpp/thread/scoped_lock).

---

## 14. Logging

Levels: `Trace` / `Debug` / `Info` / `Warn` / `Error` / `Fatal`.

| Level | When |
|-------|------|
| Trace | per-frame / per-call instrumentation, off in release |
| Debug | dev-only diagnostics |
| Info | one-shot lifecycle events (subsystem init, scene load) |
| Warn | recoverable anomaly (missing texture → fallback) |
| Error | feature-level failure that should not silently continue |
| Fatal | invariant breach; terminate after flushing |

**No logs in render-loop hot paths.** 60 FPS = 16.6 ms; one `Info` line through the standard library can cost ~100 µs on Linux due to mutex + format. Hoist to once-per-second / once-per-event, or compile out via `if constexpr`.

Prefer structured fields when sensible: `logger.info("draw_call", {{"mesh", id}, {"verts", n}})`. Centralized in `engine/utils/logger.h` — never `std::cout` in shipped code.

---

## 15. Static Analysis & Warnings

- `.clang-tidy` at repo root is the source of truth.
- CI builds with `-Wall -Wextra -Wpedantic -Werror`.
- Suppress narrowly: `// NOLINT(check-name): reason`. Never blanket `// NOLINTBEGIN ... NOLINTEND` regions.
- CI-green clang-tidy warnings are advisory (project memory). Do not restart whack-a-mole suppression sweeps; fix on touch instead.

Why: a single inline suppression with a reason is greppable and survives re-scans; block suppressions hide later regressions.

---

## 16. Platform & Preprocessor

Macros set in `CMakeLists.txt`: `VESTIGE_PLATFORM_LINUX`, `VESTIGE_PLATFORM_WINDOWS`. Use these — never `#ifdef __linux__` ad-hoc.

| Pattern | Use |
|---------|-----|
| Platform-specific impl, same API | separate `.cpp` files, gated in CMake (e.g. `file_dialog_linux.cpp` / `file_dialog_windows.cpp`) |
| Tiny branch inside a function | `if constexpr (VESTIGE_PLATFORM_LINUX) { ... }` (C++17) |
| `#ifdef` in headers | avoid — it forces every consumer to know the platform; push to `.cpp` |

Why: header `#ifdef`s metastasize; consumers compile twice the surface area for half the targets.

Ref: [cppreference if constexpr](https://en.cppreference.com/w/cpp/language/if).

---

## 17. CPU / GPU Placement

Mirrors CLAUDE.md Rule 7. Decide at design-time, document the call in the subsystem spec under a "CPU / GPU placement" heading.

Default heuristic:

| Workload shape | Placement |
|----------------|-----------|
| Per-pixel / per-vertex / per-particle / per-froxel | GPU |
| Branching, sparse, decision-heavy | CPU |
| I/O, asset load, scene graph | CPU |
| Reduction over a large buffer | GPU compute |

Dual implementations (CPU spec + GPU runtime) are allowed when a parity test pins them. Do **not** ship "CPU for now, move later" — that becomes a rewrite. If you don't know yet, the design isn't done.

---

## 18. Public API vs Internal

| Path | Stability |
|------|-----------|
| `engine/<subsystem>/<subsystem>.h` (subsystem facade) | public; semver-respecting |
| `engine/<subsystem>/internal/...` | private; may break across commits |
| inside `.cpp` (anonymous namespace, file-local) | private |
| `engine/experimental/...` | opt-in, no stability guarantees |

Public APIs get full Doxygen (section 26). Internal helpers a one-line `///` is fine.

Why: a clear public-vs-internal split is what lets the engine ship semver and lets refactors land without breaking downstream.

---

## 19. Test Discipline

Every feature and every bug fix gets a test (project rule, locked in feedback memory). TDD per the `superpowers:test-driven-development` skill is the default workflow.

| Rule | |
|------|---|
| Location | `tests/test_<subject>.cpp` (matches section 1) |
| Structure | Arrange / Act / Assert, with blank lines separating |
| Randomness | deterministic seed at the top of the test; never wall-clock |
| Assertions | one logical concern per `EXPECT_*` (Google Test) |
| Shared setup | `TEST_F` fixtures, not file-static globals |
| Naming | `TEST(Subject, BehaviourUnderCondition)` — reads as a sentence |

Bug-fix tests reproduce the failure first, then go green with the fix.

---

## 20. TODO / FIXME / Commented Code

| Marker | Format |
|--------|--------|
| TODO | `// TODO(YYYY-MM-DD owner): description` |
| FIXME | `// FIXME(YYYY-MM-DD): bug description` |
| HACK | `// HACK(YYYY-MM-DD): why this exists, what would fix it` (see CLAUDE.md Rule 5 — must be a real workaround, not a polished-up neglect) |

Date is required so staleness sweeps work. Owner is your git username.

**No commented-out code on `main`.** Git history is the archive — `git log -S` finds it. Reviewers reject commented-out blocks.

---

## 21. GLSL Beyond Naming

Builds on section 8.

| Topic | Rule |
|-------|------|
| Precision | `highp` is the default in fragment shaders. Mesa AMD treats `mediump` as `highp` anyway, and explicit `highp` documents intent for portability to mobile/Vulkan-mobile later. |
| Early-Z | Avoid `discard` before depth write where possible. When alpha-test-style discards are needed, use `layout(early_fragment_tests) in;` to let the GPU keep early-Z. |
| UBO layout | Respect `std140` rules — `vec3` pads to `vec4`, arrays stride to 16 bytes. Mismatch = silent garbage uniforms. |
| SSBO | `std430` packs tighter than `std140`; use for compute / large buffers. |

**Sampler binding (Mesa AMD-specific, locked-in project knowledge):** ALL declared samplers must have a valid texture bound at draw time, even if the shader doesn't read them — otherwise `GL_INVALID_OPERATION`. Different sampler types (`sampler2D` vs `samplerCube`) at the same texture unit also error. Do not undo this; bind a 1×1 white fallback to every declared but unused sampler.

---

## 22. Dependency Injection & Globals

| Preference | Pattern |
|------------|---------|
| Default | constructor injection — `MyClass(SomeService& service)` |
| When a global is unavoidable | confine to `namespace Vestige::Globals { ... }` and document why next to the definition |
| Singletons | last resort; one paragraph in the spec defending the choice |

Avoid the singleton-of-singletons: a manager that owns a manager that owns a manager turns every test into a fight against globals.

Why: testability and parallel subsystem evolution. A class that names what it needs is a class that can be tested in isolation.

---

## 23. Strings & Encoding

- UTF-8 throughout: source files (BOM-less), runtime strings, file paths, log output.
- `std::string_view` for read-only string params (formalizes section 7).
- `std::filesystem::path` for filesystem paths in new code — never raw `std::string` for paths.
- Comparison / hashing of file paths goes through `std::filesystem::path`'s comparison operators on Windows (case-insensitive there) — don't roll your own.

Why: cross-platform path handling is the dragon at the bottom of every engine bug list. The standard library already solved it.

---

## 24. OpenGL Resource Lifetimes

Every OpenGL handle (texture, buffer, FBO, VAO, RBO, shader, program, sync object) wrapped in a RAII class.

```cpp
class GlTexture
{
public:
    GlTexture()                            { glGenTextures(1, &m_handle); }
    ~GlTexture()                           { if (m_handle) glDeleteTextures(1, &m_handle); }
    GlTexture(const GlTexture&)            = delete;
    GlTexture& operator=(const GlTexture&) = delete;
    GlTexture(GlTexture&& o) noexcept      : m_handle(std::exchange(o.m_handle, 0)) {}
    GlTexture& operator=(GlTexture&&) noexcept;
    GLuint id() const                      { return m_handle; }
private:
    GLuint m_handle = 0;
};
```

Move-only. Naked `GLuint` allowed only for one-frame ephemeral use; long-lived state never. Reference: existing `engine/renderer/gl_*.h` wrappers.

Why: leaks of GPU resources don't show up under valgrind — they show up as a 60-second freeze when the driver runs out of VRAM during a long session.

---

## 25. Build Hygiene

- Forward-declare in headers when a pointer / reference / `unique_ptr` (with out-of-line dtor) suffices — don't `#include` the whole subsystem.
- Avoid `#include "<heavy.h>"` (Jolt, GLM with everything, Vulkan headers) in public headers — push to `.cpp`.
- CMake target dependencies are minimal: a target that only uses GLM does not link Jolt. Link surface = ABI surface.
- Compile clean with `-Wmissing-include-dirs`. Every TU must self-contain its includes — no relying on transitive ones.

Why: a clean dependency graph is the difference between a 30-second incremental build and a 5-minute one.

---

## 26. Doxygen / API Docs

Public APIs (per section 18) get full Doxygen:

```cpp
/// @brief Loads a mesh from disk and uploads it to the GPU.
/// @param path UTF-8 file path, .glb / .gltf / .obj.
/// @param flags Loader flags (see MeshLoadFlags).
/// @return MeshHandle on success, IoError on failure.
Result<MeshHandle, IoError> loadMesh(const std::filesystem::path& path, MeshLoadFlags flags);
```

| Tag | When |
|-----|------|
| `@brief` | one line, every public symbol |
| `@param` | every parameter, even obvious ones (downstream tools key on this) |
| `@return` | every non-void return |
| `@throws` | only if the function actually throws (rare per section 11) |

File header `/// @file foo.h` + `/// @brief ...` on every public source/header (formalizes section 5). Internal helpers a one-line `///` suffices.
