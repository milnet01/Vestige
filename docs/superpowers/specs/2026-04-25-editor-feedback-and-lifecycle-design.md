# Editor feedback loops & lifecycle — design spec

**Date:** 2026-04-25
**Phase target:** Phase 10.5 (Editor Usability Pass — "Feedback loops & lifecycle" subsection)
**Brainstormed via:** `superpowers:brainstorming` (Q1–Q17, six design sections)
**Status:** ready for implementation planning

## Scope

Three user-facing editor features that wire the engine to its own GitHub repo so users (developers using Vestige to build games) can report problems, suggest features, and pull fixes without leaving the tool. Plus a release-management policy that makes the auto-updater's `stable` and `nightly` channels meaningful.

1. **Bug reporter** (`Help → Report a Bug`) — submits issues to the engine's GitHub Issues with auto-attached diagnostics, deduplicates against existing issues, surfaces fixes for already-resolved bugs.
2. **Feature tracker** (`Help → Suggest a Feature`) — submits feature requests to GitHub Discussions Ideas, deduplicates against the bundled `ROADMAP.md` first.
3. **Auto-updater** (`Help → Check for Updates` + periodic toast) — pulls new releases from GitHub Releases, runs migrations and rebakes for breaking changes, rolls back on crash.

Plus shared infrastructure: HTTPS client, GitHub auth, release-manifest schema, project + config backup.

This spec is the single source of truth for the brainstormed design. The implementation plan derived from it lives in a separate plan document (produced by the next `superpowers:writing-plans` step).

## Decisions captured

The brainstorm posed seventeen pivots (Q1–Q17) and recorded the user's choice for each. The full list:

| ID | Question | Decision |
|---|---|---|
| Q1 | Bug-report destination | (a) GitHub Issues only — `milnet01/Vestige`. (b) configurable endpoint and (c) public/private dual-mode preserved as future-work bullets in the ROADMAP |
| Q2 | Bug-reporter auth model | (c) Both — anonymous-via-bot default with one-click "Sign in with GitHub for attribution" promotion |
| Q3 | Bug-report duplicate detection | (a) Deterministic keyword extraction + GitHub Search API native relevance ranking |
| Q4 | Privacy scrubbing depth | (c) Hybrid — whitelist for auto-attached fields + regex scrub for free-form fields. Preview-before-submit gate |
| Q5 | Bug reporter scope | (a) Bugs only. Feature requests get their own tracker (Q6 onward) |
| Q6 | Feature-tracker destination | (b) GitHub Discussions in the "Ideas" category |
| Q7 | Feature-tracker dedup mechanism | (a) Bundled-and-static `ROADMAP.md` parse + soft-block UX (user can submit anyway with override note). Network fallback (download from `raw.githubusercontent.com`) on missing-file with user prompt |
| Q8 | Bug-report diagnostic defaults | (a) Maximalist auto-attach with per-row opt-out |
| Q9 | Auto-updater binary host | (a) GitHub Releases |
| Q10 | Update payload structure | (a) Single archive replace |
| Q11 | Breaking-feature metadata | (a) CHANGELOG front-matter convention. Plus new requirement: per-feature migration step at install time (Q13) |
| Q12 | Project-side feature scan | (a) Component-type reflection at update-check time + `scope: universal/component/optional` tier |
| Q13 | Migration mechanism | (e) Hybrid — per-feature C++ migrations + auto-rebake fallback. Pre-install backup + warning + migration log |
| Q14 | Engine binary rollback | (b) Auto-rollback on crash + manual rollback for non-crash regressions |
| Q15 | Release channels | (b) Stable + nightly (no beta — soak in nightly does the job at 0.1.x scale) |
| Q16 | Update-check frequency | (b) Startup + every 24h, 4h hard floor; configurable |
| Q17 | Pre-1.0 release-management policy | (e) Phase boundary OR maintainer-soak (≥7 days on nightly), whichever fires first; pre-1.0 caveat shown on every install |

Additional refinements during section review:

- Section 3: ROADMAP-missing fallback prompt (download / continue without check / both with auto-tag).
- Section 4: pre-1.0 caveat placement.
- Section 5 addendum: settings preservation hard invariant — install never touches user-config or project paths; settings-schema migration is a runtime concern, not an updater concern.
- Section 6 addendum: pre-update settings warnings + post-relaunch `PostUpdateWelcomePanel` review surface; `affected_settings:` CHANGELOG metadata extension.

---

## 1. Overall architecture

Three independent user-facing surfaces sharing four pieces of new infrastructure.

### Three user-facing surfaces

- **`Help → Report a Bug`** — bug-reporter panel. Form, dedup against GitHub Issues Search, submit to `milnet01/Vestige` Issues.
- **`Help → Suggest a Feature`** — feature-tracker panel. Form, dedup against bundled `ROADMAP.md`, submit to `milnet01/Vestige` Discussions / Ideas.
- **`Help → Check for Updates`** plus a non-modal toast (startup + every 24h) — auto-updater. Reads GitHub Releases manifest, runs the install + migration + rebake pipeline, rolls back on crash.

