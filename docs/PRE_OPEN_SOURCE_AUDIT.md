# Pre-Open-Source Audit Checklist

A systematic walkthrough to run against the Vestige repository **before** publishing it to a public host (GitHub, GitLab, Codeberg, etc.). The goal is to catch anything that would embarrass you, expose personal data, violate a third-party license, or create legal ambiguity for downstream users — while the repo is still private and you can fix it.

Work through the sections in order. Each item is either a **decision** (think / discuss) or an **action** (do / verify). Check boxes as you go.

> Run time estimate: ~half a day for a single-maintainer repo of this size, assuming no major secret-in-git-history surprises. If secrets are found in history, add a full day for `git filter-repo` + coordination.

---

## 1. Licensing Decisions (MUST land first)

Everything else depends on these choices. Do not proceed past Section 1 until these are answered.

- [x] ~~**Choose an engine license.**~~ **Decided: MIT License.** Copyright holder: **Anthony Schemel**. `LICENSE` file added at repo root (2026-04-14). Rationale documented in ROADMAP.md § Open-Source Release.
- [x] ~~**Decide on CLA / DCO.**~~ **Decided: DCO** (`git commit -s` → `Signed-off-by:`). Lightweight, no CLA friction. To be documented in `CONTRIBUTING.md` at launch prep time.
- [x] ~~**Confirm asset license boundary.**~~ **Decided.** Biblical content projects (Tabernacle, Solomon's Temple) live in separate private repos. Engine-shipped sample assets must be redistributable (CC0 / CC-BY). Asset library (`/mnt/Storage/3D Engine Assets/`) is per-user config, never committed.
- [ ] **Trademark decision.** "Vestige" as a name is separate from code copyright. Decide whether to informally use it (acquired through use) or formally register. Formal registration is optional; likely defer until there's something worth protecting at scale.
- [x] ~~**Update `ROADMAP.md` "Commercial Vision" section.**~~ Done — rewritten as **"Open-Source Release"** reflecting MIT model, DCO, AI-assisted development disclosure, semver/backwards-compat commitment, and no-relicense pledge.
- [x] ~~**AI-assisted development disclosure.**~~ **Decided.** Project openly disclosed as AI-assisted (Claude Code); contributors must disclose AI use in PRs; "AI wrote it" never absolves the committer from review responsibility. Captured in ROADMAP.md.
- [x] ~~**Re-licensing policy.**~~ **Decided.** Engine stays MIT forever. No dual-licensing, no relicense-to-proprietary, no CLA enabling relicense. Captured in ROADMAP.md.
- [x] ~~**Versioning.**~~ **Decided.** Semver from 1.0; backwards compatibility commitment once 1.0 ships, with a rare-breaking-change exception for industry shifts (GPU API transitions, C++ standard changes). Captured in ROADMAP.md.

---

## 2. Secret Scanning (automated + manual)

- [x] ~~Install and run a secret scanner across the working tree AND full git history.~~ ✅ Done — gitleaks 8.30.1 installed locally; `.gitleaks.toml` config in repo with allowlist for the 3 known false positives. **Re-run before each public push** to confirm zero leaks.
- [x] ~~Manually grep for known secret-shaped strings.~~ ✅ Done — confirmed no real secrets remain (rotated NVD key was the only real hit; scrubbed from history via `git filter-repo` on 2026-04-15).
- [x] ~~Specifically verify the **NVD API key** is NOT in the repo.~~ ✅ Done — rotated 2026-04-13, removed from current HEAD, and history-rewritten on 2026-04-15 (commits `5aa7eba` / `f5cc472` / `91d66a8` no longer contain the literal). Backup tag `pre-key-redaction-backup-2026-04-14` preserved on remote in case a rollback is ever needed.
- [x] ~~Check `.gitignore` covers all common secret locations.~~ ✅ Done — `.env*`, `*.key`, `*.pem`, `*.p12`, `*.token`, `credentials.*`, `secrets.*`, `.aws/`, `.ssh/`, `id_rsa`, `id_ed25519`, `*.netrc` patterns added.
- [x] ~~If secrets were ever committed: rewrite git history.~~ ✅ Done — `git filter-repo --replace-text` redacted the rotated NVD key. **One more pre-launch sweep recommended** with `gitleaks detect --log-opts=--all` to confirm.

---

## 3. Personal / Machine-Specific Data

- [x] ~~Grep for hardcoded absolute paths that leak your machine layout.~~ ✅ Done (audit tool 2.4.1, commit `1c9a6b5`) — scrubbed from `tools/audit/web/app.py`, `tools/audit/lib/agent_playbook.py`, `tools/audit/AUDIT_TOOL_STANDARDS.md`, `tools/audit/CHANGELOG.md`. Re-grep before public push:
  ```
  /home/ants, /home/<username>, /mnt/Storage, C:\Users\
  ```
  Original hits on the working tree (now resolved):
  - `tools/audit/web/app.py`
  - `tools/audit/lib/agent_playbook.py`
  - `tools/audit/AUDIT_TOOL_STANDARDS.md`
  - `tools/audit/CHANGELOG.md`

  Replace with environment variables, config entries, or relative paths. User-specific values (asset library location, scratch dirs) must be read from a per-user config, not baked into code.
- [x] ~~Grep for personal identifiers~~ ✅ Done (2026-04-15). Sweep: `Anthony Schemel` — appears only in MIT copyright headers (703 files, intentional, public) and in this checklist. `aant.schemel@gmail.com` — appears only in `SECURITY.md` (public disclosure address, intentional) and this checklist. No home address, phone, license plates. `/home/ants` — zero matches. `C:\Users\` — zero matches. `/mnt/Storage` — one self-referential leak found in `tools/audit/CHANGELOG.md` §2.4.1 (the "path scrub" entry ironically quoted the literal path it described as removed); scrubbed in audit tool 2.8.1 (commit `838d5b2`).
- [x] ~~Review `imgui.ini` and similar editor layout files~~ ✅ Done (2026-04-15) — `imgui.ini` is `.gitignore`d (line 44) and confirmed untracked via `git ls-files`. `NodeEditor.json` likewise ignored (line 47). No other layout files found.
- [x] ~~Review committed screenshots/recordings for incidentally captured personal info~~ ✅ Done (2026-04-15) — `git ls-files` shows no `.png`/`.jpg`/`.mp4`/`.gif` screenshots or recordings are tracked. Contributor screenshots live in `~/Pictures/Screenshots/` per maintainer convention (never committed).
- [x] ~~Check `.clang-format`, CMake presets, and IDE configs for machine-specific paths~~ ✅ Done (2026-04-15). `.clang-format` — standard Google-base config with Vestige overrides, no paths. No `CMakePresets.json` in the repo. No `.vscode/`, `.idea/`, or `*.code-workspace` committed (gitignore lines 10-11).
- [x] ~~Review `CLAUDE.md` and any other AI-assistant memory files~~ ✅ Done (2026-04-15) — see §7. Clean for public; no action needed.

---

## 4. Asset Boundary

> ✅ **RESOLVED as of 2026-04-15** — the assets directory now ships
> only redistributable content. All previously-identified blockers
> have been remediated; see the individual line items below.

### Known concerns — status
- [x] ~~**`assets/textures/Texturelabs_*.jpg`** — from texturelabs.org. License
      forbids redistribution.~~ ✅ Done — 46 files `.gitignore`d (`.gitignore:113`:
      `assets/textures/Texturelabs_*`). Demo-scene code in `engine/core/engine.cpp`
      now uses CC0 Poly Haven `plank_flooring_04` for the Wood block and a
      texture-less PBR gold (metallic/roughness) for the Gold block;
      maintainer keeps local copies untracked for offline development only.
- [x] ~~**`assets/models/CesiumMan.glb` / `Fox.glb` / `RiggedFigure.glb`**~~ —
      ✅ Done (commit `564e4fd`) — each model verified against the Khronos
      glTF Sample Assets repo, per-model attribution recorded in
      `ASSET_LICENSES.md` (§Models) and `THIRD_PARTY_NOTICES.md`.
- [x] ~~**`assets/fonts/default.ttf`**~~ — ✅ Done (commit `31c8e58`) —
      identified as Arimo by Steve Matteson, SIL OFL 1.1; `OFL.txt` shipped
      alongside the font; Reserved Font Name "Arimo" preserved.
- [x] ~~**`assets/textures/everytexture-com-stock-rocks-*`**~~ — ✅ Done —
      license forbids redistribution; files `.gitignore`d; demo ground now
      renders untextured grey until a CC0 replacement lands via
      `VestigeAssets`.

### General audit — status
- [x] ~~Enumerate every asset in `assets/` and verify source + license +
      redistribution + attribution.~~ ✅ Done — [`ASSET_LICENSES.md`](../ASSET_LICENSES.md)
      tabulates shaders (MIT, engine-authored), models (Khronos CC-BY 4.0 /
      CC0), fonts (OFL 1.1), and textures (Poly Haven CC0 + engine-authored
      labels), with an explicit "excluded" table for the non-redistributable
      files kept locally.
- [x] ~~Confirm assets from `/mnt/Storage/3D Engine Assets/` are not in the
      engine repo.~~ ✅ Done — verified by gitleaks sweep and `git ls-files`
      grep; all hardcoded `/mnt/...` paths scrubbed (audit tool 2.4.1).
- [x] ~~Biblical project content lives in **separate repos**.~~ ✅ Done —
      `tabernacle/` content `.gitignore`d; removed from history in the
      2026-04-15 `git filter-repo` sweep.
- [x] ~~Audio and fonts double-check.~~ ✅ Done — no `.wav`/`.mp3`/`.ogg`
      shipped in the engine repo; the single shipped font (`default.ttf`)
      is OFL 1.1.
- [x] ~~Create `ASSET_LICENSES.md`.~~ ✅ Done — at repo root, linked
      from `README.md` and `THIRD_PARTY_NOTICES.md`.

---

## 5. Third-Party Dependencies and Attribution

- [x] ~~List every dependency pulled in via `CMakeLists.txt` / `FetchContent`~~ ✅ Done — 15 FetchContent deps + 3 vendored sources (glad / stb / dr_libs) tabulated in [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md). All `GIT_TAG` / URL versions pinned in `external/CMakeLists.txt`.
- [x] ~~For each dependency, capture its license~~ ✅ Done in `THIRD_PARTY_NOTICES.md`. Special cases resolved:
  - **FreeType** — FreeType Project License (BSD-style) explicitly chosen over the GPLv2 dual-licence alternative; version `VER-2-13-3` pinned.
  - **Jolt Physics** — MIT, version `v5.2.0`.
  - **OpenAL Soft** — LGPL v2.1, version `1.24.1`, linked **dynamically** (`external/CMakeLists.txt:352-365` uses the default shared-lib target; `engine/CMakeLists.txt:288` links `OpenAL::OpenAL`). No `LIBTYPE=STATIC` override anywhere.
- [x] ~~Confirm no GPL/AGPL dependencies are statically linked.~~ ✅ Done — no GPL/AGPL deps in the set. FreeType is BSD-style; OpenAL is LGPL dynamic. `grep -n 'GPL\|AGPL'` across `CMakeLists.txt`, `external/CMakeLists.txt`, `THIRD_PARTY_NOTICES.md`, `SECURITY.md` returns only the explanatory notes (OpenAL-Soft LGPL comment, FreeType-not-GPLv2 note).
- [x] ~~If you use any code snippets copied from Stack Overflow, tutorials, or papers — attribute them.~~ ✅ Done — searched for `stackoverflow`, `stack overflow`, `StackOverflow` across the owned tree. Four hits, none are copied code: `tests/test_scripting.cpp:1977` ("stack overflow" as a CS term in a recursion-limit test comment); `tools/audit/lib/tier5_research.py:38` and `tools/audit/IMPROVEMENTS_FROM_*.md:95` (listing `stackoverflow.com` as a trusted search domain for the CVE-research tier); `external/dr_libs/dr_wav.h` (vendored upstream library, carries its own licence). No SO snippets, tutorial code, or paper algorithms that would require CC-BY-SA attribution.

---

## 6. License, Copyright, and Repo Metadata

- [x] ~~Add `LICENSE` file at repo root.~~ ✅ Done — MIT, © 2026 Anthony Schemel (commit `689b78e`).
- [x] ~~Add copyright headers to source files.~~ ✅ Done — 703 files in `engine/`, `tests/`, `tools/`, `assets/shaders/` now carry `// Copyright (c) 2026 Anthony Schemel` + `// SPDX-License-Identifier: MIT` (commit `601e45b`).
- [x] ~~**Add `README.md`**~~ ✅ Done (2026-04-15) — public-facing README at
      repo root covering what Vestige is / isn't, project status
      disclosure, feature matrix by phase, quick-start build + test
      instructions, repository layout, documentation index, tooling
      pointers, contributing summary, security-reporting pointer, and
      license line.
- [x] ~~Add `CONTRIBUTING.md`.~~ ✅ Done (commit `4353634`) — DCO sign-off, AI-disclosure policy, build instructions, audit-tool-clean expectation, contributor cadence note.
- [x] ~~Add `CODE_OF_CONDUCT.md`.~~ ✅ Done (commit `4353634`) — adopts Contributor Covenant 2.1 by reference.
- [x] ~~Add `SECURITY.md`~~ ✅ Done (2026-04-15) — existing internal
      security-standards doc now prefaced with a "Vulnerability
      disclosure" section covering scope, reporting address
      (`aant.schemel@gmail.com` with `[vestige-security]` subject
      prefix), expected timelines, rewards (none — good-faith credit
      in the changelog), and a safe-harbour statement.
- [x] ~~**Add `.github/ISSUE_TEMPLATE/`**~~ ✅ Done (2026-04-15) —
      `bug_report.md`, `feature_request.md`, `config.yml` (blank issues
      disabled; contact links redirect to SECURITY.md and Discussions).
- [x] ~~**Add `.github/PULL_REQUEST_TEMPLATE.md`**~~ ✅ Done (2026-04-15) —
      summary + kind-of-change classifier + contributor checklist
      (DCO, coding standards, tests, ctest, audit tool, CHANGELOG,
      Formula Workbench use, no-workarounds discipline) + explicit
      AI-assistance disclosure line.
- [x] ~~Add `THIRD_PARTY_NOTICES.md` for dependency attribution.~~ ✅ Done (commit `4353634`) — covers 15 FetchContent deps, 3 vendored sources (glad/stb/dr_libs), and shipped asset attributions (glTF models, Arimo OFL, Poly Haven CC0).

---

## 7. Internal Docs Review

Some docs were written for an internal audience and may leak private context or be confusing to outsiders. Review each:

- [x] ~~`CLAUDE.md` — decide whether to keep as-is, rewrite for public audience, or move to a private branch.~~ ✅ Done (2026-04-15) — skimmed and kept as-is. Covers tech stack, hardware specs, performance targets, architecture pointer, key rules, and coding standards summary. No private notes, no secrets. The AI-collaboration norm is already publicly documented in `CONTRIBUTING.md` § AI-assisted contributions.
- [x] ~~`AUDIT.md`, `AUDIT_TOOL_AND_WORKBENCH_IMPROVEMENTS.md`, `DISCOVERY.md`, `FIXPLAN.md` — strong internal-planning flavor.~~ ✅ Done (2026-04-15) — **maintainer decision: remove**. Previous audits performed against the codebase are not for the public and have nothing to do with downstream users of the engine. All "previous-audit" docs removed from tracking; future ones caught by `.gitignore` patterns. Process/standards docs kept (`AUDIT_STANDARDS.md`, `tools/audit/AUDIT_TOOL_STANDARDS.md`).
- [x] ~~`docs/AUTOMATED_AUDIT_REPORT_*.md` files — timestamped internal reports.~~ ✅ Done (2026-04-15) — same decision as above. Automated-run snapshots (both `.md` and `.json` variants) `.gitignore`d; the JSON result files additionally leaked absolute filesystem paths, which is now blocked at the tracking layer. Historical `.md` snapshots were never tracked in the first place (confirmed via `git ls-files`).
- [x] ~~`docs/*_RESEARCH.md` — research notes.~~ ✅ Done (2026-04-15) — kept as-is. These are design rationale documents (e.g. `CLOTH_SIMULATION_RESEARCH.md`, `BVH_COLLISION_RESEARCH.md`) that cite upstream papers and tutorials; they help explain *why* subsystems are built the way they are. Spot-checked — citations are standard fair-use-style references (links to papers / docs), not embedded copyrighted content.
- [x] ~~`EXPERIMENTAL.md`~~ ✅ Done (2026-04-15) — removed. The file was explicitly framed as "Surfaced during the 2026-04-13 full audit" and referenced removed audit finding IDs (H16, AT-A4, H12, M23, M27…). Two of its items (E8 engine-level VERSION/CHANGELOG, E9 minimal CI) were already shipped. The still-live forward-looking items can be added to `ROADMAP.md` as they become actionable. Consistent with the no-previous-audits policy applied in §7.
- [x] ~~`ROADMAP.md`~~ ✅ Done (2026-04-15) — verified. The **Open-Source Release** section (lines 1903-1988) accurately reflects MIT, DCO, AI-disclosure, semver-from-1.0, no-relicense pledge, and the contribution model. Reconciled stale "Still pending" items (README / `.github/` templates / SECURITY disclosure) against the launch-prep paperwork commit (`eaf3c06`), and added a self-referential reminder to remove `VESTIGE_FETCH_ASSETS=OFF` from CI at go-live.

---

## 8. CI, Infrastructure, and Build

- [ ] Review `.github/workflows/` (or equivalent) — ensure no secrets-in-env-vars, no references to private runners, no hardcoded paths.
- [ ] Confirm CI will work on the public fork — public repos get free GitHub Actions minutes, but workflow permissions behave differently (no write access by default, no secret access from forks).
- [ ] Remove any self-hosted-runner references unless you're keeping that infrastructure public.
- [ ] Verify the audit tool works as a public check — does it require the NVD API key? If so, document the `NVD_API_KEY` environment variable and make its absence non-fatal.
- [ ] `.pre-commit-config.yaml` — verify all hooks are public / installable by contributors.
- [ ] Confirm `build/` is gitignored (it is; 53 LICENSE files confirm it pulls deps there).
- [ ] **Add a CMake version matrix to CI.** The engine's `external/CMakeLists.txt` uses a SOURCE_SUBDIR trick to populate FetchContent deps without invoking their upstream `add_subdirectory`. The trick is stable today but depends on CMake FetchContent semantics that periodically tighten (CMP0169 already bit us once on `FetchContent_Populate`). Run the build on at least three CMake versions: the project min `3.20`, the current LTS-distro `3.28`, and `latest`. Catches silent regressions in a PR check rather than a downstream user's report. Migration paths if the pattern ever does break are documented in the `IF THIS BREAKS` block at the top of `external/CMakeLists.txt`.
- [ ] **Remove `-DVESTIGE_FETCH_ASSETS=OFF` from `.github/workflows/ci.yml`** when
      `milnet01/VestigeAssets` goes public alongside `Vestige`. Temporary
      flag added 2026-04-15 after CI started failing with
      `fatal: could not read Username for 'https://github.com'` on the
      unauthenticated clone of the still-private sibling repo. The engine
      supports the off-state (top-level `CMakeLists.txt:87` falls back to
      in-engine assets only), so build + ctest coverage is preserved in
      the meantime. See the comment on the `Configure` step in
      `linux-build-test` for the full rationale. Pair this flip with the
      "flip both repos public" item in §11.

---

## 9. Git History Hygiene

- [x] ~~Review commit author names and emails across history.~~ ✅ Done — single identity in history (`milnet01 <aant.schemel@gmail.com>`); confirmed publishable.
- [x] ~~Large binary files in history bloat the public repo.~~ ✅ Done — `git filter-repo` sweep on 2026-04-15 removed 100 historical asset paths (Texturelabs / tabernacle / everytexture / migrated 4K assets). `.git` shrunk from 552 MB to 21 MB. Backup tag `pre-asset-history-sweep-2026-04-15` preserved on remote.
- [x] ~~Check for post-2026-04-15 large-asset leaks in history.~~ ✅ Done (2026-04-15) — ran `git rev-list --objects --all | git cat-file --batch-check` sorted by blob size. Top 20 blobs (≈17 MB total) are all expected: Poly Haven CC0 2K demo textures (brick/plank/red_brick sets, 2-3.4 MB each), vendored `external/glad/` and `external/dr_libs/` / `external/stb/` single-header libs (200-880 KB each), attributed Khronos glTF models (CesiumMan 438 KB, Fox 162 KB), and `assets/fonts/default.ttf` (Arimo OFL, 414 KB). No surprise large blobs post-sweep. Total `.git` size stayed at ~21 MB as expected.
- [x] ~~Review recent commit messages for profanity / personal frustration / private-conversation refs / temporary credential pastes.~~ ✅ Done (2026-04-15) — skimmed the last 80 commit subjects. All professional, technical, well-scoped. No profanity, no "stupid bug" notes, no private conversation references, no temporary-token pastes. Clean.

---

## 10. Pre-Launch Dry Run

- [ ] Clone the repo into a fresh directory with nothing else around and try to build it from scratch, following only the README. This catches "works on your machine" dependencies.
- [ ] Have a friend or second Claude Code session do the same clean-clone build.
- [ ] Open the repo in a fresh editor window and grep the project's own directory for: your full name, home address, personal phone, any credential-looking strings. Last chance.
- [ ] Tag a pre-release (`v0.x.0-preview`) as the first public commit's parent, so users can see "this is where the public history starts."

---

## 11. Day-Zero Launch Prep

- [ ] Write a launch post / README opening paragraph that sets expectations: "solo-maintained, early-stage, API unstable, contributions welcome but response time variable."
- [ ] Decide on a communication channel: GitHub Discussions (zero setup), Discord, Matrix. Easier to add than to remove.
- [ ] Decide on a **public roadmap visibility** — the current ROADMAP.md is very detailed. Great for credibility; may also invite "when is X?" pressure. Either is fine; just know which you're signing up for.
- [x] ~~Archive or rotate any credentials that EVER lived in the repo, regardless of whether you scrubbed history.~~ ✅ Done — NVD API key rotated 2026-04-13, history scrubbed 2026-04-15. No other credentials ever lived in the repo per gitleaks scans.
- [ ] **One final pre-launch sweep**, in this order:
  ```bash
  gitleaks detect --source . --config .gitleaks.toml --log-opts=--all
  python3 tools/audit/audit.py        # full audit, expect zero new findings
  rm -rf build && cmake -B build -S . && cmake --build build -j && ctest --test-dir build
  ```
  All three must succeed cleanly.
- [ ] **Tag a pre-release** (`v0.1.0-preview` or similar) on `Vestige` AND `VestigeAssets` together — the engine's `external/CMakeLists.txt` currently pins `VestigeAssets v0.1.0`; bump both repos in lockstep.
- [ ] **Flip both `Vestige` AND `VestigeAssets` from private to public in GitHub Settings.** Must happen together so the engine's CMake `FetchContent` works on fresh clones without authentication. This is the actual "go-live" moment.
- [ ] Verify a fresh clone (`git clone https://github.com/milnet01/Vestige.git`) on a clean machine still configures, builds, and runs the demo scene.
- [ ] Announce on whatever channel you chose. Pin the launch issue / discussion thread.

---

## Post-Launch Maintenance (ongoing)

- [ ] Add a secret-scan step to CI (`gitleaks` as a pre-commit and PR check) so contributors can't accidentally reintroduce secrets.
- [ ] Enable GitHub's secret scanning and Dependabot security updates.
- [ ] Set up a triage rhythm: one pass per week on issues and PRs is the minimum sustainable cadence for a solo maintainer.
- [ ] Revisit the ROADMAP quarterly; update with "shipped in v0.x" as things land.

---

## Abort Conditions

Stop the open-sourcing process and reassess if you hit any of these:

- **Secrets found in git history that were ever pushed to a remote** — rotate everything, consider whether to rewrite history or accept a "fresh-start" public repo with a disclosure in the README.
- **Asset license audit reveals un-redistributable content mixed into the repo** — separating this out cleanly may take days, not hours.
- **Dependency license audit reveals a GPL/AGPL static link** — switch dependency or relicense accordingly.
- **You discover a desire to retain commercial control** — stop and revisit the licensing decision in §1 before anything else. Going open is one-way; contributors' commits to an open repo cannot be retroactively relicensed.
