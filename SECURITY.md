# Vestige Security Standards

This document defines the security standards for the Vestige 3D Engine. Since the engine is a commercial product targeting Steam distribution, security must be considered at every stage of development.

---

## 1. Memory Safety

C++ provides no automatic memory safety. These rules mitigate the most common vulnerabilities.

### Smart Pointers — No Raw Ownership
```cpp
// Correct — smart pointers manage lifetime
std::unique_ptr<Mesh> mesh = std::make_unique<Mesh>();
std::shared_ptr<Texture> texture = std::make_shared<Texture>();

// Wrong — raw new/delete is forbidden for ownership
Mesh* mesh = new Mesh();  // Never do this
delete mesh;              // Never do this
```

### Rules
- **No raw `new`/`delete`** — use `std::unique_ptr` and `std::shared_ptr`
- **No raw arrays** — use `std::vector`, `std::array`, or `std::span`
- **No C-style string functions** (`strcpy`, `strcat`, `sprintf`) — use `std::string` and `std::format`/`std::snprintf`
- **Always initialize variables** — uninitialized variables are undefined behavior
- **Check all pointer access** — validate before dereferencing, or use references where possible
- **Use RAII for all resources** — GPU buffers, file handles, OpenGL objects must be released in destructors

### Buffer Overflow Prevention
```cpp
// Correct — bounds-checked access
std::vector<int> data = {1, 2, 3};
int value = data.at(index);  // Throws std::out_of_range if invalid

// Avoid — unchecked access in non-performance-critical code
int value = data[index];  // Only use in hot loops where bounds are guaranteed
```

- Use `.at()` for general access (bounds-checked)
- Use `[]` only in performance-critical inner loops where the index is provably valid
- Never access memory beyond allocated bounds

---

## 2. Input Validation

All data entering the engine from external sources must be validated.

### File Loading (Models, Textures, Shaders, Scenes)
- **Validate file headers/magic bytes** before parsing
- **Check file sizes** — reject files larger than reasonable limits
- **Validate all numeric ranges** — vertex counts, texture dimensions, array indices
- **Handle malformed data gracefully** — log an error and return a fallback, never crash
- **No assumptions about file content** — treat all loaded files as potentially corrupted

```cpp
// Example: validate before using
bool loadMesh(const std::string& path)
{
    auto fileSize = getFileSize(path);
    if (fileSize == 0 || fileSize > MAX_MESH_FILE_SIZE)
    {
        Logger::error("Mesh file invalid or too large: {}", path);
        return false;
    }
    // Parse with validation at every step...
}
```

### Path Traversal Prevention
- **Sanitize all file paths** — reject paths containing `..`, absolute paths when relative is expected
- **Confine asset loading to the assets directory** — never allow loading from arbitrary filesystem locations
- **Validate file extensions** match expected types

```cpp
// Validate that a path stays within the assets directory
bool isPathSafe(const std::string& basePath, const std::string& requestedPath)
{
    auto resolved = std::filesystem::canonical(basePath / requestedPath);
    auto base = std::filesystem::canonical(basePath);
    return resolved.string().starts_with(base.string());
}
```

### User Input (Keyboard, Mouse, Gamepad)
- **Clamp all input values** to valid ranges
- **Rate-limit input processing** — prevent input flooding
- **Validate gamepad axis values** — GLFW should provide [-1, 1] but verify

---

## 3. Shader Security

Shaders run on the GPU and can crash drivers or hang the system if malformed.

### Rules
- **Always check shader compilation status** — handle errors gracefully
- **Always check program linking status** — handle errors gracefully
- **Log full shader error messages** for debugging
- **Validate uniform locations** before setting values
- **Use bounded loops in shaders** — never use unbounded loops that could hang the GPU

```cpp
// Always check compilation
GLint success;
glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
if (!success)
{
    char infoLog[512];
    glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
    Logger::error("Shader compilation failed: {}", infoLog);
    return false;
}
```

---

## 4. OpenGL State Safety