### Four pieces of shared infrastructure

1. **`engine/net/http_client.{h,cpp}`** — minimal HTTPS client wrapping libcurl. Templated `Transport` for tests (`MockTransport` returns canned responses). Single client serves all three features.
2. **`engine/net/github_auth.{h,cpp}`** — device-flow OAuth + OS keyring (`libsecret` Linux, `Security.framework` Keychain macOS, `wincred.h` Credential Manager Windows). Anonymous-via-bot path: project-owned relay endpoint hosts a short-lived rotating PAT.
3. **`engine/lifecycle/release_manifest.{h,cpp}`** — pure-function schema + parser for `release-manifest.json` (version, channel, archive URL, SHA256, breaking-feature metadata).
4. **`engine/lifecycle/project_backup.{h,cpp}`** — pure-function project-tree snapshot helper. Uses `AtomicWrite::writeFile` (Slice 1 F7) for durability.

### Settings model

Settings → Network → "Allow editor to contact GitHub" is the master kill-switch (default off until first opt-in). With it off, every feature degrades to local-only modes:

- Bug reporter: panel still composes the bundle; offline-queues to `~/.config/vestige/pending-reports/<timestamp>.json`.
- Feature tracker: skips dedup, opens form, offline-queues submission.
- Auto-updater: silent; no checks, no toasts.

Sub-settings under the master toggle:

- Bug reporter auth mode (`anonymous` / `authenticated`).
- Update channel (`stable` / `nightly`).
- Update check frequency (`Always` / `Daily (24h)` / `On startup only` / `Never`).
- Telemetry-on-update-check (off by default).

### Privacy floor (consolidated)

1. **Master kill-switch** at the network layer.
2. **Per-call audit log** at `~/.config/vestige/network-log.json` — timestamp, method, host, path, status, bytes; never request/response bodies.
3. **No data exfiltration without preview** — bug-report and feature-request bundles are shown post-scrub before submit.
4. **No telemetry by default** — update-check sends only `User-Agent: Vestige/<version> (<os>)`.
5. **No third-party endpoints** — every call goes to GitHub APIs, `raw.githubusercontent.com`, or the project-owned relay endpoint.

---

## 2. Bug reporter

### Components

| Component | File | Pure / GL-free? | Responsibility |
|---|---|---|---|
| `BugReporterPanel` | `engine/editor/panels/bug_reporter_panel.{h,cpp}` | ImGui | Form UI, preview, submit |
| `DiagnosticCollector` | `engine/feedback/diagnostic_collector.{h,cpp}` | Mostly pure (one `glGetString` at init) | Builds the auto-attach `DiagnosticBundle` per Q4 whitelist + Q8 maximalist defaults |
| `DiagnosticScrubber` | `engine/feedback/diagnostic_scrubber.{h,cpp}` | **Pure** | Q4 regex-strip of paths / emails / IPs / MACs from free-form fields |
| `IssueDeduplicator` | `engine/feedback/issue_deduplicator.{h,cpp}` | **Pure** | Q3 keyword extraction; returns the GitHub Search query string |
| `IssueSubmitter` | `engine/feedback/issue_submitter.{h,cpp}` | Templated on `Transport` | Wraps the GitHub Issues REST calls |
| `OfflineQueue` | `engine/feedback/offline_queue.{h,cpp}` | **Pure** (`FileIo` mocked) | Persist unsent reports; drain on next online launch |
| `ResolvedFixLookup` | `engine/feedback/resolved_fix_lookup.{h,cpp}` | **Pure** | Given a closed-issue match, parse close-event for the linked PR/commit and the version it shipped in |

### Data flow (happy path)

1. User opens `Help → Report a Bug`.
2. Panel constructs `DiagnosticBundle` (versions, commit, OS, GL info, GPU, log tail post-scrub, screenshot-if-ticked, recent-actions ring buffer, scene name path-stripped — all auto-on per Q8 with per-row opt-out).
3. User types title + what-happened + expected + reproduction-steps; ticks/unticks per-row diagnostic toggles.
4. User clicks **Submit**.
5. `DiagnosticScrubber` runs on free-form fields; preview dialog shows the exact bytes ("These are the exact bytes we will send. Cancel / Confirm").
6. **Confirm** → `IssueDeduplicator` builds the keyword query → `IssueSubmitter` calls GitHub Search Issues API.
7. Top-5 matches displayed in "Has someone already reported this?" panel.
8. User picks one of:
   - **Add my details to #N** (open match) → POST as comment on the existing issue.
   - **This was fixed in v0.1.42 — update?** (closed match, user older) → `ResolvedFixLookup` found the fix commit → chain to auto-updater dialog.
   - **Submit as new issue** (no match OR user overrides) → POST new issue with the bundle.
