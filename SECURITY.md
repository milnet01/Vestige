# Vestige Security

Two parts: **vulnerability disclosure** (for users/contributors) and **internal security standards** (for anyone modifying the engine).

> **Short version:** email [aant.schemel@gmail.com](mailto:aant.schemel@gmail.com) with subject `[vestige-security]`. Don't open a public issue or PR for an unpatched vulnerability.

---

## Vulnerability Disclosure

**Scope.** Anything in the [`milnet01/Vestige`](https://github.com/milnet01/Vestige) repo: C++ source, shaders, audit / Formula Workbench tools, bundled assets, CI config.

**Out of scope:** upstream dependencies (GLFW, GLM, Jolt, ImGui, FreeType, OpenAL Soft, etc. — report those upstream; CVE list in §5); assets in the separate `milnet01/VestigeAssets` repo; downstream projects built with Vestige.

**How to report.** Email with `[vestige-security]` subject prefix. Include:
1. Clear description of the vulnerability.
2. Reproduction steps (commit SHA, build config, required inputs).
3. Impact assessment (severity, attack surface).
4. Proposed mitigation, if any.
5. Preferred attribution (name/handle/anonymous).

PGP available on request.

**Timeline (solo-maintained, best-effort):** acknowledgement ~7d, triage ~14d, fix+disclosure coordinated (typically ≤90d from triage). Credit in changelog unless anonymous requested.

Please do not disclose publicly until a fix ships. Active exploitation → mention in first email.

**No paid bounty.** Good-faith reports are acknowledged in release notes.

**Safe-harbour.** Good-faith research under these guidelines is not treated as a ToS violation: reverse-engineering, fuzzing/static-analysis on your own build, private reporting, coordinated disclosure. Does **not** cover: attacking project infrastructure, exfiltrating data, DoS on shared services, pre-fix public disclosure.

---

# Security Standards (internal)

Engine targets Steam distribution — security matters at every stage.

---

## 1. Memory Safety

C++ has no automatic memory safety. Mitigations:

- **No raw `new`/`delete`** — use `std::unique_ptr` / `std::shared_ptr`.
- **No raw arrays** — use `std::vector`, `std::array`, `std::span`.
- **No C string funcs** (`strcpy`, `strcat`, `sprintf`) — use `std::string`, `std::format`/`std::snprintf`.
- **Always initialize** — uninitialized variables are UB.
- **Validate before dereference**, or use references.
- **RAII for all resources** — GPU buffers, FDs, GL objects released in destructors.
- **Bounds checking:** use `.at()` for general access, `[]` only in performance-critical hot loops where the index is provably valid.

---

## 2. Input Validation

All external data must be validated.

**File loading (models, textures, shaders, scenes):**
- Validate magic bytes / headers before parsing.
- Reject files over reasonable size limits.
- Validate all numeric ranges (vertex counts, texture dims, indices).
- Log + fall back on malformed data; never crash.
- Treat all files as potentially corrupted.

**Path traversal prevention:**
- Reject `..`, absolute paths when relative is expected.
- Confine asset loading to the assets directory (`std::filesystem::canonical` + `starts_with` base).
- Validate file extensions.

**User input:** clamp to valid ranges; rate-limit; verify gamepad axes in [-1, 1].

---

## 3. Shader Security

Malformed shaders can crash drivers or hang the GPU.

- **Check `GL_COMPILE_STATUS` and `GL_LINK_STATUS`** — log full info log on failure, handle gracefully.
- **Validate uniform locations** before setting.
- **No unbounded shader loops.**

---

## 4. OpenGL State Safety

- `glGetError()` loop in debug builds after significant operations.
- Always unbind resources after use.
- Check `glCheckFramebufferStatus` before rendering to an FBO.
- Respect `GL_MAX_*` hardware limits.

---

## 5. Dependency Security

- **Pin versions** in CMake — no "latest".
- Only well-maintained, reputable, permissively-licensed libs.
- Review changelogs on update.
- Fetch via CMake `FetchContent` with specific commits/tags.
- No build-time binary downloads.

### Approved Dependencies

| Library | Version strategy | Source |
|---------|------------------|--------|
| GLFW, GLM, Google Test, Jolt, Recast, FreeType, nlohmann/json | Tagged release | GitHub |
| glad | Generated, committed | glad generator |
| stb_image, dr_libs | Specific commit | GitHub |
| OpenAL-Soft | System package | kcat/openal-soft |
| imgui | Embedded in `external/` | GitHub |

### Known CVEs and Mitigations (Reviewed 2026-04-11)

| Library | CVE / Issue | Severity | Mitigation |
|---------|-------------|----------|------------|
| **FreeType** | CVE-2025-27363 (OOB write, actively exploited) | HIGH | Update to ≥ 2.13.1. Monitor for CVE-2026-23865 (fixed in 2.14.2) |
| **stb_image** | GHSL-2023-145..151 (7 vulns incl. double-free) | HIGH | Validate dimensions after load (done: 16384×16384 max). Don't load untrusted images on background threads without mutex. Monitor upstream |
| **dr_libs** | CVE-2025-14369 (integer overflow in dr_flac) | HIGH | Validate audio metadata before decoding (done: sample rate, channels, frames). Update to master post-March 2026 |
| **dr_libs** | Issue #296 (heap overflow in drwav smpl chunk) | HIGH | Update to master. Fixed post-March 2026 |
| **nlohmann/json** | CVE-2024-38525 (uncaught exception on malformed unicode) | MEDIUM | Wrap parsing in try/catch. Enforce file-size limits on untrusted JSON |
| **Recast** | Issue #798 (OOB write in sampleVelocityAdaptive) | MEDIUM | Pin version. Validate nav mesh params. Monitor for upstream fix |
| **GLM** | PR #1253 (SIMD normalize precision) | LOW | Test with ASan if using `GLM_FORCE_INTRINSICS` |
| **AMD amdgpu kernel driver** | CVE-2026-23213 (SMU reset MMIO flaw — RDNA2/RDNA3 incl. RX 6000) | MEDIUM | Document recommended kernel. Fix backported to 6.9+. Affects users, not engine; surfaces as spurious GPU hangs |
| **Mesa RadeonSI** | 25.3/26.0 uniform VRAM leak; 26.0.2 GL_FEEDBACK X-coord regression (legacy GL only) | LOW | Recommend ≥ Mesa 26.0.4 (2026-04-01) or 26.0.5 (2026-04-15). Vestige uses core 4.5, not affected by GL_FEEDBACK |

**Sources:** FreeType NVD; [GHSL stb_image](https://securitylab.github.com/advisories/GHSL-2023-145_GHSL-2023-151_stb_image_h/); dr_flac Medium writeup (CVE-2025-14369); dr_wav issue 296; NVD for CVE-2024-38525; Recast issue 798; AMD bulletin amd-sb-6024; Mesa 26.0.2 release notes.

### Linux Support Matrix
- Minimum kernel: **6.9+** (amdgpu SMU-reset CVE-2026-23213 fix).
- Minimum Mesa: **26.0.4** (VRAM leak + GL_FEEDBACK fixes).
- Tested on: openSUSE Tumbleweed (dev), Ubuntu 24.04 LTS (target).

---

## 6. Build Security

Hardening flags per [OpenSSF Compiler Options Hardening Guide](https://best.openssf.org/Compiler-Hardening-Guides/Compiler-Options-Hardening-Guide-for-C-and-C++.html):

```cmake
target_compile_options(vestige_engine PRIVATE
    -Wall -Wextra -Wpedantic
    -Wformat=2 -Wconversion -Wsign-conversion
    -Wshadow -Wnull-dereference -Wdouble-promotion
    -Wimplicit-fallthrough -Werror=format-security
    -fstack-protector-strong -fstack-clash-protection
    -ftrivial-auto-var-init=zero
    -fno-delete-null-pointer-checks -fno-strict-overflow
)
target_compile_definitions(vestige_engine PRIVATE
    _FORTIFY_SOURCE=3 _GLIBCXX_ASSERTIONS
)
target_link_options(vestige_engine PUBLIC
    -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -Wl,--as-needed
)
# Debug builds:
#   -fsanitize=address -fsanitize=undefined
```

**Reference:** [CMake Implementation of OpenSSF Hardening](https://www.stevenengelhardt.com/2024/11/12/cmake-implementation-of-openssf-compiler-hardening-options/).

### Build Configurations

| Config | Purpose | Opt | Debug info | Sanitizers |
|--------|---------|-----|------------|------------|
| Debug | Development | `-O0` | `-g` | ASan, UBSan |
| Release | Distribution | `-O2` | none | none |
| RelWithDebInfo | Profiling | `-O1` | `-g` | none |

---

## 7. Asset Protection (commercial)

- Steam DRM (Steamworks) as first layer — Phase 6.
- Pack assets into a custom archive format rather than loose files.
- No secrets (API keys, encryption keys) in binary or assets.
- Strip debug symbols from release builds.
- **Don't** implement custom DRM (user-hostile, cracked anyway). No always-online for single-player.

---

## 8. Logging Security

- Never log sensitive data — no filesystem paths outside assets, no system usernames in release.
- Trace/Debug only in debug builds; Info/Warning/Error in release.
- Log files not world-readable on Linux.
- Cap size / rotate to prevent disk exhaustion.

---

## 9. Error Handling

- Never silently swallow errors — always log.
- Fail gracefully: missing texture → pink checkerboard, missing model → wireframe cube.
- Critical failures (no GPU, no GL context) → clear message, clean exit.
- Never `std::abort()`/`exit()` in library code — return error or throw.
- `assert()` for programmer invariants only, not runtime conditions.

---

## 10. Review Checklist

Before merge, verify:

- [ ] No raw `new`/`delete`
- [ ] No unbounded array access
- [ ] No C-style string funcs
- [ ] File paths validated + confined to assets dir
- [ ] Loaded file data validated (sizes, ranges, formats)
- [ ] GL calls error-checked in debug builds
- [ ] Shader compile/link checked
- [ ] No sensitive data in logs
- [ ] Variables initialized
- [ ] Warnings resolved (no unjustified suppressions)
- [ ] Audio: sample rate/channels/frames within limits
- [ ] Exception handlers use specific types (no `catch (...)` in new code)
- [ ] JSON parsing wrapped in try/catch with size limits for untrusted input

---

## 11. Physics Input Validation

**ClothConfig:** grid dims ≥ 2 on both axes; particle mass > 0; substeps clamped to [1, 64].

**Colliders:** plane normals non-zero (auto-normalized but zero-length rejected); sphere radius > 0; cylinder radius > 0 and height > 0.

**Jolt body creation:** shape sizes all positive; check body ID after creation (`CreateBody` can return `nullptr` if body limit exceeded or shape invalid).

**Floating-point safety:** guard div-by-zero in XPBD constraint projections (skip when denom ≤ epsilon); NaN prevention (length-squared check before normalization); infinity clamp on velocities and position deltas.

**Resource limits:** max grid 256×256 = 65 536 particles; hard substep cap of 64; practical bounds on active collider count.

---

## 12. Audio Input Validation

Decoders (dr_wav, dr_mp3, dr_flac, stb_vorbis) parse untrusted metadata — integer overflow / excessive-alloc risk (CVE-2025-14369).

**Limits (enforced in `AudioClip::load*`):** max frames 86 400 000 (~30 min @ 48 kHz), max channels 8, sample rate [8 000, 192 000] Hz. Reject + log before buffer alloc.

**Overflow prevention:** compute `totalFrames * channels` with `size_t`; validate before buffer allocation; free decoder memory on validation failure.