Incorrect OpenGL usage can cause crashes, visual corruption, or driver hangs.

### Rules
- **Check for OpenGL errors** in debug builds after significant operations
- **Always unbind resources** after use to prevent state leakage
- **Validate framebuffer completeness** before rendering to it
- **Never exceed hardware limits** — query `GL_MAX_*` values and respect them

```cpp
// Debug-only OpenGL error checking
#ifdef VESTIGE_DEBUG
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR)
    {
        Logger::error("OpenGL error: 0x{:X}", err);
    }
#endif
```

---

## 5. Dependency Security

Third-party libraries can introduce vulnerabilities.

### Rules
- **Pin dependency versions** in CMake — never use "latest" without review
- **Only use well-maintained, reputable libraries** with permissive licenses
- **Review changelogs** when updating dependencies
- **Fetch dependencies via CMake FetchContent** with specific commit hashes or tags
- **No downloading executables** from the internet at build time

### Current Approved Dependencies
| Library | Version Strategy | Source |
|---------|-----------------|--------|
| GLFW | Tagged release | GitHub |
| GLM | Tagged release | GitHub |
| glad | Generated, committed | glad generator |
| stb_image | Specific commit | GitHub |
| Google Test | Tagged release | GitHub |
| Jolt Physics | Tagged release | GitHub |
| Recast Navigation | Tagged release | GitHub |
| OpenAL-Soft | System package | kcat/openal-soft |
| FreeType | FetchContent | GitHub |
| nlohmann/json | Tagged release | GitHub |
| dr_libs | Specific commit | GitHub |
| imgui | Embedded in external/ | GitHub |

### Known CVEs and Mitigations (Reviewed 2026-04-11)

| Library | CVE / Issue | Severity | Mitigation |
|---------|-------------|----------|------------|
| **FreeType** | CVE-2025-27363 (OOB write, actively exploited) | HIGH | Update to FreeType >= 2.13.1. Monitor for CVE-2026-23865 (fixed in 2.14.2) |
| **stb_image** | GHSL-2023-145 through GHSL-2023-151 (7 vulns incl. double-free) | HIGH | Validate dimensions after load (done: 16384x16384 max). Never load untrusted images on background threads without mutex. Monitor upstream |
| **dr_libs** | CVE-2025-14369 (integer overflow in dr_flac) | HIGH | Validate audio metadata before decoding (done: sample rate, channel, frame limits). Update to latest master post-March 2026 |
| **dr_libs** | Issue #296 (heap overflow in drwav smpl chunk) | HIGH | Update to latest master. Fixed in commit post-March 2026 |
| **nlohmann/json** | CVE-2024-38525 (uncaught exception on malformed unicode) | MEDIUM | Wrap all JSON parsing in try/catch. Enforce file size limits on untrusted JSON |
| **Recast Navigation** | Issue #798 (OOB write in sampleVelocityAdaptive) | MEDIUM | Pin version. Validate nav mesh params. Monitor for upstream fix |
| **GLM** | PR #1253 (SIMD normalize precision) | LOW | Test with ASan if using GLM_FORCE_INTRINSICS |

**Sources:**
- FreeType: https://nvd.nist.gov/vuln/detail/CVE-2025-27363
- stb_image: https://securitylab.github.com/advisories/GHSL-2023-145_GHSL-2023-151_stb_image_h/
- dr_flac: https://medium.com/@caplanmaor/integer-overflow-in-dr-flac-cve-2025-14369-2785de317496
- dr_wav: https://github.com/mackron/dr_libs/issues/296
- nlohmann/json: https://nvd.nist.gov/vuln/detail/CVE-2024-38525
- Recast: https://github.com/recastnavigation/recastnavigation/issues/798

---

## 6. Build Security