9. Result: success toast with link, or offline-queued if network unreachable.

### Error handling

- **Network unreachable**: bundle written to `OfflineQueue`; submit retried on next online launch.
- **Rate limit**: each call returns `RateLimitInfo` from response headers; UI surfaces "rate-limited until <reset-time>; queued for later" if exhausted.
- **Auth failure**: prompts re-auth. Anonymous-via-bot path falls back to "open the issue in your browser instead" (URL-encoded form) if the relay endpoint is unreachable.
- **Malformed response from GitHub**: log and continue.
- **Editor crash mid-submission**: bundle persisted to offline queue before the network call; on next launch the queue flush retries it.

### Sub-decisions

- **Issue labels:** auto-tagged with `bug`, `from-editor`, `version-X.Y.Z`, and either `attribution-anonymous` or `attribution-<github-handle>`.
- **Issue template:** new file `.github/ISSUE_TEMPLATE/editor-reported-bug.md` so non-editor reporters using the GitHub UI follow the same shape.
- **Repro-scene attachment:** **deferred to a follow-up bullet** — bundling stripped scene files would help triage but introduces format / privacy / size questions out of scope for v1.

### Tests

- **Pure unit tests** (no transport, no real filesystem): `DiagnosticScrubber`, `IssueDeduplicator`, `OfflineQueue` (mock `FileIo`), `ResolvedFixLookup`.
- **`MockTransport` integration tests**: `IssueSubmitter` with hand-authored GitHub Search / Issue response fixtures covering no-match, single-open-match, single-closed-resolved-match, multi-match, rate-limited.
- **Manual UI checklist**: `BugReporterPanel` ImGui rendering.

---

## 3. Feature tracker

Smaller surface than the bug reporter — no diagnostic collection, no resolved-fix lookup, dedup is local instead of remote.

### Components

| Component | File | Pure / GL-free? | Responsibility |
|---|---|---|---|
| `FeatureTrackerPanel` | `engine/editor/panels/feature_tracker_panel.{h,cpp}` | ImGui | Form UI: title / what / why / who-benefits / proposed-approach. Soft-block dialog on match |
| `RoadmapItem` + `RoadmapIndex` | `engine/feedback/roadmap_index.{h,cpp}` | **Pure** | Struct for a parsed bullet; index = `std::vector<RoadmapItem>` with token map |
| `RoadmapParser` | `engine/feedback/roadmap_parser.{h,cpp}` | **Pure** | Parses `ROADMAP.md` text → `RoadmapIndex`. Detects bullets via `^- \[[\sx~]\] \*\*([^*]+)\*\* (.*)$`; nearest preceding `### ...` heading = section, nearest `## Phase ...` = phase |
| `RoadmapMatcher` | `engine/feedback/roadmap_matcher.{h,cpp}` | **Pure** | Reuses `IssueDeduplicator`'s keyword-extraction; returns top-N `RoadmapItem` matches with relevance score |
| `RoadmapFallbackFetcher` | `engine/feedback/roadmap_fallback_fetcher.{h,cpp}` | Templated on `Transport` | Fetches `ROADMAP.md` from `raw.githubusercontent.com` when bundled copy is missing; caches via `AtomicWrite` |
| `DiscussionSubmitter` | `engine/feedback/discussion_submitter.{h,cpp}` | Templated on `Transport` | POST to GitHub Discussions GraphQL API (Discussions has no REST surface) |

### Data flow

1. User opens `Help → Suggest a Feature`.
2. Panel loads `<install>/share/vestige/ROADMAP.md`.
3. **If file missing**, prompt:
   - "Local roadmap not found. Download the latest version from GitHub to check for similar feature requests? **[Download]** **[Continue without check]**"
   - **Download → success**: parsed in-place, cached to `~/.cache/vestige/fallback-roadmap.md`, dedup runs. Cache refreshed on next bundled-roadmap update; 7-day expiry.
   - **Download → failure**: warning toast; form stays open with submit enabled; submission auto-tagged `dedup-skipped: roadmap-unavailable` for maintainer-side triage.
   - **Continue without check**: form stays open; submission auto-tagged `dedup-skipped: user-declined`.
4. `RoadmapParser` builds index (one-shot at panel-open; <50 ms for 2000-line file).
5. User types title + description.
6. On title-edit (debounced 300 ms), `RoadmapMatcher` runs against the index → top-5 matches shown live.
7. User reviews:
   - **Match found, agrees this is the same** → "View on roadmap" deep-links into the in-editor docs browser at the bullet.
   - **Match found, wants to add use-case** → "Submit anyway with note" — soft-block dialog confirms; submission auto-tagged `roadmap-overlap: <Phase X.Y, line N>`.
   - **No match** → straight to submit.
8. Click **Submit** → `DiscussionSubmitter` creates a discussion in the "Ideas" category.
9. Result: success toast with link, or offline-queued.

