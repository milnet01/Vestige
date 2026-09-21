# Vestige 3D Engine

C++17 / OpenGL 4.5 first-person exploration engine. Primary use: architectural walkthroughs (biblical structures first — Tabernacle, Solomon's Temple). Future: Vulkan, ray tracing, Steam, broader games.

## Stack
C++17 · OpenGL 4.5 · GLFW · GLM · CMake · Google Test. Jolt Physics · OpenAL Soft · Dear ImGui · enkiTS · Recast/Detour. Full list with licences: `THIRD_PARTY_NOTICES.md`. Linux + Windows. Keyboard/mouse + Xbox/PS controllers via GLFW.

## Dev hardware
Ryzen 5 5600 · RX 6600 (RDNA2, GL 4.6, Vulkan 1.3) · 32 GB · Linux.

## Performance
**60 FPS minimum — hard requirement.** Profile before optimizing. Prefer GPU-efficient paths (batching, instancing, culling).

## Global rules apply
Everything in `~/.claude/CLAUDE.md` and the standards it points at is in force here. Read them there; they are not restated below, because a rule stated twice is two rules that will disagree.

The project rules below specialise those rules. They do not replace them.

## Project-specific rules
1. **Most work needs no spec, and `~/.claude/standards/spec-format.md` §1 is what decides.** Its usual answer is no: a bug fix, a new toggle, a new flag, another case in an existing switch — build it, and the roadmap item is the contract. Where §1 says yes, `write-spec` writes it — the contract at `docs/specs/<ID>-<topic>.md`, the build order at `docs/plans/<ID>-<topic>.md`. Research the shape first where it is genuinely unknown, and cite the sources in the spec. The gate before anyone builds is rule 9's cold read. `docs/phases/` holds this project's earlier design documents and is not where new ones go.
2. **Explain clearly.** User is learning — no assumed graphics/C++ knowledge.
3. **Modular and minimal.** Subsystems independent and extensible. Start simple.
4. **Mandatory post-phase audit.** After every phase, run the AUDIT_STANDARDS.md tier process — every tier it defines, not a subset; CI covers the per-push tiers and the rest run at phase close — and get a fix plan approved before the next phase. Research experimental features alongside.
5. **Log workarounds in commit + CHANGELOG.** Project extension to the global no-workarounds rule: a workaround that genuinely ships also gets named in the commit message and `CHANGELOG.md` so it stays discoverable later. Patterns to flag (not dress up as fixes): iteration caps, disabled features, hidden clamps.
6. **Formula Workbench (`tools/formula_workbench/`) for numerical design.** Author/fit/validate/export formulas and coefficients there instead of hand-coding magic constants. Legitimate optimization path too (Workbench-fit approximations can replace heavier runtime math). Hand-code only when no reference data exists; leave a `TODO: revisit via Formula Workbench` comment.
7. **CPU vs GPU at design time.** Every spec needs a "CPU / GPU placement" section with choice + reason. Default heuristic: per-pixel / per-vertex / per-particle / per-froxel → GPU; branching / sparse / decision / I/O → CPU. Dual impls allowed (CPU spec + GPU runtime) — pin them with a parity test. Don't defer to "CPU for now, move later"; that becomes a rewrite.
8. **A non-latest pin carries a written reason at the pin site.** `~/.claude/standards/dependencies.md` owns the latest-stable rule and what counts as a dependency. What this project adds is the recording: a non-latest pin is only allowed in the two cases `DEPENDENCY_STANDARDS.md` defines, each carrying a reason at the pin site: **(A)** a newer *release* provably breaks — record a row in the Breaking-Version Registry with a re-test trigger (the exact breaking version, so a newer release is re-tested and the pin lifted once fixed); or **(B)** upstream has no suitable release yet, so an exact branch commit is pinned — record it in `THIRD_PARTY_NOTICES.md`'s Branch-commit-pins section with a re-evaluate trigger. "We haven't tested it yet" is not a valid reason. Every dependency upgrade gets an independent review by a fresh subagent with no authoring context. Full process + routing: `DEPENDENCY_STANDARDS.md`.
9. **Documentation reviews are independent cold reads — `review-contract`.** Spec, design-doc and architecture-doc reviews dispatch fresh subagents with no authoring context. ROADMAP and CHANGELOG are not subjects — `review-contract` refuses them, and they get `check-doc-facts` instead. Re-reading it yourself is not a review. Iterate review → fix → review until it converges or the skill's loop cap is reached; both are clean exits. This is the same principle `review-code` applies to code, extended to documentation.

## Coding standards (summary)
- Files `snake_case.{cpp,h}` · Classes `PascalCase` · Functions `camelCase` · Members `m_camelCase` · Constants `UPPER_SNAKE_CASE`
- Allman braces · 4-space indent · one class per file · `#pragma once`
- **There is no GLSL `#include`** — the shader loader does no preprocessing, so two shaders cannot share source the obvious way. Two options, in this order: (a) **link the same stage file into both programs** where the stages genuinely agree — the grass shadow caster pairs the unmodified `grass.vert.glsl` with `grass_shadow.frag.glsl`, so there is one blade generator and drift is impossible (3D_E-0042); (b) **copy the function and pin it with a text-parity test** where the stages must differ — `tests/test_grass_shadow_parity.cpp` is the pattern to copy. Never copy without the parity test. The `tree_mesh` / `tree_shadow` dither copy is the known **unpinned** case (3D_E-0685); it is not a precedent to follow. Drift fails at load time, never at build time: the loader logs an error, and a shadow-caster program logs a warning and carries on, so the feature just silently stops working.

## See also
ARCHITECTURE.md (Subsystem + Event Bus) · CODING_STANDARDS.md · SECURITY.md · AUDIT_STANDARDS.md · DEPENDENCY_STANDARDS.md · TESTING.md · CONTRIBUTING.md · ROADMAP.md · CHANGELOG.md.

Why these rules read as they do: `docs/history/claude-md.md`. Nothing there is in force.
