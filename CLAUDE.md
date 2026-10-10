# Vestige 3D Engine

C++17 / OpenGL 4.5 first-person exploration engine. Primary use: architectural walkthroughs (biblical structures first — Tabernacle, Solomon's Temple). Future: Vulkan, ray tracing, Steam, broader games.

## Stack
C++17 · OpenGL 4.5 · GLFW · GLM · CMake · Google Test. Jolt Physics · OpenAL Soft · Dear ImGui · enkiTS · Recast/Detour. Full list with licences: `THIRD_PARTY_NOTICES.md`. Linux + Windows. Keyboard/mouse + Xbox/PS controllers via GLFW.

## Dev hardware
Ryzen 5 5600 · RX 6600 (RDNA2, GL 4.6, Vulkan 1.3) · 32 GB · Linux.

## Performance
**60 FPS minimum — hard requirement.** Measure it on the dev hardware with a Release build: `vestige --demo-flythrough --no-vsync --profile-log=run.csv` at the default quality and window size, then `tools/fps_floor.py run.csv`. Every one-second sample after two seconds of warm-up must read at least 60 FPS. An average hides hitches, and a Debug or ASan build is not a reading. `tools/perf_gate.py` is a different check: it compares against a committed baseline. Profile before optimizing. Prefer GPU-efficient paths (batching, instancing, culling).

## Global rules apply
Everything in `~/.claude/CLAUDE.md` and the standards it points at is in force here. Read them there; they are not restated below, because a rule stated twice is two rules that will disagree.

The project rules below specialise those rules. They do not replace them.

## Project-specific rules
1. **Most work needs no spec, and `~/.claude/standards/spec-format.md` §1 is what decides.** Apply its headline test, not a list of examples: where it says no, build it, and the roadmap item is the contract. Where §1 says yes, `write-spec` writes it — the contract at `docs/specs/<ID>-<topic>.md`, the build order at `docs/plans/<ID>-<topic>.md`, with this project's deltas in `docs/standards/spec-format-overrides.md`. Research the shape first where it is genuinely unknown, and cite the sources in the spec, or in the roadmap item where there is none. The gate before anyone builds is global rule 14's cold read (`~/.claude/CLAUDE.md`), run as rule 9 describes. `docs/phases/` holds this project's earlier design documents. It takes no new ones, including phases already drafted but not yet started: new design work goes to `docs/specs/`.
2. **Explain clearly.** User is learning — no assumed graphics/C++ knowledge.
3. **Modular and minimal.** Subsystems independent and extensible. Start simple.
4. **Mandatory audit before each minor or major release.** Read `.claude/rules/release-audit.md` before cutting any `x.y.0` release.
5. **Log workarounds in commit + CHANGELOG.** Project extension to the global no-workarounds rule: a workaround that genuinely ships also gets named in the commit message and `CHANGELOG.md`, marked so a search lists every one: in the commit message, an unindented line starting `Workaround:` with no bullet, found by `git log --grep='^Workaround:'`; in `CHANGELOG.md`, a top-level `- ` bullet (not nested) whose text starts `Workaround:`, bold or not, found by `grep -nE '^- (\*\*)?Workaround(\*\*)?:' CHANGELOG.md`. Patterns to flag (not dress up as fixes): iteration caps, disabled features, hidden clamps.
6. **Formula Workbench (`tools/formula_workbench/`) for numerical design.** Author/fit/validate/export formulas and coefficients there instead of hand-coding magic constants. Legitimate optimization path too (Workbench-fit approximations can replace heavier runtime math). Hand-code only when no reference data exists; leave a `TODO: revisit via Formula Workbench` comment.
7. **CPU vs GPU at design time.** Work that touches the GPU, or matches a GPU row of `CODING_STANDARDS.md` §17's table, records its placement with choice + reason: in the spec's "CPU / GPU placement" section, or in the roadmap item where §1 needs no spec. A spec for other work (a save schema, localisation) omits the section. §17 owns the heuristic table. Dual impls allowed (CPU spec + GPU runtime) — pin them with a parity test. Don't defer to "CPU for now, move later"; that becomes a rewrite.
8. **A non-latest pin carries a written reason at the pin site.** Read `.claude/rules/dependency-pins.md` before adding, upgrading or pinning a dependency.
9. **Documentation reviews are independent cold reads — `review-contract`.** Read `.claude/rules/doc-reviews.md` before reviewing a spec, plan, design or architecture document.

## Coding standards (summary)
- Files `snake_case.{cpp,h}` · Classes `PascalCase` · Functions `camelCase` · Members `m_camelCase` · Constants `UPPER_SNAKE_CASE` or `kCamelCase`
- Allman braces · 4-space indent · one class per file · `#pragma once`
- **There is no GLSL `#include`.** Read `.claude/rules/shaders.md` before sharing a function between two shaders.

## See also
ARCHITECTURE.md (Subsystem + Event Bus) · CODING_STANDARDS.md · SECURITY.md · AUDIT_STANDARDS.md · DEPENDENCY_STANDARDS.md · TESTING.md · CONTRIBUTING.md · ROADMAP.md · CHANGELOG.md.

Why these rules read as they do: `docs/history/claude-md.md`. Nothing there is in force.
