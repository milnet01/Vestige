# Vestige 3D Engine

C++17 / OpenGL 4.5 first-person exploration engine. Primary use: architectural walkthroughs (biblical structures first — Tabernacle, Solomon's Temple). Future: Vulkan, ray tracing, Steam, broader games.

## Stack
C++17 · OpenGL 4.5 · GLFW · GLM · CMake · Google Test. Linux + Windows. Keyboard/mouse + Xbox/PS controllers via GLFW.

## Dev hardware
Ryzen 5 5600 · RX 6600 (RDNA2, GL 4.6, Vulkan 1.3) · 32 GB · Linux.

## Performance
**60 FPS minimum — hard requirement.** Profile before optimizing. Prefer GPU-efficient paths (batching, instancing, culling).

## Key Rules
1. **Research → design → review → code.** New features/phases start with web research, then a `docs/PHASE*_DESIGN.md` (architecture, API, steps, performance, accessibility) with cited sources, then user review (blocking, not a formality), then implementation.
2. **Explain clearly.** User is learning — no assumed graphics/C++ knowledge.
3. **Modular and minimal.** Subsystems independent and extensible. Start simple; don't over-engineer for hypothetical futures.
4. **Mandatory post-phase audit.** After every phase, run the AUDIT_STANDARDS.md 5-tier process and get a fix plan approved before the next phase. Also research experimental features.
5. **No workarounds without root-cause investigation.** Fix the underlying cause; don't silence symptoms (iteration caps, disabled features, hidden clamps). If a workaround is genuinely the only option, document it explicitly in code, commit, and CHANGELOG — never dress it up as a fix.
6. **Use the Formula Workbench (`tools/formula_workbench/`) for numerical design.** Author/fit/validate/export formulas and coefficients there instead of hand-coding magic constants. Legitimate optimization path too (Workbench-fit approximations can replace heavier runtime math). Only hand-code when no reference data exists — leave a `TODO: revisit via Formula Workbench` comment.
7. **Decide CPU vs GPU at design time.** Every design doc needs a "CPU / GPU placement" section with choice + reason. Default heuristic: per-pixel / per-vertex / per-particle / per-froxel → GPU; branching / sparse / decision / I/O → CPU. Dual impls allowed for testability (CPU spec + GPU runtime) — pin them with a parity test. Don't defer to "put on CPU for now, move later"; that becomes a rewrite.

## Coding standards (summary)
- Files `snake_case.{cpp,h}` · Classes `PascalCase` · Functions `camelCase` · Members `m_camelCase` · Constants `UPPER_SNAKE_CASE`
- Allman braces · 4-space indent · one class per file · `#pragma once`

## See also
ARCHITECTURE.md (Subsystem + Event Bus) · CODING_STANDARDS.md · SECURITY.md · AUDIT_STANDARDS.md.
