# Vestige Coding Standards

Mandatory rules. All new code must conform.

---

## 0. Language Standard

**C++17 baseline.** All translation units must compile with `-std=c++17` (matches `CMakeLists.txt` `CMAKE_CXX_STANDARD 17`). C++20 features adopted selectively when uniformly available across GCC 13+ / Clang 18+ / MSVC 19.36+: designated initializers, concepts where they reduce SFINAE noise. `[[likely]]` / `[[unlikely]]` only on hot-path branches with measured benefit (compiler honour-rate varies). `std::span` is canonical for new code (see section 7) — that is no longer "selective." C++23 features (`std::expected`, `std::print`) on a case-by-case basis with a documented fallback — see section 11 for `std::expected` portability.

**GLSL: 450 core**, matching the engine's OpenGL 4.5 target. Compute shaders use `#version 450 core` with explicit `layout(local_size_x = ...)`.

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

**Numerical-design constants belong in the Formula Workbench.** Per CLAUDE.md Rule 6, tuning coefficients (drag, friction, attenuation, fog density, wave amplitude, fitting curves, …) are authored / fit / validated in `tools/formula_workbench/` and exported to JSON / generated code, not hand-typed into headers. Hand-coded constants only when no reference data exists — leave a `// TODO(YYYY-MM-DD): revisit via Formula Workbench` per section 20.

---

## 3. Code Formatting

**Formatting source of truth: `.clang-format` at the repo root.** `clang-format -i path/to/file.cpp` formats any file into compliance. The rules below describe the format; the tool enforces it.

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
- File header: `/// @file foo.h` + `/// @brief ...` on every public source/header (see section 18 for what counts as public). Internal helpers (under `internal/`, in `.cpp`-only files) need only a one-line `///`.
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

**Must not:** `using namespace` in headers, raw `new`/`delete`, C-style casts, unresolved warnings (`-Wall -Wextra -Wpedantic`), `throw` inside the render / physics / audio-mix loops (see section 11 for the policy and the rationale — exceptions are allowed only outside the steady-state hot paths).

**Prefer:** `auto` only when the type is obvious; range-based `for`; structured bindings where they improve clarity. `std::string_view` for read-only string params (canonical rule in section 23). `std::span<T>` for non-owning array views — no more `(ptr, len)` pairs in new code.

**Modern C++ idioms.** Use the current syntax for the version actually in use, not the syntax from three years ago even if it still compiles. Specifics:

| Topic | Rule |
|-------|------|
| `[[nodiscard]]` | every non-void public-API return where ignoring is a likely bug (loaders, builders, results) |
| `[[maybe_unused]]` | parameters / vars whose use is conditional on `if constexpr` or platform |
| `[[likely]]` / `[[unlikely]]` (C++20) | only on hot-path branches with measured benefit; not for "I think this is rare" |
| `noexcept` | mark functions that genuinely cannot throw — destructors, swap, move ops, pure-math helpers. Exposes optimisations and contractually documents intent. **Render-loop / physics-step / audio-mix functions** should be `noexcept` end-to-end (see section 11): the policy bans `throw` there, so the noexcept hint is free, makes container moves cheaper, and turns a future stray `throw` into a `std::terminate` rather than a silent budget bomb. |
| Trailing return | classical `T name()` is the default per section 2 (`modernize-use-trailing-return-type` is disabled). Use `auto name(...) -> T` only for templates with dependent return types, lambdas, and nested-types scoped by the enclosing class. |
| Fixed-width integers | `int32_t` / `uint32_t` / `size_t` for ABI / serialisation / bit-twiddling; `int` / `size_t` for plain counters |
| Move semantics | Rule of Zero is the default — let the compiler generate. Define `= default` or `= delete` explicitly only when the compiler can't, or when you need to suppress copy. Rule of Five only for resource-managing classes (see section 24). |

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
| Operation succeeds with `T` or fails with `E` | `Result<T, E>` (engine alias, see below) or `std::expected<T, E>` directly | `Result<Texture, IoError> loadTexture(...)` |
| System boundary (file I/O, OS, syscalls) | error code / `errno`-style return | wrap into `Result` at the next layer up |
| Programmer error / invariant breach | `VESTIGE_ASSERT` | not for user input |

