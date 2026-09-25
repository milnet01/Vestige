# Spec-format overrides — Vestige

Deltas to `~/.claude/standards/spec-format.md` for this project. That file
governs everything not named here. `write-spec` applies these deltas when it
drafts. `spec_lint` does not read this file, so nothing here is checked
mechanically; the cold read is what catches a breach.

## 1. CPU / GPU placement — conditionally required

**Delta to spec-format.md § 4.** Add a `CPU / GPU placement` section, appended
after § 3's twelve like any § 4 section, to every spec whose work touches the
GPU or runs per frame or per element. It is required in that case, not
recommended. A spec for other work (a save schema, localisation) omits it,
as § 4 already says of a section that does not apply.

The section states the choice and the reason. `CODING_STANDARDS.md` § 17
owns the heuristic and the dual-implementation rule, and the section cites
it rather than restating it. `CLAUDE.md` rule 7 is the policy this delta
serves.

**Why a delta.** spec-format.md's required sections carry no placement
section, so without this a spec could pass every check and still breach
rule 7.

## What checks this

The rule-14 cold read of each spec, which is handed this file as a
cross-reference. Nothing mechanical checks it.

## Cold-eyes loop log

Written by `review-contract`. One row per loop, oldest first. Never
back-filled; a correction goes in the current loop's row.

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Verified | Fixed | Outcome |
|---|---|---|---|---|---|---|---|---|