### Discussion body template

```
## What
<user's "what" field>

## Why
<user's "why" field>

## Who benefits
<user's "who-benefits" field>

## Proposed approach
<user's "proposed-approach" field, optional>

---
Submitted from Vestige editor v<version> (<channel>).
Roadmap-overlap notes (if soft-block overridden): <Phase X.Y, line N>: <matched bullet title>
```

### Discussion labels

`from-editor`, `version-X.Y.Z`, `roadmap-overlap-<id>` (when override invoked), `dedup-skipped: roadmap-unavailable | user-declined` (when applicable).

### Tests

- **Pure unit tests**: `RoadmapParser` with fixtures covering each bullet shape (open / shipped / partial / no-ID), multi-line bodies, malformed lines; `RoadmapMatcher` with hand-authored user-suggestion / expected-top-match pairs; `RoadmapFallbackFetcher` for cache-hit / fetch-success / fetch-404 / network-error / cache-stale-then-refresh.
- **`MockTransport` integration tests**: `DiscussionSubmitter` against canned GraphQL responses for create-discussion, rate-limit, auth-failure, malformed.
- **Manual UI checklist**: panel form + soft-block dialog + missing-roadmap prompt.

---

## 4. Auto-updater

The largest of the three features — multi-stage flow with migration and rollback paths.

### Components

| Component | File | Pure / GL-free? | Responsibility |
|---|---|---|---|
| `UpdateChecker` | `engine/lifecycle/update_checker.{h,cpp}` | Templated on `Transport` | Periodic + on-demand check against GitHub Releases API |
| `ReleaseManifest` | `engine/lifecycle/release_manifest.{h,cpp}` | **Pure** | Schema + parser (Section 5 has the full schema) |
| `ChangelogParser` | `engine/lifecycle/changelog_parser.{h,cpp}` | **Pure** | Q11 CHANGELOG front-matter parser |
| `ProjectFeatureScanner` | `engine/lifecycle/project_feature_scanner.{h,cpp}` | **Pure** (takes `Scene*`/`Prefab*` refs) | Q12 component-type reflection |
| `BreakingChangeIntersector` | `engine/lifecycle/breaking_change_intersector.{h,cpp}` | **Pure** | `(scope=universal) OR (scope=component AND breakingFeatures ∩ projectFeatures ≠ ∅)` |
| `MigrationRegistry` | `engine/lifecycle/migration_registry.{h,cpp}` | **Pure** | Per-feature migration entries; runs at install time |
| `ProjectBackup` | `engine/lifecycle/project_backup.{h,cpp}` | Mostly pure | Pre-migration project-tree snapshot |
| `BinaryDownloader` | `engine/lifecycle/binary_downloader.{h,cpp}` | Templated on `Transport` | Streams archive, verifies SHA256 |
| `BinarySwapper` | `engine/lifecycle/binary_swapper.{h,cpp}` | OS-specific | Atomic install-tree swap (Linux/macOS rename; Windows launcher process) |
| `RollbackController` | `engine/lifecycle/rollback_controller.{h,cpp}` | Mostly pure | Auto-rollback on crash + manual rollback |
| `AutoRebakeOrchestrator` | `engine/lifecycle/auto_rebake_orchestrator.{h,cpp}` | Pure planning + invokes existing bake systems | Q13 fallback for derived data |
| `UpdatePromptToast` | `engine/editor/panels/update_prompt_toast.{h,cpp}` | ImGui | Non-modal "update available" toast |
| `UpdateDialog` | `engine/editor/panels/update_dialog.{h,cpp}` | ImGui | Full update dialog: changelog + warnings + install buttons |
| `PostUpdateWelcomePanel` | `engine/editor/panels/post_update_welcome_panel.{h,cpp}` | ImGui | Section 6 addendum: post-relaunch review |
| `PostUpdateReviewState` | `engine/lifecycle/post_update_review_state.{h,cpp}` | **Pure** (`FileIo` mocked) | Read/write `~/.config/vestige/post-update-review.json` |
| `MigrationLogReader` | `engine/lifecycle/migration_log_reader.{h,cpp}` | **Pure** | Parses `<project>/.vestige/migration-history.json` for the welcome panel |

### Data flow — happy path

1. Editor startup. `UpdateChecker` runs (background thread), respecting Q16 timer (24 h since last check, or first check on startup).
2. Fetches `release-manifest.json` from GitHub Releases API for the user's current channel (Q15).
3. If `manifest.version > running VERSION`:
   - `UpdatePromptToast` appears: "Vestige <new-v> is available — release notes / Install / Later".
