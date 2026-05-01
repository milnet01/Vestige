# Vestige 3D Engine

C++17 / OpenGL 4.5 first-person exploration engine. Primary use: architectural walkthroughs (biblical structures first — Tabernacle, Solomon's Temple). Future: Vulkan, ray tracing, Steam, broader games.

## Stack
C++17 · OpenGL 4.5 · GLFW · GLM · CMake · Google Test. Linux + Windows. Keyboard/mouse + Xbox/PS controllers via GLFW.

## Dev hardware
Ryzen 5 5600 · RX 6600 (RDNA2, GL 4.6, Vulkan 1.3) · 32 GB · Linux.

## Performance
**60 FPS minimum — hard requirement.** Profile before optimizing. Prefer GPU-efficient paths (batching, instancing, culling).

## Global rules apply
All rules in `~/.claude/CLAUDE.md` are in force here and are not repeated below — no workarounds without a root-cause fix, shortest correct implementation, reuse before rewriting, six-month test, latest library versions with current idioms, push-cadence rules, surface ambiguity instead of guessing, push back when a simpler path exists, reproduce-before-fix for bugs (use `/feature-test` to scaffold the failing test first), stay in your lane on edits, and state a verify-step plan for multi-step work. The project-specific rules below specialise — they don't replace.

## Project-specific rules
1. **Research → design → review → code.** New features/phases: web research, then `docs/phases/phase_NN_design.md` (architecture, API, steps, performance, accessibility, CPU/GPU placement) with cited sources, then user review (blocking, not a formality), then implementation. This is the project's verify-step format.
2. **Explain clearly.** User is learning — no assumed graphics/C++ knowledge.
3. **Modular and minimal.** Subsystems independent and extensible. Start simple.
4. **Mandatory post-phase audit.** After every phase, run the AUDIT_STANDARDS.md 5-tier process and get a fix plan approved before the next phase. Research experimental features alongside.
5. **Log workarounds in commit + CHANGELOG.** Project extension to the global no-workarounds rule: a workaround that genuinely ships also gets named in the commit message and `CHANGELOG.md` so it stays discoverable later. Patterns to flag (not dress up as fixes): iteration caps, disabled features, hidden clamps.
6. **Formula Workbench (`tools/formula_workbench/`) for numerical design.** Author/fit/validate/export formulas and coefficients there instead of hand-coding magic constants. Legitimate optimization path too (Workbench-fit approximations can replace heavier runtime math). Hand-code only when no reference data exists; leave a `TODO: revisit via Formula Workbench` comment.
7. **CPU vs GPU at design time.** Every design doc needs a "CPU / GPU placement" section with choice + reason. Default heuristic: per-pixel / per-vertex / per-particle / per-froxel → GPU; branching / sparse / decision / I/O → CPU. Dual impls allowed (CPU spec + GPU runtime) — pin them with a parity test. Don't defer to "CPU for now, move later"; that becomes a rewrite.
8. **Older library pins need a written reason.** Project extension to the latest-versions rule: when a `FetchContent_Declare` lags upstream, the reason goes in `THIRD_PARTY_NOTICES.md` or beside the declare (compiler-compat regression, ABI lockstep, security advisory). Every dependency upgrade gets a cold-eyes review (fresh subagent, no authoring context).
9. **Documentation reviews are cold-eyes.** Spec, design-doc, ROADMAP, and architecture-doc reviews dispatch fresh subagents with no authoring context — never in-line review by the author. Iterate review → fix → review until convergence. Same principle as `/indie-review` for code, extended to documentation.

## Coding standards (summary)
- Files `snake_case.{cpp,h}` · Classes `PascalCase` · Functions `camelCase` · Members `m_camelCase` · Constants `UPPER_SNAKE_CASE`
- Allman braces · 4-space indent · one class per file · `#pragma once`

## See also
ARCHITECTURE.md (Subsystem + Event Bus) · CODING_STANDARDS.md · SECURITY.md · AUDIT_STANDARDS.md.