`std::expected<T, E>` is C++23 and supported on the engine's GCC 13+ / **Clang 18+** / MSVC 19.36+ baseline. (Clang 16's libc++ shipped `<expected>` but had a broken `__cpp_lib_expected` feature-test macro and missing monadic ops until Clang 18 — see [LLVM #108011](https://github.com/llvm/llvm-project/issues/108011). Do not rely on `__cpp_lib_expected` to gate the codepath; gate on `__clang_major__ >= 18` for libc++ builds.) When `engine/utils/result.h` lands it will define `template <class T, class E> using Result = std::expected<T, E>;` plus a `Result<void, E>` alias — until that file exists, use the standard name directly and file an issue for the alias.

**No exceptions in 60-FPS hot paths** (render loop, physics step, audio mix). Throws are acceptable in tools, loaders, and editor-only code where the frame budget is loose.

Why: a `throw` is unbounded — stack unwinding, allocation for the exception object, destructor chain across N frames of stack. Errors-as-values keep the *error* path as predictable as the happy path; the 16.6 ms / frame budget can absorb a missing texture but not a 200 µs allocator hit on a freed-fence-not-found error. (The "zero-cost happy path" of itanium-ABI exceptions is a defence *of* exceptions for code that almost never throws — but the moment `throw` fires inside the render loop, you blow the budget.)

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
| Single mutex, scoped | `std::lock_guard<std::mutex>` (clear single-mutex intent; compile error if you forget the mutex argument) |
| Multiple mutexes, scoped | `std::scoped_lock(m1, m2, ...)` (C++17, deadlock-safe — but only when you actually hold 2+ mutexes) |
| Wait / notify | `std::condition_variable` + `std::unique_lock` |
| Worker pool | subsystem-owned (job system, asset loader); no naked `std::thread` spawned from feature code |

Each subsystem's spec must answer: *which threads enter this code, and which locks do they hold*. If you cannot answer, the subsystem is not thread-safe; document it as main-thread-only.

`std::scoped_lock` with zero args silently locks nothing — that footgun is exactly why single-mutex sites prefer `lock_guard` (the latter is a compile error without a mutex argument). Reach for `scoped_lock` only when there really are 2+ mutexes to take atomically.

Refs: [ISOCpp Guidelines CP.20–CP.43](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#S-concurrency) · [cppreference std::scoped_lock](https://en.cppreference.com/w/cpp/thread/scoped_lock) · [LLVM #107839: clang-tidy prefer-scoped-lock-over-lock-guard pushback](https://github.com/llvm/llvm-project/issues/107839).

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

- `.clang-format` at repo root enforces formatting (section 3).
- A `.clang-tidy` at repo root is **planned but not yet present**. Until it lands, this doc is the policy and the table below is the canonical disabled-checks list. Treat clang-tidy invocation as per-developer; CI does not currently fail on clang-tidy findings (only on compiler warnings, which *are* `-Werror`).

| Disabled check | Rationale | Defined in |
|----------------|-----------|------------|
| `cppcoreguidelines-avoid-magic-numbers` | drowns real cases in noise; per-site judgment with named constants for business logic | §2 Magic numbers |
| `readability-identifier-length` | math/loop locals (`i`, `x`, `dt`) are idiomatic | §2 Naming Conventions |
| `modernize-use-trailing-return-type` | classical return-type style matches ~2000 existing declarations; trailing-return only for templates / lambdas / nested types | §2 Return-type style + §7 modern-idioms table |
| `readability-braces-around-statements` | hybrid adoption — fix on touch, no codebase-wide rewrite | §3 Code Formatting |
- CI builds with `-Wall -Wextra -Wpedantic -Werror` — warnings *are* gates.
- Suppress narrowly: `// NOLINT(check-name): reason`. Never blanket `// NOLINTBEGIN ... NOLINTEND` regions.
- CI-green clang-tidy warnings are advisory (project memory). Do not restart whack-a-mole suppression sweeps; fix on touch instead.

Why: a single inline suppression with a reason is greppable and survives re-scans; block suppressions hide later regressions.

---

## 16. Platform & Preprocessor

Macros set in `CMakeLists.txt`:

| Macro | Set when | Used by |
|-------|----------|---------|
| `VESTIGE_PLATFORM_LINUX` | `CMAKE_SYSTEM_NAME` is `Linux` | platform-gated `.cpp`s, runtime branches |
| `VESTIGE_PLATFORM_WINDOWS` | `CMAKE_SYSTEM_NAME` is `Windows` | platform-gated `.cpp`s, runtime branches |
| `VESTIGE_EDITOR` | editor builds (default in dev, off in shipped runtimes) | editor-only code per §33 |

Use these — never `#ifdef __linux__` ad-hoc.

| Pattern | Use |
|---------|-----|
| Platform-specific impl, same API | separate `.cpp` files, gated in CMake (e.g. `file_dialog_linux.cpp` / `file_dialog_windows.cpp`) |
| Tiny branch inside a function | `#if defined(VESTIGE_PLATFORM_LINUX)` ... `#endif`, or convert the macro to a `constexpr bool` constant in a shared `engine/utils/platform.h` and use `if constexpr (kPlatformLinux)` |
| `#ifdef` in headers | avoid — it forces every consumer to know the platform; push to `.cpp` |

Why: header `#ifdef`s metastasize; consumers compile twice the surface area for half the targets.

**Caveat:** `VESTIGE_PLATFORM_LINUX` / `VESTIGE_PLATFORM_WINDOWS` are CMake `add_compile_definitions` macros — they are **not** `constexpr bool` constants, so plain `if constexpr (VESTIGE_PLATFORM_LINUX)` does *not* compile. Use `#if`-gating, or define mirrored `inline constexpr bool kPlatformLinux = ...;` constants in a shared header and use those in `if constexpr`.

Ref: [cppreference if constexpr](https://en.cppreference.com/w/cpp/language/if).

---

## 17. CPU / GPU Placement

**This section is canonical.** CLAUDE.md Rule 7 states the high-level policy ("decide at design-time, document in spec"); the engineering detail lives here. When CLAUDE.md and this section appear to disagree, the discrepancy is a bug — fix this section first, then update CLAUDE.md's pointer.

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

Every feature and every bug fix gets a test (project rule, locked in feedback memory). TDD via the `superpowers:test-driven-development` skill is the default for: regression tests for bug fixes, invariants, parsers, formula evaluators, and any code with a clear input → output contract. For one-shot glue (a single editor-button hookup), throwaway exploration, or rendering-output features that need visual confirmation, write the test *after* the code passes the eye-check — but still write one. The "shortest correct implementation" rule (global rule 2) does not exempt tests; it just means don't over-build *production* code for hypotheticals.

| Rule | |
|------|---|
| Location | `tests/test_<subject>.cpp` (matches section 1) |
| Structure | Arrange / Act / Assert, with blank lines separating |
| Randomness | deterministic seed at the top of the test; never wall-clock |
| Assertions | one logical concern per `EXPECT_*` (Google Test) |
| Shared setup | `TEST_F` fixtures, not file-static globals |
| Naming | `TEST(Subject, BehaviourUnderCondition)` — reads as a sentence |
| Wiring | CMake `gtest_discover_tests(target)` auto-registers every `TEST` in a target — no manual `add_test` per test |
| Layout | `tests/` mirrors `engine/` one-to-one: `engine/foo/bar.cpp` ↔ `tests/test_foo_bar.cpp` |

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

**Enforcement:** when a new uniform sampler is declared in GLSL but no host-side bind is wired, the shader-compile / link step warns at startup. If you hit a `GL_INVALID_OPERATION` on a draw call, the first thing to check is "did I add a sampler to the shader and forget to bind a fallback?" — see `engine/renderer/` for the existing fallback texture handles (white, black, normal-up).

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
- `std::string_view` for read-only string params (the canonical home for the rule — section 7 mentions it; this is the binding statement).
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
    GlTexture()                            { glGenTextures(1, &m_handle); }      // single-stmt inline body — §3 Allman exception
    ~GlTexture()                           { if (m_handle) glDeleteTextures(1, &m_handle); }
    GlTexture(const GlTexture&)            = delete;
    GlTexture& operator=(const GlTexture&) = delete;
    GlTexture(GlTexture&& o) noexcept      : m_handle(std::exchange(o.m_handle, 0)) {}
    GlTexture& operator=(GlTexture&& o) noexcept
    {
        if (this != &o)
        {
            if (m_handle) glDeleteTextures(1, &m_handle);
            m_handle = std::exchange(o.m_handle, 0);
        }
        return *this;
    }
    GLuint id() const                      { return m_handle; }
private:
    GLuint m_handle = 0;
};
```

Move-only. Naked `GLuint` allowed only for one-frame ephemeral use; long-lived state never. Reference: existing `engine/renderer/gl_*.h` wrappers.

**Sentinel caveat:** OpenGL uses `0` as "no object," so the `if (m_handle)` guards above are correct *for OpenGL only*. **Do not copy this pattern to Vulkan** (where `VK_NULL_HANDLE` is the sentinel and may be a valid `0` on 64-bit but is `(uint64_t)-1` on some platforms) **or to Jolt** (where `JPH::BodyID()` is the sentinel and zero is a valid body index). Each backend gets its own sentinel comparison.

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

---

## 27. Units & Coordinate Conventions

| Convention | Value |
|------------|-------|
| World units | metres (1.0f = 1 m), Y-up, right-handed |
| Time | seconds (`float dt`), monotonic from engine start |
| Angles | radians at the API boundary; degrees only in UI / serialised configs (convert at the boundary) |
| Mass | kilograms |
| Force | newtons (kg·m/s²); impulses in N·s |
| Spatial integrity tolerances | section 9 — placement clearances in centimetres; do not change without an explicit reason |
| Coordinate system | right-handed, Y up, +Z toward camera (matches GLM's default LH/RH choice — `GLM_FORCE_LEFT_HANDED` is **not** set) |

These are physical-world units, not pixels or arbitrary "engine units." Pinning them avoids the integration-bug class where two subsystems agree on the *number* but disagree on what the number means.

Why: every subsystem can integrate against a single mental model. A wind force in N reads identically whether it lands on cloth, particles, or a rigid body.

---

## 28. License Headers (SPDX)

Every source file (C++, GLSL, CMake, Python tools) gets a two-line header. Use **the year the file is created** — the example below shows `<YYYY>` for that reason; a real file written today has the literal year:

```cpp
// Copyright (c) <YYYY> Anthony Schemel
// SPDX-License-Identifier: MIT
```

For shaders:
```glsl
// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT
#version 450 core
```

For CMake / Python:
```cmake
# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT
```

The SPDX line is machine-readable per [SPDX 2.3](https://spdx.dev/use/specifications/) — it lets license-scanning tools (REUSE, FOSSology, GitHub's License API) classify the file without parsing prose.

Hybrid adoption: every new file gets the header; existing files gain it on natural edits. The open-source release (engine going MIT, biblical projects stay private) needs every public-repo file SPDX-tagged before tagging — see `docs/PRE_OPEN_SOURCE_AUDIT.md`.

Year: per [REUSE 3.0](https://reuse.software/spec-3.0/), the year of first publication is the canonical entry; multi-year ranges (`2024-2026`) only when there is a real per-year contribution history to assert. Bump on substantive contribution if the contributor wants copyright protection on those changes; trivial edits (typo fixes) do not bump.

---

## 29. GPU Debug Markers

For the 60 FPS budget to stay observable, every render pass and major subsystem must emit GPU debug labels for RenderDoc / apitrace / Nsight captures.

| API | Use |
|-----|-----|
| Object naming | `glObjectLabel(GL_TEXTURE, id, -1, "albedo_tabernacle_wall")` after every resource creation |
| Pass scoping | `glPushDebugGroup(...)` at pass entry, `glPopDebugGroup()` at exit — RAII helper preferred |
| Annotated submits | one debug-group per "thing on screen" so a captured frame reads as a list of named operations, not anonymous draw calls |

Debug markers are **free in release builds** when the debug-output extension is not active (`KHR_debug` is a no-op when the context wasn't created with `GL_CONTEXT_FLAG_DEBUG_BIT`). Always emit them.

Why: when frametime regresses, the first question is "which pass regressed?" Without markers the answer is "we have to instrument it first." With markers it's a 10-second RenderDoc capture.

---

## 30. Jolt Physics Conventions

Jolt is the engine's physics backend (`engine/physics/`). Jolt-touching code follows these rules in addition to the generic ones above.

| Topic | Rule |
|-------|------|
| Body creation | through `PhysicsWorld::createStaticBody` / `createDynamicBody` / `createKinematicBody`; never `BodyInterface::CreateBody` directly from feature code |
| Body IDs | `JPH::BodyID()` is the sentinel — use `id.IsInvalid()`, never `id == 0` |
| Layers | `BroadPhaseLayer` and `ObjectLayer` constants live in `engine/physics/physics_layers.h` — feature code references named constants, never raw integers |
| Step | the engine's configured fixed timestep (`PhysicsWorldConfig::fixedTimestep`, set at world-init time); never call `Update` with a variable `dt` from the render loop |
| Coordinate convention | matches the engine: metres, Y-up, right-handed (CODING_STANDARDS section 27). Jolt operates natively in these conventions; `engine/physics/jolt_helpers.h` provides component-wise `glm::vec3 ↔ JPH::Vec3`, `glm::quat ↔ JPH::Quat`, `glm::mat4 ↔ JPH::Mat44` conversions (no axis-flip, no unit-scale fixup) |
| Allocator | Jolt's `TempAllocatorImpl` is shared per `PhysicsWorld`; do not create per-call `TempAllocator` instances |

Float-determinism: physics translation units must **not** compile with `-ffast-math` (or `/fp:fast`). Replay / save-game parity depends on bit-identical IEEE-754 behaviour; fast-math reorders FMAs and breaks NaN propagation.

---

## 31. GL State Discipline

OpenGL is a giant pile of global state. Project knowledge has accumulated several locked-in rules.

| State | Rule |
|-------|------|
| `glClipControl` | shadow passes that switch to `GL_NEGATIVE_ONE_TO_ONE` **must** restore `GL_ZERO_TO_ONE` before the next pass — clip control is global, leakage causes silent NDC-space bugs (project memory) |
| Sampler binding | see section 21 — single source of truth for the rule and the 1×1-white-fallback enforcement pattern |
| Viewport / scissor | render passes set their own viewport + scissor at entry; never assume the previous pass left them sane |
| Blend / depth / stencil state | restore to the engine's "neutral" pipeline state at pass exit when a subsystem-specific override was applied |
| Bound FBO | render passes target their own FBO, restore to default (`0`) only at the end of the frame |
| Bound program | invalid after any `glDelete*` of a referenced resource — re-bind on the next draw |

A "render-pass-as-RAII" wrapper that snapshots and restores state is the long-term cleanup; until then, treat every `glXxx` state setter as a contract you must unwind.

---

## 32. Asset Paths

All asset paths are relative to a configured asset root; never raw filesystem strings or absolute paths in code. Today the engine passes the asset path explicitly (e.g. `Vestige::captionMapPath(assetPath)` in `engine/core/engine_paths.h`); a future `EnginePaths::assetRoot()` accessor will land alongside the install-layout work and unify dev-tree vs install-tree resolution.

```cpp
// good — path joins live in engine_paths helpers, callers pass the configured root
auto path = Vestige::captionMapPath(m_assetPath);

// good (once EnginePaths::assetRoot() exists)
auto path = Vestige::EnginePaths::assetRoot() / "models" / "ark_of_covenant.glb";

// bad — fails when the user runs from a different CWD or after install
auto path = std::filesystem::path("models/ark_of_covenant.glb");
```

This becomes load-bearing post-MIT release (Phase 12 distribution): downstream users will run binaries from arbitrary CWDs, and any code that assumes "relative paths work because we always launch from the repo root" silently breaks.

Path comparison and hashing: through `std::filesystem::path` operators (case-insensitive on Windows). Asset IDs that key into a registry use the canonical `path::lexically_normal()` form so `models/foo.glb` and `models/./foo.glb` resolve to the same asset.

---

## 33. Editor-Runtime Boundary

The editor (`engine/editor/`) is real-time WYSIWYG (project memory `feedback_editor_realtime.md`). Editor-driven changes apply within one frame; there is **no bake step on the runtime side**.

Implications for code in either subsystem:
- Runtime data structures support live mutation (no "rebuild on save" assumptions).
- Editor commands write to the runtime through the EventBus or domain-system APIs — never by reaching into private state via friendship.
- "Save" is a serialisation event, not a recompute trigger. Loading a saved scene is identical to live-editing the same scene to the same shape.
- Editor-only code (panels, gizmos, undo stack) is gated by `#ifdef VESTIGE_EDITOR` or lives in `engine/editor/` and is excluded from runtime-only builds.

Why: the project's primary use case (architectural walkthroughs) is iterated against — "place a column, look at it, adjust" must be a sub-second loop, not a save/reload cycle.