4. User clicks **release notes** or **Install** → `UpdateDialog` opens.
5. Dialog renders:
   - CHANGELOG diff between running VERSION and `manifest.version` via existing markdown renderer.
   - `ChangelogParser` extracts metadata; `ProjectFeatureScanner` walks the open project; `BreakingChangeIntersector` computes warnings.
   - **Three-tier warning panel** (Section 6 addendum):
     - "Project features your project uses that will be migrated automatically" (with auto-migration description per feature).
     - "Settings to double-check after install" (with current value + "preserved as X" note).
     - "Files in your install directory will be replaced (binary, shaders, ROADMAP). Settings and projects will NOT be touched."
   - Buttons: **Backup-then-install** (default) / **Install now** / **Cancel**.
6. User clicks **Backup-then-install**:
   - `ProjectBackup` snapshots project tree to `~/.cache/vestige/project-backups/<project-id>/<from-v>-to-<to-v>/`.
   - User-config tree backed up to `~/.cache/vestige/config-backups/<from-v>-to-<to-v>/`.
   - `BinaryDownloader` streams archive to `~/.cache/vestige/updates/<v>/`. Progress shown.
   - SHA256 verified against manifest.
   - `BinarySwapper`:
     - Linux/macOS: rename staging → install root atomically.
     - Windows: launch `vestige-updater.exe`, exit editor, updater swaps + restarts.
7. Editor restarts on new binary.
8. `MigrationRegistry` runs all entries matching the version transition × project's used features. Each migration logged to `<project>/.vestige/migration-history.json`.
9. Settings-schema migrations run on `Settings::loadFromDisk` (Section 5 addendum).
10. `AutoRebakeOrchestrator` marks any derived data stale; rebakes trigger lazily on next project load.
11. `PostUpdateReviewState::shouldShowFor(<new-v>)` returns true → `PostUpdateWelcomePanel` opens (modal but dismissible). Renders changelog + migrations performed + settings to double-check + stale data needing rebake. User dismisses with "Don't show this again for v<new-v>" ticked.
12. Startup-success file `~/.cache/vestige/startup-success-v<new-v>` written after first frame → next launch knows the install was OK.

### Migration mechanism (Q13 hybrid)

`MigrationRegistry::registerMigration({feature_id, from_pred, to_pred, fn})` called at engine init. Each `fn` receives `MigrationContext { project, fromVersion, toVersion, log }` and returns `MigrationResult`:

- `Success` — silent log entry; project saved post-migration.
- `Failed{reason}` — warning shown, project NOT saved; user told to restore from backup.
- `NeedsManualAction{guideUrl}` — surfaces the URL; project NOT saved.
- `NeedsRebake{feature}` — `AutoRebakeOrchestrator` marks stale; rebake triggers next time the feature is used.

Example (illustrative):

```cpp
// R7: SH coefficients now stored as radiance-SH instead of irradiance-SH.
// Pre-fix data was scaled by π too many times; rescale by 1/π on migration.
MigrationRegistry::registerMigration({
    .feature_id = "sh_probe_grid",
    .from = VersionPredicate::lessThan("0.1.31"),
    .to   = VersionPredicate::greaterOrEqual("0.1.31"),
    .fn   = [](MigrationContext& ctx) -> MigrationResult {
        for (auto* probeGrid : ctx.project.findAll<SHProbeGrid>())
            probeGrid->rescaleAllProbes(1.0f / glm::pi<float>());
        return MigrationResult::Success{};
    }
});
```

### Rollback flow (Q14)

**Auto-rollback on crash:**

1. New version v0.1.42 launches.
2. After first frame is rendered, write `~/.cache/vestige/startup-success-v0.1.42`.
3. Any subsequent launch checks for `~/.cache/vestige/startup-success-<currently-installed-v>`.
4. If missing → most-recent install never completed a frame. `RollbackController` restores `~/.cache/vestige/binary-backups/<previous-v>/` to install root. User sees a notification with a "Report" button pre-filling the bug reporter with the failed-version diagnostics.
5. If present → normal startup.

**Manual rollback** (`Help → Restore Previous Version`):

Lists `~/.cache/vestige/binary-backups/` entries. User picks one. Same swap path. Project-data backups are not auto-restored on binary rollback (`Help → Restore Project Backup` is a separate user-invoked action).

### Pre-1.0 caveat

Shown on every install regardless of channel:

> **Pre-1.0 release.** This is an alpha/beta build. Updates between minor versions may include breaking changes; the editor will attempt to migrate your project but always backs up first.

### Sub-decisions

- **Channels (Q15):** `stable` / `nightly`, with explicit "I want to test pre-release builds" opt-in dialog when switching. Per-project pin via `<project>/.vestige/update-channel.json` overrides editor-wide setting.
- **Frequency (Q16):** check on startup + every 24 h, configurable; 4 h hard floor between actual API calls regardless of timer.
- **Telemetry:** opt-in only; default User-Agent only.

### Tests

