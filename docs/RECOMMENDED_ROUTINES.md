# Recommended Claude Code Routines — Vestige 3D Engine

This document captures the routines that earn their daily-quota slot
(`0/15` on Max, `0/5` on Pro) for the Vestige engine repository. Each
routine is ready to paste into the **New routine** form at
[claude.ai/code/routines](https://claude.ai/code/routines) — or via
`/schedule` in any session for the schedule-only triggers.

Routines run on Anthropic-managed cloud infrastructure as full Claude
Code sessions. They count against your account's daily routine cap
(separate from interactive subscription usage), and anything they push
appears as **you** via your connected GitHub identity. See [Automate
work with routines](https://code.claude.com/docs/en/routines) for the
full mechanics.

Companion document for the Ants Terminal repo lives at
`/mnt/Emulators/Scripts/Linux/Ants/docs/RECOMMENDED_ROUTINES.md` — both
share the same daily quota since routines are account-scoped.

---

## Why these specific routines

Vestige has high-leverage automation gaps that match routines exactly:

- **`AUDIT_STANDARDS.md` defines a 5-tier audit** — CI currently runs
  only Tier 1 (`.github/workflows/ci.yml` skips Tier 5 because the NVD
  CVE-research path needs `NVD_API_KEY` which can't live in a public
  CI secret). Routines have per-routine env vars, so Tiers 4-5 can run
  nightly without a CI-secret leak.
- **The 60 FPS hard requirement** (CLAUDE.md rule 8) deserves an
  unattended weekly perf-regression sentry — humans don't notice a
  smooth 5 % FPS slip until the game-feel testing lands.
- **The two-repo asset story** (engine MIT + private `VestigeAssets`)
  needs periodic re-audit that the public repo stays redistributable
  — a single accidentally-committed Texturelabs PNG would taint the
  whole repo's git history.
- **Phase-based development** (rule 9: post-phase audit before next
  phase) maps cleanly to a GitHub-triggered routine on a `phaseN-done`
  label or branch tag.

The four routines below cover those four gaps: **CVE research**,
**post-phase audit**, **asset-license sweep**, and **60 FPS regression
watcher**.

---

## 1. Nightly Tier 5 CVE + dependency research

**What it does.** Fills the `AUDIT_STANDARDS.md` Tier 5 gap that CI
can't do. Pulls the dependency manifest (`external/CMakeLists.txt`,
`vcpkg.json` if present), queries the NVD API for any CVE filed
against the listed versions since the last run, summarises in a
machine-readable JSON + a human-readable markdown PR.

**Trigger.** Schedule → **Daily**, 02:00 local.

**Repository.** `<your-vestige-github>/vestige`. Branch pushes
restricted to `claude/*` (default).

**Environment.** Default with **Network access: full** (NVD API call).
**Environment variables**: `NVD_API_KEY=<your key>` — required for
Tier 5; without it the routine no-ops with a clear status message
instead of producing partial results.

**Connectors.** None required.

**Paste this prompt into the form:**

```
Tier 5 CVE + dependency research, per AUDIT_STANDARDS.md §6.

Procedure:
1. Read the dependency manifest:
   - external/CMakeLists.txt — every find_package(), FetchContent_Declare(),
     and add_subdirectory(external/<dep>) call. Extract the dep name + the
     pinned version (commit SHA or tag).
   - tools/audit/dependencies.json if it exists (the Tier 5 cache).
2. For each dep, query the NVD API 2.0:
     curl -s -H "apiKey: $NVD_API_KEY" \
       "https://services.nvd.nist.gov/rest/json/cves/2.0?keywordSearch=<dep>&pubStartDate=<last-run-iso8601>&pubEndDate=<now-iso8601>"
   The last-run timestamp lives in tools/audit/dependencies.json under
   `last_tier5_check`. If absent, look back 7 days.
3. For each CVE returned, cross-reference against the pinned version.
   Mark applicable / not-applicable / unknown.
4. Update tools/audit/dependencies.json: bump last_tier5_check, append
   new findings to the per-dep history.
5. If at least one APPLICABLE CVE is new, open a PR titled
   `audit-tier5: <N> new CVE(s) affecting pinned deps — <YYYY-MM-DD>`
   on a `claude/audit-tier5-<YYYY-MM-DD>` branch with:
   - The updated dependencies.json
   - A new docs/AUDIT_TIER5_<YYYY-MM-DD>.md report following the
     existing AUTOMATED_AUDIT_REPORT_*.md format conventions in docs/.
6. If NO new applicable CVEs, do not open a PR. Just commit the
   dependencies.json bump on a `claude/tier5-cache-<YYYY-MM-DD>` branch
   so the next run's window is correct, and write a one-line status to
   docs/AUDIT_TIER5_LATEST.md (overwrite each run).

If $NVD_API_KEY is unset, exit early with `echo "Tier 5 skipped: no
NVD_API_KEY in routine env" >&2`. Do NOT generate a partial report.
```

**Why nightly.** NVD updates throughout the day; a 24-h window means
zero-day CVEs against your pinned versions land in your inbox before
the next workday starts.

---

## 2. Post-phase audit kickoff

**What it does.** When a maintainer pushes a tag matching `phase*-complete`
or merges a PR labeled `phase-done`, runs the AUDIT_STANDARDS.md
multi-tier process end-to-end and opens a draft PR with the audit
report ready for human review.

**Trigger.** GitHub event → `pull_request.closed` filtered to
`is merged equals true` AND `labels include phase-done`.

Alternative form: `release.published` if you tag phase completions as
GitHub releases. Either trigger works; pick one and stick to it so
the routine doesn't fire twice.

**Repository.** `<your-vestige-github>/vestige`.

**Environment.** Same env as Routine #1 (needs NVD_API_KEY for the
Tier 5 segment). Setup script:

```bash
sudo apt-get update -y
sudo apt-get install -y cmake ninja-build clang clang-tidy clang-tools \
    cppcheck libglfw3-dev libglm-dev googletest libgtest-dev libgmock-dev \
    libxinerama-dev libxcursor-dev libxi-dev libxrandr-dev pkg-config
```

**Connectors.** None required.

**Paste this prompt into the form:**

```
A phase has just completed (PR with `phase-done` label was merged). Run
the full post-phase audit per AUDIT_STANDARDS.md. The 5-tier process is:

Tier 1 — Automated Tools. Run the in-tree tools/audit catalogue. The
exact invocation is documented in tools/audit/README.md; reproduce it
without relying on the dialog UI.

Tier 2 — Pattern Grep Scan. Apply the grep patterns documented in
AUDIT_STANDARDS.md §3.2 across src/ and engine/. Match the rule list
to the language (C++ for engine, GLSL for shaders).

Tier 3 — Changed-File Deep Review. Scope: every file touched by the
phase since the previous phase-done tag. List with:
   git diff --name-only $(git describe --tags --match "phase*-complete" --abbrev=0 HEAD~1)..HEAD
For each file in the list, do a focused security + correctness review
keyed to AUDIT_STANDARDS.md §3.3.

Tier 4 — Full-Codebase Categorical Sweep. Spawn parallel subagents
(each subagent_type=general-purpose) to scan one category each:
memory safety, input validation, GL state correctness, shader
correctness, build hygiene, test coverage gaps. Subagents return
findings as structured lists; merge into the report.

Tier 5 — Online Research. If $NVD_API_KEY is set, run the same NVD
sweep as Routine #1 but scoped to the new dependencies introduced in
this phase. Plus: research experimental features that could benefit
the engine (one or two ideas, well-cited).

Output: open a DRAFT PR (not ready-for-review) titled
`audit: phase <N> post-completion report` on a
`claude/phase-<N>-audit-<YYYY-MM-DD>` branch with:
- docs/PHASE_<N>_AUDIT_<YYYY-MM-DD>.md following the existing
  AUTOMATED_AUDIT_REPORT_*.md template structure.
- The findings table sorted: blockers first, then high, medium, low,
  info.
- Each finding has a proposed verdict: REAL / FALSE-POSITIVE /
  NEEDS-USER-DECISION.
- NEEDS-USER-DECISION items are explicitly tagged so the user can
  resolve them before the next phase begins (per AUDIT_STANDARDS.md
  rule: "audit findings must be resolved first").

DO NOT push fixes. The audit-then-plan-then-implement loop is human-
gated by design. Your job is the audit half only.
```

**Why GitHub-triggered, not nightly.** Phases are episodic events.
Running Tier 4 (parallel subagent sweeps) on a quiet day is wasteful;
running it the moment a phase closes is exactly right.

---

## 3. Weekly asset-license sweep

**What it does.** Cross-checks every file under `assets/` (in the
public engine repo, NOT VestigeAssets) against `ASSET_LICENSES.md`.
Catches:
- Files added without a corresponding `ASSET_LICENSES.md` entry
- Entries in `ASSET_LICENSES.md` whose listed file no longer exists
- File hashes that drifted from the recorded fingerprint (if the
  format records hashes; add this if not)
- Any binary file >100 KB missing a CC0/CC-BY/MIT-equivalent license
  citation, since large binaries are the redistributability risk.

**Trigger.** Schedule → **Weekly**, Sunday 18:00 local.

**Repository.** `<your-vestige-github>/vestige`.

**Environment.** Default. No special setup.

**Connectors.** None required.

**Paste this prompt into the form:**

```
Asset-license sweep against ASSET_LICENSES.md. Goal: keep the public
Vestige repo redistributable.

Scope:
1. List every file under assets/ in the engine repo. Use:
     git ls-files assets/
2. Parse ASSET_LICENSES.md. Each section header is a file or directory
   group; each section lists Source, License, Redistribution-status.
3. Three checks:
   (a) Files in assets/ that are NOT mentioned in ASSET_LICENSES.md.
   (b) Entries in ASSET_LICENSES.md whose referenced file is missing.
   (c) Binary files >100 KB whose license is anything other than:
       CC0, CC-BY-3.0, CC-BY-4.0, CC-BY-SA-3.0, CC-BY-SA-4.0, MIT,
       Apache-2.0, BSD-3-Clause, public-domain, or "shipped under
       the engine MIT license".
4. Cross-reference against the historic problem categories called out
   in ASSET_LICENSES.md: Texturelabs (non-redistributable),
   everytexture.com rocks, biblical-project tabernacle content. If
   ANY of those vendors appear in the file listing or git history of
   assets/, flag as a blocker.

Output:
- If checks (a)-(c) are all clean and no historic-vendor matches,
  write a one-line status to docs/ASSET_LICENSE_STATUS.md (date +
  `clean`). No PR.
- If anything fails, open a PR titled
  `assets: license sweep <YYYY-MM-DD> — N issue(s)` on a
  `claude/asset-license-<YYYY-MM-DD>` branch with:
   - The detailed findings list (file, issue category, suggested
     remediation: add entry / remove file / migrate to VestigeAssets).
   - Updated docs/ASSET_LICENSE_STATUS.md with the per-issue table.
   - DO NOT delete files automatically. The user decides whether to
     fix the manifest or remove the asset.

If a blocker (historic-vendor match) appears, open the PR as
`assets: BLOCKER — possibly non-redistributable content detected`
and tag it with whatever urgent-review label the project uses.
```

**Why weekly.** Asset additions land in commits; weekly is fast enough
to catch a mistake before the next release tag without burning budget
on no-op runs.

---

## 4. 60 FPS regression sentry

**What it does.** Builds the engine in Release mode, runs the
benchmark suite (or a representative scene if no formal benchmark
exists yet), parses the per-scene FPS report, and opens a PR if any
scene's median FPS drops below 60 OR is more than 10 % slower than
the previous run.

**Trigger.** Schedule → **Weekly**, Saturday 14:00 local.

**Repository.** `<your-vestige-github>/vestige`.

**Environment.** Default. **Network access: full** for any package
fetches. Setup script:

```bash
sudo apt-get update -y
sudo apt-get install -y cmake ninja-build clang \
    libglfw3-dev libglm-dev libxinerama-dev libxcursor-dev \
    libxi-dev libxrandr-dev pkg-config mesa-utils \
    xvfb  # for offscreen GL context
```

The cloud env doesn't have a real GPU, so the sentry runs Mesa
software OpenGL (`MESA_GL_VERSION_OVERRIDE=4.5`,
`LIBGL_ALWAYS_SOFTWARE=1`) under Xvfb. Software rendering FPS is NOT
your real-hardware FPS — but **regressions** in software FPS correlate
strongly with regressions on hardware (the same fragment-shader cost
or draw-call inflation hits both). The routine's job is to detect
that regression vector early; the dev's RX 6600 box is the source of
truth for absolute numbers.

**Connectors.** None required.

**Paste this prompt into the form:**

```
60 FPS regression sentry. Goal: detect performance regressions weekly,
even though absolute FPS numbers come from a software-rendering cloud
env (not your hardware-measured baseline).

Procedure:
1. Build Release:
     cmake -S . -B build-release -GNinja -DCMAKE_BUILD_TYPE=Release
     cmake --build build-release --target vestige -j$(nproc)
2. Locate the benchmark binary or harness. If apps/benchmark/ exists,
   that's the entry point. If not, run `apps/walkthrough` with the
   default scene and capture frame timings via the engine's built-in
   Performance subsystem (logs to logs/perf_*.json).
3. Run the benchmark under Xvfb with software GL:
     Xvfb :99 -screen 0 1920x1080x24 &
     sleep 1
     export DISPLAY=:99
     export MESA_GL_VERSION_OVERRIDE=4.5
     export LIBGL_ALWAYS_SOFTWARE=1
     ./build-release/apps/benchmark --duration=60 --output=/tmp/perf.json
4. Parse /tmp/perf.json. Per-scene fields: name, median_fps, p99_fps,
   draw_call_count, vertex_count.
5. Compare against the previous run's stored snapshot:
   docs/PERF_BASELINE.json (create from this run if absent — first
   run is the baseline, no regression possible).
6. Flag a regression IF, for any scene:
   (a) median_fps < 60.0 (the hard floor; per CLAUDE.md rule 8)
   (b) median_fps < 0.9 * baseline.median_fps (10 % regression)
   (c) draw_call_count > 1.2 * baseline.draw_call_count (20 % more
       calls — even if FPS holds today, the budget got tighter)

Output:
- Always update docs/PERF_HISTORY_<YYYY-MM>.md (append-only) with
  this week's per-scene table.
- If NO regression, do not open a PR. Just commit the history update
  on a `claude/perf-history-<YYYY-MM-DD>` branch.
- If regression, open a PR titled
  `perf: <N> scene(s) regressed — <YYYY-MM-DD>` on a
  `claude/perf-regression-<YYYY-MM-DD>` branch with:
  - The flagged-scenes table (scene, today's median, baseline,
    delta %, blocker category).
  - A `git log --since=<last-baseline-date> --oneline` slice so the
    user has the candidate commits to bisect.
  - Optional: if the changed files since baseline include shaders,
    rendering code, or batching/culling code, propose a bisect plan.

ABSOLUTE NUMBERS in the cloud env are NOT comparable to your hardware
RX 6600 numbers — only DELTAS within the same env are meaningful. Make
this explicit in every report so a reader doesn't panic at "12 FPS"
when local hardware would give 280 FPS.
```

**Why weekly + Saturday.** Catches mid-week regressions before the
weekend; falls outside the typical commit cadence so you're reviewing
a whole week of work in one pass instead of fragmenting attention.

---

## Routine usage budget — the math

Daily Max cap is **15 runs**, shared across all your routines (Vestige
+ Ants Terminal + anything else):

| Routine                          | Cadence       | Runs/day average |
|----------------------------------|---------------|------------------|
| 1. Nightly Tier 5 CVE research   | Daily         | 1                |
| 2. Post-phase audit kickoff      | On phase-done | ~0.05            |
| 3. Weekly asset-license sweep    | Weekly        | 0.14             |
| 4. 60 FPS regression sentry      | Weekly        | 0.14             |
| **Vestige total**                |               | **~1.3 / day**   |
| (see Ants RECOMMENDED_ROUTINES.md|               | ~3.5 / day)      |
| **Combined (Ants + Vestige)**    |               | **~5 / day**     |

Leaves ~10 daily slots for ad-hoc `/schedule` invocations and future
additions.

---

## Maintenance

When `AUDIT_STANDARDS.md`, the dependency manifest format, the asset
license format, or the perf benchmark harness changes in a way that
breaks one of the prompts above, **update this file in the same commit
as the change**. Stale prompts mean broken nightly runs and PRs that
quote the wrong invariants.

The hook in `.claude/settings.json` watches `tools/hook-on-version-edit.sh`
triggers, but not this file — review `RECOMMENDED_ROUTINES.md` whenever
you touch `AUDIT_STANDARDS.md`, `ASSET_LICENSES.md`, or
`tools/audit/`.
