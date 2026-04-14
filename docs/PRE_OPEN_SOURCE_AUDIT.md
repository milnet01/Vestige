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

- [ ] Install and run a secret scanner across the working tree AND full git history:
  ```bash
  # Option A: gitleaks (recommended — fast, easy to CI)
  gitleaks detect --source . --verbose --redact
  gitleaks detect --source . --log-opts="--all" --verbose --redact  # full history

  # Option B: trufflehog (also scans history + verifies against live services)
  trufflehog git file://. --only-verified
  ```
- [ ] Manually grep for known secret-shaped strings (catches what scanners miss):
  ```
  api[_-]?key, secret, token, password, bearer, authorization,
  BEGIN RSA, BEGIN PRIVATE KEY, BEGIN OPENSSH, ssh-rsa, ssh-ed25519,
  AKIA (AWS), ghp_ (GitHub PAT), sk-ant- (Anthropic), sk- (OpenAI),
  xoxb- (Slack), nvd-api
  ```
- [ ] Specifically verify the **NVD API key** referenced in user memory is NOT in the repo (code, config, docs, commits, or `.env` files). It lives in external config only.
- [ ] Check `.gitignore` covers all common secret locations:
  ```
  .env, .env.*, *.key, *.pem, *.p12, credentials.json, secrets.yaml,
  .aws/, .ssh/, *.token
  ```
- [ ] If secrets were ever committed: **rewrite git history** with `git filter-repo` (not `filter-branch`, which is deprecated and error-prone). Force-push only after the rewrite is verified locally. **Rotate every exposed credential** regardless — assume it's leaked the moment it was ever pushed to a remote.

---

## 3. Personal / Machine-Specific Data

- [ ] Grep for hardcoded absolute paths that leak your machine layout:
  ```
  /home/ants, /home/<username>, /mnt/Storage, C:\Users\
  ```
  Known hits on the current tree:
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

> ⚠️ **CRITICAL LAUNCH BLOCKER** — current `assets/` directory has content that likely cannot be redistributed under MIT. Must be resolved before any public push.

### Known concerns (as of 2026-04-14)
- **`assets/textures/Texturelabs_*.jpg`** — from texturelabs.org. Their license permits free use but **explicitly forbids redistribution in other asset packs, tools, or repositories**. These **cannot** ship in an open-source repo. Options:
  1. Replace with CC0 equivalents (ambientCG, Poly Haven, CC0 Textures, OpenGameArt)
  2. Remove entirely and point sample scenes at downloaded-on-first-run assets
  3. Contact texturelabs.org and request explicit redistribution permission (unlikely to be granted)
- **`assets/models/CesiumMan.glb` / `Fox.glb` / `RiggedFigure.glb`** — glTF sample models from Khronos / Cesium, typically **CC-BY 4.0** (redistributable with attribution). Verify exact source and license, then record in `THIRD_PARTY_NOTICES.md`.
- **`assets/fonts/default.ttf`** — unknown source. Fonts are a notorious licensing trap: verify it's OFL / Apache / MIT licensed. If not, replace with a known-free font (DejaVu, Liberation, Noto, Inter).

### General audit
- [ ] Enumerate every asset in `assets/` and verify:
  - Source URL documented
  - License documented (CC0, CC-BY, OFL, bought-with-redistribution-rights, self-made, etc.)
  - License permits redistribution (not all "free" licenses do)
  - Attribution requirement captured in `THIRD_PARTY_NOTICES.md`
- [ ] Confirm assets from `/mnt/Storage/3D Engine Assets/` that are licensed for personal use only are **not** in the engine repo. Asset library references in code resolve via a config path, not a hardcoded `/mnt/...` string.
- [ ] Biblical project content (Tabernacle, Solomon's Temple models/textures) lives in **separate repos**. Confirm no leakage into the engine repo.
- [ ] Audio and fonts double-check: every `.ttf`, `.otf`, `.wav`, `.mp3`, `.ogg` has a verified-redistributable license.
- [ ] Create `ASSET_LICENSES.md` (or extend `THIRD_PARTY_NOTICES.md`) listing every shipped asset with source + license + attribution.

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

- [ ] Add `LICENSE` file at repo root with the chosen license text (verbatim from https://spdx.org/licenses/).
- [ ] Add copyright headers to source files:
  ```cpp
  // Copyright (c) 2026 <Your Name>
  // SPDX-License-Identifier: MIT
  ```
  (Batch-apply via a script. Use SPDX identifiers — tools understand them.)
- [ ] Add `README.md` with: what the engine is, current status ("early-stage, API unstable"), build instructions, minimum system requirements, quick-start, license line, contact/discussion channel.
- [ ] Add `CONTRIBUTING.md`: how to build from source, coding standards reference, testing requirements, PR expectations, DCO sign-off instruction if you chose DCO.
- [ ] Add `CODE_OF_CONDUCT.md` (Contributor Covenant is the de facto standard).
- [ ] Add `SECURITY.md` — you already have one; verify the disclosure contact is an address you're willing to publish.
- [ ] Add `.github/ISSUE_TEMPLATE/` with: bug report, feature request, security (redirect to SECURITY.md).
- [ ] Add `.github/PULL_REQUEST_TEMPLATE.md` with a checklist (tests added, audit tool clean, CHANGELOG updated).
- [ ] Add `NOTICE` or `THIRD_PARTY_NOTICES.md` for dependency attribution.

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

---

## 9. Git History Hygiene

- [ ] Review commit author names and emails across history:
  ```bash
  git log --format='%an <%ae>' | sort -u
  ```
  Confirm every identity is one you're comfortable publishing.
- [ ] Large binary files in history bloat the public repo. If you've committed and later deleted large assets, they're still in history. Consider:
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
- [ ] Archive or rotate any credentials that EVER lived in the repo, regardless of whether you scrubbed history.

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