### Compiler Hardening Flags (OpenSSF Compliance)
Always compile with security-relevant warnings and protections. Based on the
[OpenSSF Compiler Options Hardening Guide](https://best.openssf.org/Compiler-Hardening-Guides/Compiler-Options-Hardening-Guide-for-C-and-C++.html).

```cmake
# Warnings
target_compile_options(vestige_engine PRIVATE
    -Wall -Wextra -Wpedantic
    -Wformat=2                    # Format string vulnerabilities
    -Wconversion -Wsign-conversion # Implicit type conversions
    -Wshadow                      # Variable shadowing
    -Wnull-dereference            # Null pointer dereference
    -Wdouble-promotion            # Float to double promotion
    -Wimplicit-fallthrough         # Missing break in switch
    -Werror=format-security        # Format string security errors
)

# Runtime hardening (all builds)
target_compile_options(vestige_engine PRIVATE
    -fstack-protector-strong      # Stack buffer overflow detection
    -fstack-clash-protection      # Stack clash attack protection
    -ftrivial-auto-var-init=zero  # Zero-init stack variables
    -fno-delete-null-pointer-checks # Preserve null checks in optimized code
    -fno-strict-overflow          # Treat signed overflow as defined
)

# Runtime definitions
target_compile_definitions(vestige_engine PRIVATE
    _FORTIFY_SOURCE=3             # Buffer overflow checks (GCC 12+, requires -O1+)
    _GLIBCXX_ASSERTIONS           # libstdc++ container precondition checks
)

# Linker hardening
target_link_options(vestige_engine PUBLIC
    -Wl,-z,relro -Wl,-z,now      # Full RELRO (GOT hardening)
    -Wl,-z,noexecstack            # Non-executable stack
    -Wl,--as-needed               # Eliminate unnecessary deps
)

# Debug builds — sanitizers
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(vestige_engine PRIVATE
        -fsanitize=address        # Memory error detector
        -fsanitize=undefined      # Undefined behavior detector
    )
endif()
```

**Source:** [CMake Implementation of OpenSSF Hardening](https://www.stevenengelhardt.com/2024/11/12/cmake-implementation-of-openssf-compiler-hardening-options/)

### Build Configurations
| Config | Purpose | Optimizations | Debug Info | Sanitizers |
|--------|---------|---------------|------------|------------|
| Debug | Development | None (`-O0`) | Full (`-g`) | ASan, UBSan |
| Release | Distribution | Full (`-O2`) | None | None |
| RelWithDebInfo | Profiling | Moderate (`-O1`) | Full (`-g`) | None |

---

## 7. Asset Protection (Commercial)

Since Vestige targets Steam distribution, assets (models, textures) have commercial value.

### Rules
- **Use Steam DRM** (Steamworks) as the first layer of protection (Phase 6)
- **Consider asset bundling** — pack assets into custom archive format rather than loose files
- **Do not store sensitive data** (API keys, encryption keys) in the binary or assets
- **Strip debug symbols** from release builds

### What NOT To Do
- Do not implement custom DRM — it frustrates legitimate users and gets cracked anyway
- Do not implement always-online requirements for a single-player experience
- Keep asset protection proportional — focus development time on making the product great

---

## 8. Logging Security

### Rules
- **Never log sensitive data** — no file system paths beyond the asset directory, no system usernames in release builds
- **Log levels must be appropriate** — Trace/Debug only in debug builds, Info/Warning/Error in release
- **Log files should not be world-readable** on Linux (set appropriate permissions)
- **Limit log file size** — rotate or cap to prevent disk exhaustion

---

## 9. Error Handling Strategy

### Rules
- **Never silently swallow errors** — always log, even if recovery is possible
- **Fail gracefully** — show a fallback (missing texture → pink checkerboard, missing model → wireframe cube) rather than crashing
- **Critical failures** (no GPU, no OpenGL context) should display a clear error message and exit cleanly
- **Never use `std::abort()` or `exit()` in library code** — return error codes or throw, let the application decide
- **Use `assert()` only for programmer errors** (invariants that should never be violated), not for runtime errors

```cpp
// Graceful degradation example
std::shared_ptr<Texture> ResourceManager::loadTexture(const std::string& path)
{
    auto texture = tryLoadFromFile(path);
    if (!texture)
    {
        Logger::warning("Failed to load texture: {} — using fallback", path);
        return m_fallbackTexture;
    }
    return texture;
}
```

---

## 10. Security Checklist for Code Review

Before merging any code, verify:

- [ ] No raw `new`/`delete` — smart pointers used
- [ ] No unbounded array access — `.at()` or bounds verified
- [ ] No C-style string functions — `std::string` used
- [ ] All file paths validated and confined to asset directory
- [ ] All loaded file data validated (sizes, ranges, formats)
- [ ] OpenGL calls checked for errors in debug builds
- [ ] Shader compilation/linking status checked
- [ ] No sensitive data in logs
- [ ] Variables initialized before use
- [ ] Compiler warnings resolved (no suppressions without justification)
- [ ] Audio files validated (sample rate, channel count, frame count within limits)
- [ ] Exception handlers use specific types (no `catch (...)` in new code)
- [ ] JSON parsing wrapped in try/catch with file size limits for untrusted input

---

## 11. Physics Input Validation

Physics subsystems accept configuration data and simulation parameters that must be validated to prevent crashes, hangs, or undefined behavior.

### ClothConfig Validation
- **Grid dimensions** must be >= 2 in both width and height -- a 1x1 grid cannot form any constraints
- **Particle mass** must be > 0 -- zero or negative mass causes division-by-zero in the XPBD solver
- **Substep count** must be clamped to the range [1, 64] -- zero substeps skip simulation entirely, and excessive substeps cause frame stalls

### Collider Parameter Validation
- **Plane colliders** must have non-zero normals -- a zero-length normal cannot define a half-space. Normals are auto-normalized, but zero-length input must be rejected (return false)
- **Sphere colliders** must have radius > 0 -- zero or negative radius produces degenerate collision geometry
- **Cylinder colliders** must have radius > 0 and height > 0 -- both parameters define the collision volume and must be positive

### Jolt Body Creation
- **Shape sizes** (half-extents for boxes, radius for spheres, radius/half-height for capsules) must all be positive -- Jolt will assert or produce invalid shapes otherwise
- **Body IDs** must be checked after creation -- `BodyInterface::CreateBody` can return `nullptr` if the body limit is exceeded or the shape is invalid. Always verify before adding to the physics world

### Floating-Point Safety
- **Division-by-zero guards** in the XPBD solver -- constraint projections divide by inverse mass sums; skip the projection when the denominator is zero or below epsilon
- **NaN propagation prevention** -- wind gust calculations and collision response must guard against NaN inputs (e.g., normalizing a zero-length vector). Use length-squared checks before normalization
- **Infinity clamping** -- clamp particle velocities and position deltas to sane maximums to prevent simulation explosion

### Resource Limits
- **Maximum grid size** -- enforce an upper bound on grid dimensions (e.g., 256x256 = 65,536 particles) to prevent allocation overflow and ensure the solver completes within frame budget
- **Substep cap** -- hard limit of 64 substeps per step prevents unbounded CPU time in the XPBD loop
- **Collider count** -- consider practical limits on the number of active colliders to keep per-particle collision checks bounded

---

## 12. Audio Input Validation

Audio decoders (dr_wav, dr_mp3, dr_flac, stb_vorbis) parse untrusted file metadata that can trigger integer overflows and excessive memory allocation (see CVE-2025-14369).

### Validation Limits (enforced in AudioClip::load*)
- **Maximum frames**: 86,400,000 (~30 minutes at 48kHz)
- **Maximum channels**: 8
- **Sample rate range**: 8,000 Hz to 192,000 Hz
- **Reject files exceeding these limits** with a logged error before any buffer allocation

### Integer Overflow Prevention
- Compute `totalFrames * channels` using `size_t` arithmetic to prevent 32-bit overflow
- Validate metadata before allocating decode buffers
- Always free decoder-allocated memory on validation failure