- **Pure unit tests**: `ChangelogParser`, `ProjectFeatureScanner`, `BreakingChangeIntersector`, per-migration entries with synthetic before/after `Project` fixtures, `ReleaseManifest` parser, `RollbackController` (mock `FileIo`), `PostUpdateReviewState`, `MigrationLogReader`.
- **`MockTransport` integration**: `UpdateChecker`, `BinaryDownloader`, `BinarySwapper`.
- **Manual UI checklist**: `UpdateDialog`, `UpdatePromptToast`, `PostUpdateWelcomePanel`.

---

## 5. Shared infrastructure + release management

### `HttpClient` (libcurl wrapper)

- Backend: libcurl. If not already linked transitively, vendor via `FetchContent_Declare(curl ...)`. Configure TLS-only, TLS 1.2 minimum, cert pin to GitHub root CA bundle shipped at `share/vestige/certs/github-roots.pem`.
- Synchronous interface; long-running requests on the existing async-task pool.
- Templated `Transport` for tests.
- Network-log writer per call.

### `GitHubAuth`

- Device-flow OAuth: `POST https://github.com/login/device/code` → poll `https://github.com/login/oauth/access_token` until token. Persist to OS keyring via `libsecret`/Keychain/`wincred.h` shim.
- Anonymous-via-bot: editor fetches short-lived rotating PAT from project-owned relay endpoint (Cloudflare Worker, free tier, code in `tools/relay/`). Worker holds the long-lived PAT server-side; rotates daily; rate-limits per-IP.
- Fallback when relay unreachable: "open the issue in your browser instead" URL-encoded form.

### `ReleaseManifest` schema

```json
{
  "schema_version": 1,
  "release": {
    "version": "0.2.0",
    "channel": "stable",
    "tag": "v0.2.0-stable",
    "published_at": "2026-05-15T12:00:00Z",
    "soak_days": 7,
    "phase_marker": "Phase 10.9 complete"
  },
  "platforms": {
    "linux-x86_64": { "archive_url": "https://...", "sha256": "...", "size_bytes": 87654321 },
    "windows-x86_64": { "archive_url": "https://...", "sha256": "...", "size_bytes": 92345678 },
    "macos-arm64":   { "archive_url": "https://...", "sha256": "...", "size_bytes": 88123456 }
  },
  "changelog_md_url": "https://github.com/milnet01/Vestige/releases/download/v0.2.0-stable/CHANGELOG.md",
  "breaking_features": [
    { "feature_id": "sh_probe_grid", "scope": "universal", "severity": "behavior-change", "changelog_anchor": "#2026-04-25-r7" },
    { "feature_id": "audio_bus_naming", "scope": "component", "severity": "breaking", "changelog_anchor": "#2026-05-10-..." }
  ]
}
```

### `ProjectBackup`

- Walks the project root, copies `<project>/scenes/`, `<project>/assets/`, `<project>/.vestige/` to `~/.cache/vestige/project-backups/<project-id>/<from-v>-to-<to-v>/`. Skips `build/`, `.cache/`.
- Per-file SHA256 manifest written alongside.
- Backups via `AtomicWrite::writeFile` (Slice 1 F7).
- Backup-list-and-prune: keep N most recent (default 3, configurable).

### Release management policy (Q17)

**Versioning convention:**

- **Patch bumps** (0.1.35 → 0.1.36 → ...) — every CI-green commit on main. Nightly-channel releases. CI auto-tags `vX.Y.Z-nightly+<short-sha>`.
- **Minor bumps** (0.1.X → 0.2.0 → 0.3.0) — phase / milestone completions OR maintainer-soak promotion. Hand-tagged `vX.Y.0-stable`. Stable-channel releases.
- **Major bumps** (0.X.Y → 1.0.0) — once. Maintainer judgment when "feature-complete enough to ship a game without expecting breaking changes."

**Channel sources:**

- **Nightly**: every `*-nightly+<sha>` tag.
- **Stable**: every `*-stable` tag.

**Promotion criteria for stable:**

1. *Phase boundary auto-promote*: when a phase / major slice lands its final doc commit, the next CI-green commit on main is tagged stable.
2. *Soak promote*: a nightly that's been on the channel ≥ 7 days with no P0/P1 regression reports can be tagged stable, without a phase boundary. Used for hotfixes between phases.

**Pre-1.0 caveat** displayed on every install regardless of channel.

**1.0 trigger**: deferred decision; not codified in this spec. Likely tied to first biblical-walkthrough title shipping.

### CI workflows

Three new GitHub Actions:

1. **`.github/workflows/publish-nightly.yml`** — triggers on every push to `main` after the existing CI matrix passes. Builds platform archives, uploads to a `vX.Y.Z-nightly+<sha>` GitHub Release, generates `release-manifest.json` with `channel: nightly`.
2. **`.github/workflows/publish-stable.yml`** — manual `workflow_dispatch` with input `tag: vX.Y.0-stable`. Builds platform archives, uploads to `vX.Y.0-stable` Release, generates manifest with `channel: stable`, copies CHANGELOG.md slice between previous stable and this stable into release body.
3. **`.github/workflows/manifest-validation.yml`** — runs on every PR. Validates that CHANGELOG entries between latest stable and HEAD have `affected_features:` / `severity:` / `scope:` / `affected_settings:` metadata where they should. Heuristic: any entry matching `breaking|migration|behavior change` keywords needs the metadata; missing it → warning, not failure.

