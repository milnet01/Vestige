# Vestige 3D Engine

## Project Overview
Vestige is a 3D exploration engine built in C++17 with OpenGL 4.5. Its primary purpose is creating first-person architectural walkthroughs, starting with biblical structures (Tabernacle/Tent of Meeting, Solomon's Temple).

## Technology Stack
- **Language:** C++17
- **Graphics API:** OpenGL 4.5
- **Windowing:** GLFW
- **Math:** GLM
- **Build System:** CMake
- **Platforms:** Linux, Windows
- **Input:** Keyboard, mouse, Xbox/PlayStation controllers (via GLFW)
- **Testing:** Google Test (from the start)

## Development Hardware
- CPU: AMD Ryzen 5 5600 (6-core/12-thread)
- GPU: AMD Radeon RX 6600 (RDNA2, OpenGL 4.6, Vulkan 1.3, basic HW ray tracing)
- RAM: 32GB
- OS: Linux (Ubuntu-based)

## Performance Target
- **Minimum 60 FPS** at all times — this is a hard requirement
- Profile before optimizing — measure, don't guess
- Prefer GPU-efficient approaches (batching, instancing, frustum culling)

## Future Goals
- Vulkan rendering backend
- Ray tracing (rudimentary, then hardware-accelerated)
- Steam distribution
- Game development (beyond exploration experiences)

## Key Rules
1. **Research before implementation.** Every new feature or phase must begin with thorough online research (web searches across game engines, graphics programming resources, open-source projects) to inform design decisions. Research findings go into a design document (e.g., `docs/PHASE5C_DESIGN.md`) with cited sources before any code is written.
2. **Plan before coding.** The user wants thorough documentation and discussion before implementation. Design docs must cover architecture, API design, implementation steps, performance considerations, and accessibility.
3. **Follow coding standards strictly.** See CODING_STANDARDS.md — all naming conventions, formatting, and structure rules must be followed exactly.
4. **Explain concepts clearly.** The user is learning — no assumed knowledge of graphics programming or C++.
5. **Keep it modular.** Every subsystem should be independent and extensible. New features slot in without breaking existing code.
6. **No over-engineering.** Start simple, add complexity only when needed.
7. **Security first.** See SECURITY.md — memory safety, input validation, and secure coding practices are mandatory.
8. **60 FPS minimum.** All rendering and logic must sustain at least 60 frames per second.
9. **Mandatory post-phase audit.** After every phase completion, run the full audit process defined in AUDIT_STANDARDS.md before starting the next phase. The audit uses a 5-tier approach (automated tools → grep scans → changed-file review → full-codebase categorical sweep → online research) to cover the entire codebase while minimizing token usage. All findings must be compiled into a report, verified, and a fix plan approved by the user before implementation begins. Also research experimental features that could benefit the engine. See AUDIT_STANDARDS.md for the complete process, checklist, and scaling strategy.
10. **No workarounds without exhausting root-cause investigation.** When a bug is observed, first investigate and fix the underlying cause. Do not reach for symptom-suppressing tweaks (capping iteration counts, disabling features, dialling down intensities, adding bounded clamps that hide the issue) until you have a clear understanding of *why* the bug occurs and have ruled out a clean fix. Methodical bisection (e.g. CLI feature toggles like `--isolate-feature=NAME`) is preferred to "guess and tweak". If a workaround is genuinely the only viable option (third-party bug, hardware limitation, time-bounded mitigation), say so explicitly in code comments, commit messages, and CHANGELOG: *"this is a workaround for X; the proper fix is Y but requires Z."* Never dress up a workaround as a fix.
11. **Use the Formula Workbench for numerical design.** When implementing or optimizing a feature that involves a numerical formula or coefficient-driven calculation — physics response, rendering math, terrain generation, camera easing, post-processing effects, lighting attenuation, animation curves, etc. — use the Formula Workbench (`tools/formula_workbench/`) to author, fit, validate, and export the formula rather than hand-coding magic constants or ad-hoc curve approximations. The workbench provides a physics formula library, Levenberg-Marquardt coefficient fitting with bounds, convergence / residual / R² / RMSE / AIC/BIC inspection, and export to `FormulaLibrary` JSON or generated C++/GLSL that the engine consumes at asset-load time. Using it keeps numerical tuning centralized, reproducible, reviewable, and performance-tunable; hand-coded constants drift and resist review. Also treat the workbench as a performance tool — replacing a heavier runtime computation with a Workbench-fit approximation is a legitimate optimization path, consistent with the 60 FPS requirement. Only hand-code constants when no reference data exists to fit against (placeholder / early prototype); when you do, leave a `TODO: revisit via Formula Workbench once reference data is available` comment so it's not forgotten.
12. **Decide CPU vs GPU placement at design time, not at optimisation time.** Every new subsystem's design doc must include a one-section **"CPU / GPU placement"** entry that states where the work runs and *why*. The decision is part of the architecture, not something to defer — retrofitting a CPU subsystem onto the GPU later is a rewrite, not a tweak.

    **Default heuristic.**
    - **Per-pixel / per-vertex / per-particle / per-froxel work → GPU.** The cost scales with screen resolution or instance count, the work is embarrassingly parallel, and branching is minimal. Examples already on GPU in Vestige: particles (compute), morph targets (SSBO), cloth (compute), SH probe bake, frustum culling for foliage, all post-processing (bloom, SSAO, tonemap, fog composite, TAA, SMAA).
    - **Branching / sparse / decision-making / tree-traversal / I/O work → CPU.** Scene-graph updates, asset loading, audio mixing decisions (OpenAL handles per-sample DSP on its own thread), AI behaviour trees, script interpretation, physics broad-phase at engine-typical scales, editor UI logic.
    - **Dual CPU + GPU implementations are allowed when testability demands it.** Example: fog primitives live in `engine/renderer/fog.{h,cpp}` *and* in `screen_quad.frag.glsl`. The CPU version acts as a unit-testable spec; the GPU version is what runs per-pixel. When you do this, write a parity test that pins the two to each other, so silent drift between the CPU spec and the GPU reality is impossible.

    **Signals that the default is wrong — investigate before following it:**
    - Per-item GPU work that requires a large per-item state machine or mutable shared structure → CPU may be simpler and faster than contention on the GPU.
    - Per-frame CPU loop that touches > ~10k items of homogeneous data (e.g. culling AABBs, updating particle states, evaluating heightfield blend weights) → GPU compute is likely the right answer even before profiling says so.
    - "I'll put it on the CPU for now and move it later" → this almost never happens, and when it does it's a rewrite. Budget the GPU path in the original design doc.

    **Auditor's lens.** When reviewing an existing subsystem for performance, ask: (a) does the per-frame cost scale with a count that hits the thousands or millions? (b) is the math pure-arithmetic with little branching? (c) is the data layout already packed or packable into a contiguous buffer? If all three are yes and the work is on the CPU, flag the migration as a candidate. Don't flag on instinct — GPU offload carries real costs (PCIe transfer, driver overhead, debug friction, Mesa sampler-binding quirks) and must earn its place.

    **Document the decision.** Each design doc gets a short "CPU / GPU placement" section stating the choice and the reason, so reviewers (and future Claude sessions) can challenge or confirm it without reconstructing the thinking from scratch.

## Architecture
Subsystem + Event Bus pattern. See ARCHITECTURE.md for full details.

## Coding Standards Summary
- Files: `snake_case.cpp` / `snake_case.h`
- Classes: `PascalCase`
- Functions: `camelCase`
- Members: `m_camelCase`
- Constants: `UPPER_SNAKE_CASE`
- Braces: Allman style (opening brace on new line)
- Indentation: 4 spaces
- One class per file
- `#pragma once` for include guards
- See CODING_STANDARDS.md for complete standards
