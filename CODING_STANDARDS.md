# Vestige Coding Standards

This document defines the mandatory coding standards for the Vestige 3D Engine. All code must conform to these rules.

---

## 1. File Naming

| Type | Convention | Example |
|------|-----------|---------|
| C++ source files | `snake_case.cpp` | `scene_manager.cpp` |
| C++ header files | `snake_case.h` | `scene_manager.h` |
| Shader vertex files | `snake_case.vert.glsl` | `basic_lighting.vert.glsl` |
| Shader fragment files | `snake_case.frag.glsl` | `basic_lighting.frag.glsl` |
| Shader geometry files | `snake_case.geom.glsl` | `shadow_pass.geom.glsl` |
| CMake files | Standard CMake names | `CMakeLists.txt` |
| Test files | `test_snake_case.cpp` | `test_event_bus.cpp` |

**Rule:** One class per file. The file name matches the class name in snake_case.
- `SceneManager` class → `scene_manager.h` + `scene_manager.cpp`
- `EventBus` class → `event_bus.h` + `event_bus.cpp`

---

## 2. Naming Conventions

### Classes, Structs, and Type Aliases
```cpp
// PascalCase
class SceneManager;
struct VertexData;
using TextureHandle = uint32_t;
```

### Functions and Methods
```cpp
// camelCase
void loadMesh(const std::string& path);
glm::vec3 getPosition() const;
bool isVisible() const;
```

### Variables
```cpp
// Local variables: camelCase
int vertexCount = 0;
float deltaTime = 0.0f;

// Member variables: m_ prefix + camelCase
glm::vec3 m_position;
GLuint m_vertexBuffer;
bool m_isInitialized;

// Static members: s_ prefix + camelCase
static Renderer* s_instance;

// Global variables (avoid when possible): g_ prefix + camelCase
int g_windowWidth;
```

### Constants and Macros
```cpp
// Constants: UPPER_SNAKE_CASE
constexpr int MAX_LIGHTS = 16;
constexpr float DEFAULT_FOV = 45.0f;

// Macros: VESTIGE_ prefix + UPPER_SNAKE_CASE
#define VESTIGE_VERSION_MAJOR 0
#define VESTIGE_ASSERT(condition) ...
```

### Enums
```cpp
// Always use enum class (scoped enums)
// Type: PascalCase, Values: PascalCase
enum class RenderMode
{
    Wireframe,
    Solid,
    Textured
};

enum class LightType
{
    Directional,
    Point,
    Spot
};
```

### Namespaces
```cpp
// PascalCase
namespace Vestige
{
    namespace Renderer { ... }
    namespace Core { ... }
}
```

### Template Parameters
```cpp
// Single uppercase letter or PascalCase
template <typename T>
template <typename ValueType>
```

### Boolean Naming
```cpp
// Always prefix with: is, has, can, should, was
bool m_isVisible;
bool hasTexture;
bool canRender();
bool shouldUpdate();
```

---

## 3. Code Formatting

### Indentation
- **4 spaces** per indent level
- **No tabs** — configure your editor to insert spaces

### Brace Style
Allman style — opening brace on its own line:
```cpp
if (condition)
{
    doSomething();
}
else
{
    doSomethingElse();
}

for (int i = 0; i < count; i++)
{
    process(i);
}

class Camera
{
public:
    void update(float deltaTime);

private:
    glm::vec3 m_position;
};
```

### Line Length
- **Soft limit: 120 characters**
- Break long lines at logical points (after commas, before operators)

### Pointer and Reference Alignment
Attach to the type, not the variable:
```cpp
// Correct
int* ptr;
const std::string& name;
Mesh* loadMesh();

// Wrong
int *ptr;
const std::string &name;
```

### Spacing
```cpp
// Spaces around binary operators
int result = a + b;
bool check = (x > 0) && (y < 10);

// No space before parentheses in function calls
doSomething(arg1, arg2);

// Space after keywords
if (condition)
while (running)
for (int i = 0; i < n; i++)

// No spaces inside parentheses
// Correct: doSomething(arg)
// Wrong:   doSomething( arg )
```

### Blank Lines
- One blank line between methods in a source file
- Two blank lines between sections (e.g., between class definitions in a header)
- No trailing blank lines at end of file

---

## 4. Header File Structure

### Include Guards
Use `#pragma once` — modern, clean, supported by all target compilers:
```cpp
#pragma once
```

### Include Order
Separated by blank lines, in this order:
1. Corresponding header (in `.cpp` files only)
2. Vestige engine headers
3. Third-party library headers
4. Standard library headers