### Settings preservation hard invariant (Section 5 addendum)

Three storage zones with strict rules:

| Zone | Location | Touched by updater? | Backed up? |
|---|---|---|---|
| **Install tree** | `~/.local/share/vestige/` (Linux), `~/Applications/Vestige.app/` (macOS), `%PROGRAMFILES%\Vestige\` (Windows) | **YES — fully replaced** | No (it's the binary) |
| **User settings** | `~/.config/vestige/` (Linux), `~/Library/Application Support/Vestige/` (macOS), `%APPDATA%\Vestige\` (Windows) | **NEVER touched** | Yes — `~/.cache/vestige/config-backups/<from-v>-to-<to-v>/` |
| **Project settings** | `<project>/.vestige/` | **NEVER touched** | Yes — Section 4 ProjectBackup |

**Settings-schema migration is a runtime concern**, not an updater concern. On every editor launch, `Settings::loadFromDisk()` checks `schema_version`; runs registered settings-migrations if older. Pre-migration value preserved in the config-backup. If migration fails, shipped defaults are used for the changed keys; non-fatal toast.

**Hard-coded path-isolation enforcement**: `BinarySwapper` asserts at construction that `installRoot` is not a parent / sibling / descendant of any user-config path or any open project's root. Unit-tested.

**Convention for shipped default files**: don't hand-edit them. Shipped files in the install tree are owned by the binary and replaced by every update. Custom themes save to `~/.config/vestige/themes/<name>.json` — user-zone, never touched.

---

## 6. Cross-cutting concerns

### Accessibility

Primary user is partially-sighted; preserve everything Phase 10.7 + Slice 3 S4 / S9 established:

- **Keyboard-only navigation** — every panel reaches every interactive element via Tab; Enter activates focused buttons; Escape cancels. Screen-reader labels via `UIAccessibleRole`.
- **Scale presets** — panels respect `UIScalePreset` and `UITheme::withScale`; no pixel-locked layouts.
- **High-contrast register** — `theme.text` / `theme.accent` / `theme.panelStroke`; no hard-coded colours.
- **Reduced-motion register** — toast slide / fade animations respect `Settings → Accessibility → Reduced motion`.
- **Auto-rollback notification** — readable by screen-reader bridge via `UIAccessibleRole`.

### Pre-update settings warnings + post-relaunch review (Section 6 addendum)

**CHANGELOG metadata extended with `affected_settings:` and `notes:`:**

```
### 2026-05-15 v0.2.0 — Phase 10.9 complete (stable)
affected_features: [sh_probe_grid, audio_bus_naming]
affected_settings: [accessibility.reducedMotion, audio.master.bus]
severity: behavior-change
scope: universal
notes: |
  Reduced-motion default flipped from off to on for new installs.
  Audio master bus default is now -3 dB (was 0 dB) for headroom.
