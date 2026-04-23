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
