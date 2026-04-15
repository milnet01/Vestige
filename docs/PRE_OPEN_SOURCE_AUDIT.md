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
- [ ] Grep for personal identifiers: full name, personal email (beyond the Git author name you've already chosen to publish), home address, phone number, license plates, house photos in committed screenshots.
- [ ] Review `imgui.ini` and similar editor layout files — these can contain window paths, recent-file lists. Decide whether to commit a clean default or `.gitignore` them.
- [ ] Review committed screenshots/recordings for incidentally captured personal info (desktop notifications, browser tabs, system tray apps).
- [ ] Check `.clang-format`, CMake presets, and IDE configs for machine-specific paths.
- [ ] Review `CLAUDE.md` and any other AI-assistant memory files — these often contain project context that's fine to publish, but occasionally contain private notes. Skim before shipping.

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

- [ ] List every dependency pulled in via `CMakeLists.txt` / `FetchContent` (GLFW, GLM, ImGui, Jolt, OpenAL, FreeType, etc.). Versions pinned.
- [ ] For each dependency, capture its license in `THIRD_PARTY_NOTICES.md`. Most are MIT / BSD / zlib (compatible with any permissive engine license), but verify — especially:
  - FreeType (FTL or GPLv2 dual-license — choose which one applies)
  - Jolt Physics (MIT)
  - OpenAL-Soft (LGPL — dynamic linking is fine; static linking has implications)
- [ ] Confirm no GPL/AGPL dependencies are statically linked. LGPL is OK via dynamic linking only.
- [ ] If you use any code snippets copied from Stack Overflow, tutorials, or papers — attribute them. MIT-licensed SO content can be used with attribution; CC-BY-SA content (some docs) cannot be embedded in an MIT project without care.

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

- [ ] `CLAUDE.md` — decide whether to keep as-is, rewrite for public audience, or move to a private branch. (Signals "AI-assisted development" which some contributors will judge either way.)
- [ ] `AUDIT.md`, `AUDIT_STANDARDS.md`, `AUDIT_TOOL_AND_WORKBENCH_IMPROVEMENTS.md`, `DISCOVERY.md`, `FIXPLAN.md` — strong internal-planning flavor. Decide: publish (shows rigor), move to `docs/internal/` with a note, or omit.
- [ ] `docs/AUTOMATED_AUDIT_REPORT_*.md` files — timestamped internal reports. These **should probably be gitignored going forward** and not published. Historical ones can stay if clean.
- [ ] `docs/*_RESEARCH.md` — research notes. Usually fine to publish; review for anything cited under fair-use that wouldn't survive redistribution.
- [ ] `EXPERIMENTAL.md` — fine to publish if it describes what the engine can/can't do; scrub if it describes unreleased commercial plans.
- [ ] `ROADMAP.md` — great to publish; verify the **Open-Source Release** section still reflects the current licensing choice (see §1).

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
- [ ] If you've committed and later deleted *additional* large assets after the 2026-04-15 sweep, they're still in history. Consider:
  ```bash
  git filter-repo --analyze   # reports largest blobs
  ```
  Decide whether to strip or accept the size.
- [ ] Review recent commit messages for: profanity, personal frustration notes ("this stupid bug"), references to private conversations, temporary password/token pastes.

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