```cpp
// In renderer.cpp:
#include "renderer/renderer.h"        // 1. Corresponding header

#include "core/logger.h"              // 2. Vestige headers
#include "core/window.h"
#include "renderer/shader.h"

#include <GLFW/glfw3.h>               // 3. Third-party
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>                    // 4. Standard library
#include <string>
#include <vector>
```

### Forward Declarations
Prefer forward declarations over includes in headers when possible:
```cpp
// In scene_manager.h — avoid including the full entity.h if only using pointers/references
namespace Vestige
{
    class Entity;  // Forward declaration
}
```

---

## 5. Comment Style

### Single-Line Comments
```cpp
// This is a single-line comment
int count = 0;  // Inline comment (2 spaces before //)
```

### Multi-Line Comments
```cpp
/*
 * Multi-line comments use this style
 * with aligned asterisks.
 */
```

### Documentation Comments (Doxygen)
```cpp
/// @brief Manages the active scene graph and entity lifecycle.
/// @details Handles creation, destruction, and updating of all
///          entities within the current scene.
class SceneManager
{
public:
    /// @brief Updates all entities in the active scene.
    /// @param deltaTime Time elapsed since last frame in seconds.
    void update(float deltaTime);

    /// @brief Loads a scene from a file.
    /// @param filePath Path to the scene file.
    /// @return True if the scene was loaded successfully.
    bool loadScene(const std::string& filePath);
};
```

### File Header Comment
Every source and header file starts with:
```cpp
/// @file scene_manager.h
/// @brief Scene management and entity lifecycle.
```

### Comment Philosophy
- Comment **why**, not **what** — the code shows what happens
- Every file gets a brief file header comment
- Public API methods get `@brief` documentation
- Complex algorithms get explanatory comments
- Do not comment obvious code

---

## 6. Class Structure Order

Members within a class follow this order:
```cpp
class ExampleClass
{
public:
    // 1. Type aliases and nested types
    using Callback = std::function<void()>;

    // 2. Constructors and destructor
    ExampleClass();
    ~ExampleClass();

    // 3. Public methods
    void update(float deltaTime);
    bool isReady() const;

    // 4. Public member variables (rare — prefer getters)

protected:
    // Same order as public

private:
    // Same order as public

    // Private member variables last
    float m_deltaTime;
    bool m_isReady;
};
```

---

## 7. General Rules

### Must Do
- Use `nullptr` instead of `NULL` or `0`
- Use `enum class` instead of plain `enum`
- Use `const` wherever possible (parameters, methods, variables)
- Use `constexpr` for compile-time constants
- Mark single-argument constructors `explicit` to prevent implicit conversions
- Use RAII — acquire resources in constructors, release in destructors
- Delete copy constructors/assignment when a class manages resources (Rule of Five)

### Must Not
- Never use `using namespace` in header files
- Never use raw `new`/`delete` — use smart pointers (`std::unique_ptr`, `std::shared_ptr`)
- Never use C-style casts — use `static_cast`, `dynamic_cast`, `reinterpret_cast`
- Never leave warnings unresolved — compile with `-Wall -Wextra -Wpedantic`

### Prefer
- `auto` only when the type is obvious: `auto it = map.begin();` (OK), `auto x = getValue();` (not OK — unclear type)
- Range-based for loops over index-based when possible
- `std::string_view` for read-only string parameters (C++17)
- Structured bindings where they improve clarity: `auto [x, y, z] = getPosition();`

---

## 8. Shader Conventions

### File Naming
- `shader_name.vert.glsl` — vertex shader
- `shader_name.frag.glsl` — fragment shader
- `shader_name.geom.glsl` — geometry shader
- `shader_name.comp.glsl` — compute shader

### GLSL Naming
```glsl
// Uniforms: u_ prefix + camelCase
uniform mat4 u_modelMatrix;
uniform vec3 u_lightPosition;

// Inputs/Outputs: camelCase (match attribute names)
in vec3 position;
in vec2 texCoord;
out vec4 fragColor;

// Constants: UPPER_SNAKE_CASE
const int MAX_LIGHTS = 16;
```

---

## 9. Asset Naming

| Asset Type | Convention | Example |
|-----------|-----------|---------|
| Textures | `snake_case` + type suffix | `gold_surface_diffuse.png`, `stone_wall_normal.png` |
| Models | `snake_case` | `altar_of_incense.obj`, `ark_of_covenant.glb` |
| Shaders | `snake_case` + stage suffix | `phong_lighting.vert.glsl` |
| Fonts | Original name preserved | `roboto_regular.ttf` |
| Scenes | `snake_case` | `tent_of_meeting.scene` |