```

**Pre-install warning panel** gains a settings tier (silent if `affected_settings` is empty).

**`PostUpdateWelcomePanel`** surfaces on first launch after install. Modal, dismissible. Layout (Section 6 addendum has the full mockup):

- Header: "Welcome back to Vestige v0.2.0"
- Section 1: rendered changelog (scrollable).
- Section 2: project features migrated (with auto-fix description per feature).
- Section 3: settings to double-check (with current value + "preserved as X" + Open Settings deep-link).
- Section 4: stale data needing rebake.
- Footer: "Don't show this again for v0.2.0" (default ticked) + "Got it".

**Persistence**: `~/.config/vestige/post-update-review.json`:

```json
{
  "shown_for_version": "0.2.0",
  "dismissed": true,
  "dismissed_at": "2026-05-16T09:14:22Z"
}
```

Resurfaces if `current_version > shown_for_version`.

**Auto-rollback case**: rollback notification overrides the welcome panel; no welcome panel for the version that didn't survive its first frame.

**Settings-schema migration interaction**: migration toasts are suppressed until after the welcome panel is dismissed.

### Error-handling consolidation

Three failure classes appear across all features. Standardised:

- **Network unreachable**: non-modal toast; persist payload to `OfflineQueue` (bug reporter, feature tracker) or skip silently (update checker).
- **Rate limit**: `RateLimitInfo` parsed from response headers; UI surfaces "rate-limited until <reset-time>; queued for later".
- **Auth failure**: bug reporter / feature tracker prompt re-auth; update checker falls back to anonymous mode for manifest fetch.

### Testing strategy summary

Same shape as the rest of Phase 10.9: pure helpers tested without GL or network; integration paths mocked at the boundary.

- **Pure helpers**: `DiagnosticScrubber`, `IssueDeduplicator`, `RoadmapParser`, `RoadmapMatcher`, `ChangelogParser`, `ProjectFeatureScanner`, `BreakingChangeIntersector`, `ReleaseManifest` parser, `OfflineQueue` (mock `FileIo`), `RollbackController`, per-feature migration entries, `PostUpdateReviewState`, `MigrationLogReader`.
- **`MockTransport` integration tests**: `IssueSubmitter`, `DiscussionSubmitter`, `RoadmapFallbackFetcher`, `UpdateChecker`, `BinaryDownloader`, `BinarySwapper` (with mocked `FileIo`).
- **Real-network integration tests** (gated behind `VESTIGE_INTEGRATION_TESTS=1`; nightly-CI only): exercise actual GitHub API calls against a sandbox repo.
- **Manual UI checklist**: panels not covered by unit tests.

Test-count estimate: ~80–120 new unit tests across the three features. Suite stays under 5 s.

### Rollout order

The three features have one dependency edge: the auto-updater's "this was fixed in v0.1.42 — update?" chain from the bug reporter depends on the auto-updater existing. Otherwise independent.

Suggested ship order:

1. **Shared infrastructure** (Section 1: `HttpClient`, `GitHubAuth`, `ReleaseManifest`, `ProjectBackup`).
2. **Auto-updater** (pure-CPU pieces first: manifest parsing, changelog parsing, breaking-change intersection; then download/swap/rollback; then migration registry + auto-rebake; then `PostUpdateWelcomePanel`).
3. **CI workflows** (nightly + stable + manifest-validation).
4. **Bug reporter** (panel + diagnostic collector + scrubber; deduplicator + issue submitter; offline queue + resolved-fix lookup).
5. **Feature tracker** (panel + roadmap parser + matcher + discussion submitter + roadmap fallback fetch).

Total: ~9 slices spread across however many calendar days they take. TDD red-green-doc cadence as usual.

**Pre-1.0 caveat on rollout**: bug reporter could ship before auto-updater if needed; the resolved-fix-update-chain is the only cross-feature dependency and can stub to "update available; check Help → Check for Updates" until the auto-updater lands.

### Pre-implementation verifications

Open facts to check before writing code:

- Does Vestige already link libcurl transitively (via Jolt / glfw / etc)? If yes, `find_package(CURL REQUIRED)` instead of FetchContent.
- Cloudflare Worker free-tier limits (100k req/day) sufficient for relay endpoint at projected user volumes?
- GitHub Discussions GraphQL API quirks — verify `createDiscussion` mutation shape against real API before committing test fixtures.
- Per-OS keyring shim availability — `libsecret` ubiquity on Linux (KDE/GNOME/sway), Keychain on macOS (always), `wincred.h` (always Win10+).
- Atomic install-tree swap on Windows under UAC-elevated installs — confirm launcher-process pattern, or move to per-user `%LOCALAPPDATA%\Vestige\` install path.

### Things explicitly **not** in this design (deferred)

- Configurable submission endpoint for studios (Q1 option b) — future-work bullet.
- Public/private dual bug-report mode (Q1 option c) — future-work bullet.
- Beta channel (Q15 option c, Q17 option d) — soak in nightly does the same job for now.
- Embedding-based bug-dedup (Q3 option c) — future-work bullet, gated on issue volume.
- Repro-scene attachment to bug reports — future-work bullet.
- Delta updates / per-component updates (Q10 options b/c) — future-work bullet.
- Side-by-side versioned install (Q14 option c) — future-work bullet.

---

## Spec self-review

Performed inline before user review:

- **Placeholder scan**: no "TBD" / "TODO" / vague requirements. Decisions either resolved (Q1–Q17) or explicitly deferred to future-work.
- **Internal consistency**: Section 1's "four pieces of shared infrastructure" matches Section 5's expanded definitions. Section 4's `BinarySwapper` matches Section 5's path-isolation enforcement. Section 6 addendum's `affected_settings:` matches Section 5's release-manifest schema (manifest → CHANGELOG metadata is the data flow).
- **Scope check**: three features + shared infra + release policy. Substantial but focused. The rollout-order section acknowledges the size and proposes a 9-slice path. Each feature is independently testable and shippable.
- **Ambiguity check**: each component has a file path and a "Pure / GL-free?" classification — every test contract is locatable. The CHANGELOG metadata schema is stated literally with example. The release manifest schema is stated literally with example. Migration entries have a worked code example.

No fixes needed.

---

## Next steps

1. User reviews this spec at `docs/superpowers/specs/2026-04-25-editor-feedback-and-lifecycle-design.md`.
2. After approval, the `superpowers:writing-plans` skill produces the detailed implementation plan keyed to this design.
3. The first plan item is the shared-infrastructure slice (Section 1's four pieces); subsequent slices follow the rollout order in Section 6.
