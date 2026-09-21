# check-code suppressions — recurring false positives and won't-fix classes

Read by `check-code` step 7, matched against raw findings. **Only entries with
a matchable key are used** — a tool plus a rule id, or a path glob. A narrative
entry with no key is skipped and counted as skipped.

Every entry below names what was measured and when. An entry with no
measurement behind it does not belong here: the point of this file is to move
a known-noisy class out of the actionable list, not to make a report look
clean.

---

## cppcheck: `unusedStructMember`

- **Tool:** cppcheck
- **Rule:** `unusedStructMember`
- **Measured:** 2026-08-31 — 1012 of 1648 cppcheck findings.

An artefact of per-translation-unit analysis on a header-heavy tree: a struct
field used only from another TU reads as unused. It cannot be made accurate
without whole-program analysis. Suppressed at the tool rather than triaged on
every run.

## cppcheck: `missingIncludeSystem`

- **Tool:** cppcheck
- **Rule:** `missingIncludeSystem`

cppcheck is not given the full system include path on purpose — supplying it
makes the run an order of magnitude slower for no finding this project acts on.

## ruff: `S101` in `tests/`

- **Tool:** ruff
- **Rule:** `S101`
- **Path glob:** `tests/**`
- **Measured:** 2026-08-31 — 1427 of 1880 ruff findings.

`assert` is what a test is made of. The rule stays live everywhere else, so
excluding the directory is narrower than excluding the rule.

## typos: domain vocabulary

- **Tool:** typos
- **Measured:** 2026-08-31 — `LOD` alone accounted for 1385 findings.

Handled in `_typos.toml` at the repo root rather than here, so that `typos`
run by hand or by an editor gets the same vocabulary. Verified 2026-09-21:
`LOD`, `LODs` and `froxel` no longer flag inside the repo.

## typos: binary assets

- **Tool:** typos
- **Path glob:** `assets/**/*.{jpg,png,hdr,exr,glb,ttf,zip,blend}`
- **Measured:** 2026-08-31 — 5747 of 8062 findings (71%) were byte sequences
  inside binary asset files.

A byte sequence inside a JPEG is not a misspelling.
