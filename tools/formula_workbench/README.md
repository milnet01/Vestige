# Formula Workbench

Interactive tool for formula discovery, coefficient fitting, and validation. Lives alongside the Vestige engine but ships as a separate binary.

## What it is

A desktop GUI (Dear ImGui + ImPlot) that lets you:
- Browse the built-in physics formula library (templates across physics, rendering, terrain, camera, post-processing)
- Import data via CSV or hand-entry
- Fit coefficients with Levenberg-Marquardt + bounds
- Inspect convergence, residuals, R², RMSE, AIC/BIC
- Export fitted formulas to `FormulaLibrary` JSON or as C++/GLSL snippets

See `CHANGELOG.md` for the full release history.

## How it relates to the engine

The workbench **links against `vestige_engine`** — it depends on `engine/formula/*` (the formula library, curve fitter, codegen, and presets). This coupling is intentional: the engine is the canonical consumer of fitted formulas, and keeping the workbench in-tree means changes to the formula library surface immediately in the tool.

It does **not** run as part of the engine at runtime. It's a separate binary (`build/bin/formula_workbench`) that writes its output to disk; the engine reads those outputs at asset-load time.

## Independent versioning

The workbench tracks its own SEMVER independently of the engine. Current version: see `WORKBENCH_VERSION` in `workbench.h` (authoritative source) and `CHANGELOG.md` (human-readable history).

| Concern | Engine | Formula Workbench |
|---------|--------|-------------------|
| Version source | (follows phase milestones) | `WORKBENCH_VERSION` in `workbench.h` |
| Changelog | `ROADMAP.md` phases | `tools/formula_workbench/CHANGELOG.md` |
| Release cadence | Phase-gated | Feature-gated |

When updating the workbench:
1. Bump `WORKBENCH_VERSION` in `workbench.h` using SEMVER (major for breaking export format changes, minor for new features, patch for fixes).
2. Add a new section at the top of `CHANGELOG.md` with today's date.
3. The commit that changes `WORKBENCH_VERSION` should also contain the `CHANGELOG.md` update — same rule as the audit tool.

## Standalone use

The workbench can run against any Vestige formula library JSON, including ones produced outside the engine's build. There are no plans to distribute it as a separate project — it has no meaningful purpose without the engine's formula schema — but the build target is standalone, so you can ship just `formula_workbench` to data scientists or designers without the full engine.

## Why it isn't its own repo

Short answer: it's tightly coupled to `engine/formula/*`, has exactly one user (the engine), and splitting now adds cross-repo sync overhead without benefit. If the formula library ever ships as a standalone C++ math library, this tool would be a natural companion and could move with it. Until then: monorepo with a clean seam.
