# Phase 10.5 — Auto-Updater Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Periodic + on-demand check against GitHub Releases; pull update, run breaking-change intersection, snapshot project, run migrations, swap binary atomically, auto-rollback on crash, surface a post-relaunch welcome panel.

**Architecture:** Pure-CPU pieces first (manifest parsing, changelog parsing, breaking-change intersection, migration registry, post-update review state) — all unit-tested without GL or network. Then download/swap/rollback wrapped over the platform `BinarySwapper` (Linux/macOS rename; Windows launcher). Finally ImGui surfaces (`UpdatePromptToast`, `UpdateDialog`, `PostUpdateWelcomePanel`) wire to the components.

**Tech Stack:** C++17, libcurl (via `HttpClient` from shared infra plan), nlohmann::json, ImGui (existing), `engine/utils/atomic_write.h`. Per-platform binary swap via `rename(2)` (Linux/macOS) / launcher exe (Windows).

**Source spec:** `docs/superpowers/specs/2026-04-25-editor-feedback-and-lifecycle-design.md` Sections 4, 5 (release manifest), 6 (post-update review).

**Depends on:** `docs/superpowers/plans/2026-04-25-phase-10-5-shared-infrastructure-plan.md` (HttpClient, ReleaseManifest, ProjectBackup).

---

## File structure

| New file | Pure / GL-free? | Tests |
|---|---|---|
| `engine/lifecycle/changelog_parser.{h,cpp}` | Pure | `tests/test_changelog_parser.cpp` |
| `engine/lifecycle/project_feature_scanner.{h,cpp}` | Pure (Scene*/Prefab* refs) | `tests/test_project_feature_scanner.cpp` |
| `engine/lifecycle/breaking_change_intersector.{h,cpp}` | Pure | `tests/test_breaking_change_intersector.cpp` |
| `engine/lifecycle/migration_registry.{h,cpp}` | Pure | `tests/test_migration_registry.cpp` |
| `engine/lifecycle/auto_rebake_orchestrator.{h,cpp}` | Pure planning + invokes existing bake systems | `tests/test_auto_rebake_orchestrator.cpp` |
| `engine/lifecycle/update_checker.{h,cpp}` | Templated `Transport` | `tests/test_update_checker.cpp` |
| `engine/lifecycle/binary_downloader.{h,cpp}` | Templated `Transport` + `FileIo` | `tests/test_binary_downloader.cpp` |
| `engine/lifecycle/binary_swapper.{h,cpp}` | OS-specific impl + `FileIo`-mocked tests | `tests/test_binary_swapper.cpp` |
| `engine/lifecycle/rollback_controller.{h,cpp}` | Mostly pure (`FileIo` mocked) | `tests/test_rollback_controller.cpp` |
| `engine/lifecycle/post_update_review_state.{h,cpp}` | Pure (`FileIo` mocked) | `tests/test_post_update_review_state.cpp` |
| `engine/lifecycle/migration_log_reader.{h,cpp}` | Pure | `tests/test_migration_log_reader.cpp` |
| `engine/editor/panels/update_prompt_toast.{h,cpp}` | ImGui | manual checklist |
| `engine/editor/panels/update_dialog.{h,cpp}` | ImGui | manual checklist |
| `engine/editor/panels/post_update_welcome_panel.{h,cpp}` | ImGui | manual checklist |
| Modify `engine/editor/editor.{h,cpp}` | Wire panels into `Editor::drawPanels` | (compile check) |
| Modify `engine/core/settings.{h,cpp}` | Settings keys for channel + check frequency | (existing tests) |

---

## Task 1: ChangelogParser — extracts release-block metadata

Goal: parse a `## [Unreleased] / ### YYYY-MM-DD ...` markdown structure plus the front-matter convention defined in spec Section 6 (`affected_features:`, `affected_settings:`, `severity:`, `scope:`, `notes:`).

**Files:**
- Create: `engine/lifecycle/changelog_parser.{h,cpp}`
- Test: `tests/test_changelog_parser.cpp`

- [ ] **Step 1: Write the failing test.**

```cpp
// tests/test_changelog_parser.cpp
#include <gtest/gtest.h>
#include "lifecycle/changelog_parser.h"

namespace Vestige::Lifecycle::Test
{
constexpr const char* kSample = R"(
## [Unreleased]

### 2026-05-15 v0.2.0 — Phase 10.9 complete (stable)
affected_features: [sh_probe_grid, audio_bus_naming]
affected_settings: [accessibility.reducedMotion, audio.master.bus]
severity: behavior-change
scope: universal
notes: |
  Reduced-motion default flipped from off to on for new installs.

Body of the entry goes here.
Multi-line content allowed.

### 2026-05-10 v0.1.99 (nightly)
affected_features: [audio_bus_naming]
severity: breaking
scope: component

Other entry.
)";

TEST(ChangelogParser, ExtractsAllEntries_PHASE105)
{
    auto entries = parseChangelog(kSample);
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].date, "2026-05-15");
    EXPECT_EQ(entries[0].version, "0.2.0");
    EXPECT_EQ(entries[0].channel, "stable");
    EXPECT_EQ(entries[0].title, "Phase 10.9 complete");
    ASSERT_EQ(entries[0].affectedFeatures.size(), 2u);
    EXPECT_EQ(entries[0].affectedFeatures[0], "sh_probe_grid");
    ASSERT_EQ(entries[0].affectedSettings.size(), 2u);
    EXPECT_EQ(entries[0].affectedSettings[1], "audio.master.bus");
    EXPECT_EQ(entries[0].severity, ChangelogSeverity::BehaviorChange);
    EXPECT_EQ(entries[0].scope, ChangelogScope::Universal);
    EXPECT_NE(entries[0].notes.find("Reduced-motion default flipped"), std::string::npos);
}

TEST(ChangelogParser, EntryWithoutFrontMatterParses_PHASE105)
{
    constexpr const char* kPlain = R"(
### 2026-04-25 Phase 10.9 — Slice 8 W11 (GpuCuller delete)

Closes the last open zombie of Slice 8.
)";
    auto entries = parseChangelog(kPlain);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_TRUE(entries[0].affectedFeatures.empty());
    EXPECT_TRUE(entries[0].affectedSettings.empty());
    EXPECT_EQ(entries[0].severity, ChangelogSeverity::None);
}

TEST(ChangelogParser, FilterEntriesBetweenVersions_PHASE105)
{
    auto entries = parseChangelog(kSample);
    auto diff = filterEntriesBetween(entries, "0.1.99", "0.2.0");
    ASSERT_EQ(diff.size(), 1u);
    EXPECT_EQ(diff[0].version, "0.2.0");
}
}
```

- [ ] **Step 2: Header.**

```cpp
// engine/lifecycle/changelog_parser.h
#pragma once
#include <string>
#include <vector>

namespace Vestige::Lifecycle
{
enum class ChangelogSeverity { None, BehaviorChange, Breaking };
enum class ChangelogScope { Optional, Component, Universal };

struct ChangelogEntry
{
    std::string date;       // "2026-05-15"
    std::string version;    // "0.2.0" (empty if none on header)
    std::string channel;    // "stable" / "nightly" / ""
    std::string title;
    std::vector<std::string> affectedFeatures;
    std::vector<std::string> affectedSettings;
    ChangelogSeverity severity = ChangelogSeverity::None;
    ChangelogScope scope = ChangelogScope::Optional;
    std::string notes;
    std::string body;       // everything after the front-matter block
};

std::vector<ChangelogEntry> parseChangelog(const std::string& md);

/// @brief Returns entries with `version > fromVersion AND version <= toVersion`.
std::vector<ChangelogEntry> filterEntriesBetween(
    const std::vector<ChangelogEntry>& entries,
    const std::string& fromVersion,
    const std::string& toVersion);

/// @brief Semver-ish comparison (`0.1.41` < `0.1.42` < `0.2.0` < `1.0.0`).
int compareVersions(const std::string& a, const std::string& b);
}
```

- [ ] **Step 3: Implement.** Pure parser: scan for `^### `, extract date/version/channel from header line; consume key:value lines until first blank; concatenate the rest as body.

- [ ] **Step 4: Build + test + commit.**

```bash
cmake --build build -j8 && ctest --test-dir build -R Changelog --output-on-failure
git add engine/lifecycle/changelog_parser.h engine/lifecycle/changelog_parser.cpp tests/test_changelog_parser.cpp engine/CMakeLists.txt tests/CMakeLists.txt
git commit -m "Phase 10.5 Slice 2 T1: ChangelogParser"
```

---

## Task 2: ProjectFeatureScanner — Q12 component-type reflection

Goal: walks open `Scene*`, returns `std::set<std::string>` of feature_ids the project uses. The mapping `ComponentType → feature_id` is a static table (`audio_source` → `"audio_bus_naming"`, `light_probe` → `"sh_probe_grid"`, etc.).

**Files:**
- Create: `engine/lifecycle/project_feature_scanner.{h,cpp}`
- Test: `tests/test_project_feature_scanner.cpp`

- [ ] **Step 1: Define the static mapping.** Edit the spec's table into `engine/lifecycle/feature_id_map.cpp` as a `static const std::map`.

- [ ] **Step 2: Failing test.**

```cpp
TEST(ProjectFeatureScanner, ReportsAudioBusNaming_WhenSceneHasAudioSource_PHASE105)
{
    Scene scene;
    auto e = scene.createEntity();
    e.addComponent<AudioSourceComponent>();

    auto features = scanProjectFeatures(scene);
    EXPECT_TRUE(features.count("audio_bus_naming"));
}

TEST(ProjectFeatureScanner, ReportsShProbeGrid_WhenSceneHasLightProbe_PHASE105) { /* similar */ }

TEST(ProjectFeatureScanner, EmptySceneReportsNoFeatures_PHASE105)
{
    Scene scene;
    auto features = scanProjectFeatures(scene);
    EXPECT_TRUE(features.empty());
}
```

- [ ] **Step 3: Implement.** Iterate components by type id; look up in `feature_id_map`. Add a "universal" feature_id list (always present, e.g. `"core.engine"`).

- [ ] **Step 4: Build + commit.**

```bash
git commit -m "Phase 10.5 Slice 2 T2: ProjectFeatureScanner"
```

---

## Task 3: BreakingChangeIntersector

Goal: pure boolean logic. `(scope == Universal) OR (scope == Component AND breakingFeatures ∩ projectFeatures ≠ ∅)`.

**Files:**
- Create: `engine/lifecycle/breaking_change_intersector.{h,cpp}`
- Test: `tests/test_breaking_change_intersector.cpp`

- [ ] **Step 1: Failing tests cover all three scope tiers.**

```cpp
TEST(BreakingChangeIntersector, UniversalScopeAlwaysTriggers_PHASE105)
{
    BreakingFeature f{"x", BreakingFeatureScope::Universal, BreakingFeatureSeverity::Breaking, ""};
    EXPECT_TRUE(featureAffectsProject(f, /*projectFeatures=*/{}));
}

TEST(BreakingChangeIntersector, ComponentScopeTriggersOnlyIfFeatureUsed_PHASE105)
{
    BreakingFeature f{"audio_bus_naming", BreakingFeatureScope::Component,
                      BreakingFeatureSeverity::Breaking, ""};
    EXPECT_FALSE(featureAffectsProject(f, /*projectFeatures=*/{}));
    EXPECT_TRUE (featureAffectsProject(f, /*projectFeatures=*/{"audio_bus_naming"}));
}

TEST(BreakingChangeIntersector, OptionalScopeNeverTriggers_PHASE105)
{
    BreakingFeature f{"x", BreakingFeatureScope::Optional, BreakingFeatureSeverity::Breaking, ""};
    EXPECT_FALSE(featureAffectsProject(f, {"x"}));
}
```

- [ ] **Step 2: Implement + commit.**

```bash
git commit -m "Phase 10.5 Slice 2 T3: BreakingChangeIntersector"
```

---

## Task 4: MigrationRegistry — per-feature migration entries

Goal: registry of migrations keyed on `(feature_id, fromPredicate, toPredicate)`. `runMigrations(ctx)` returns aggregated `MigrationResult`.

**Files:**
- Create: `engine/lifecycle/migration_registry.{h,cpp}`
- Test: `tests/test_migration_registry.cpp`

- [ ] **Step 1: Failing test for happy path + needs-rebake path.**

```cpp
TEST(MigrationRegistry, RunsMigrationsMatchingVersionPredicate_PHASE105)
{
    MigrationRegistry reg;
    int called = 0;
    reg.registerMigration({
        .featureId = "test_feature",
        .from = VersionPredicate::lessThan("0.1.31"),
        .to   = VersionPredicate::greaterOrEqual("0.1.31"),
        .fn   = [&called](MigrationContext&) { called++; return MigrationResult::Success{}; }
    });

    MigrationContext ctx{/*project=*/nullptr, /*from=*/"0.1.30", /*to=*/"0.1.31", /*log=*/{}};
    auto out = reg.runMigrations(ctx, {"test_feature"});

    EXPECT_EQ(called, 1);
    EXPECT_EQ(out.successes.size(), 1u);
}

TEST(MigrationRegistry, SkipsMigrationsForUnusedFeatures_PHASE105) { /* feature not in project */ }
TEST(MigrationRegistry, RecordsFailureWithoutSavingProject_PHASE105) { /* MigrationResult::Failed */ }
TEST(MigrationRegistry, FlagsRebakeNeededWithoutFailure_PHASE105) { /* MigrationResult::NeedsRebake */ }
```

- [ ] **Step 2: Header + impl.**

```cpp
// engine/lifecycle/migration_registry.h
struct MigrationContext { Scene* project; std::string fromVersion, toVersion; std::vector<std::string>& log; };
struct MigrationResultSuccess {};
struct MigrationResultFailed { std::string reason; };
struct MigrationResultNeedsManual { std::string guideUrl; };
struct MigrationResultNeedsRebake { std::string feature; };
using MigrationResult = std::variant<MigrationResultSuccess, MigrationResultFailed,
                                     MigrationResultNeedsManual, MigrationResultNeedsRebake>;

struct VersionPredicate
{
    static VersionPredicate lessThan(const std::string& v);
    static VersionPredicate greaterOrEqual(const std::string& v);
    bool matches(const std::string& version) const;
    // ... internals
};

struct MigrationEntry
{
    std::string featureId;
    VersionPredicate from, to;
    std::function<MigrationResult(MigrationContext&)> fn;
};

class MigrationRegistry
{
public:
    void registerMigration(MigrationEntry e);
    struct AggregateResult
    {
        std::vector<std::string> successes;       // feature ids
        std::vector<std::string> failures;        // "feature: reason"
        std::vector<std::string> needsRebake;
        std::vector<std::string> needsManual;     // "feature: url"
    };
    AggregateResult runMigrations(MigrationContext& ctx,
                                  const std::set<std::string>& projectFeatures);
};
```

- [ ] **Step 3: Commit.**

```bash
git commit -m "Phase 10.5 Slice 2 T4: MigrationRegistry"
```

---

## Task 5: PostUpdateReviewState — persists "shown for v0.2.0" flag

Goal: pure `FileIo`-templated read/write of `~/.config/vestige/post-update-review.json`. Returns `shouldShowFor(version)` based on whether `current_version > shown_for_version`.

**Files:**
- Create: `engine/lifecycle/post_update_review_state.{h,cpp}`
- Test: `tests/test_post_update_review_state.cpp`

- [ ] **Step 1: Failing tests.**

```cpp
TEST(PostUpdateReviewState, ShouldShowForFirstLaunchOfNewVersion_PHASE105)
{
    MemFs fs;
    PostUpdateReviewState s{fs, "/cfg/post-update-review.json"};
    EXPECT_TRUE(s.shouldShowFor("0.2.0"));
}

TEST(PostUpdateReviewState, RecordsDismissalSoNextLaunchSkipsForSameVersion_PHASE105)
{
    MemFs fs;
    PostUpdateReviewState s{fs, "/cfg/post-update-review.json"};
    s.recordDismissal("0.2.0");
    EXPECT_FALSE(s.shouldShowFor("0.2.0"));
}

TEST(PostUpdateReviewState, ShouldShowForNewerVersionEvenIfPriorDismissed_PHASE105)
{
    MemFs fs;
    PostUpdateReviewState s{fs, "/cfg/post-update-review.json"};
    s.recordDismissal("0.2.0");
    EXPECT_TRUE(s.shouldShowFor("0.3.0"));
}
```

- [ ] **Step 2: Implement + commit.**

```bash
git commit -m "Phase 10.5 Slice 2 T5: PostUpdateReviewState"
```

---

## Task 6: MigrationLogReader

Goal: parse `<project>/.vestige/migration-history.json` for the welcome panel.

**Files:**
- Create: `engine/lifecycle/migration_log_reader.{h,cpp}`
- Test: `tests/test_migration_log_reader.cpp`

- [ ] **Steps:** mirror the ChangelogParser shape — schema-versioned JSON, per-entry record (`fromVersion`, `toVersion`, `featureId`, `result`, `timestamp`). Test cases: empty file, single entry, malformed JSON.

```bash
git commit -m "Phase 10.5 Slice 2 T6: MigrationLogReader"
```

---

## Task 7: AutoRebakeOrchestrator

Goal: marks derived assets stale (light probe SH, GI baked indirect, cached navmesh) when migrations report `NeedsRebake`. Lazy — actual rebake fires next time the asset is consumed.

**Files:**
- Create: `engine/lifecycle/auto_rebake_orchestrator.{h,cpp}`
- Test: `tests/test_auto_rebake_orchestrator.cpp`

- [ ] **Steps:** registers a `markStale(featureId)` API that flips a per-feature dirty bit on the next-load side; pinned by tests that assert the right dirty bit gets set per feature_id.

```bash
git commit -m "Phase 10.5 Slice 2 T7: AutoRebakeOrchestrator"
```

---

## Task 8: UpdateChecker — periodic + on-demand

Goal: templated on `Transport`. Polls GitHub Releases API for the user's channel; respects 24h timer + 4h hard floor; returns `UpdateAvailable { manifest }` or `NoUpdate` or `RateLimited` or `Error`.

**Files:**
- Create: `engine/lifecycle/update_checker.{h,cpp}`
- Test: `tests/test_update_checker.cpp`

- [ ] **Step 1: Failing tests.**

```cpp
TEST(UpdateChecker, ReturnsUpdateAvailable_WhenManifestVersionGreater_PHASE105)
{
    MockTransport t;
    t.respondWith("https://api.github.com/repos/milnet01/Vestige/releases/latest",
                  HttpResponse{200, "application/json", /*release JSON with version 0.2.0*/});
    UpdateChecker c(t);
    auto result = c.checkNow("0.1.42", "stable");
    ASSERT_TRUE(std::holds_alternative<UpdateAvailable>(result));
    EXPECT_EQ(std::get<UpdateAvailable>(result).manifest.release.version, "0.2.0");
}

TEST(UpdateChecker, ReturnsNoUpdate_WhenManifestVersionEqual_PHASE105) { /*…*/ }

TEST(UpdateChecker, RespectsFourHourFloor_PHASE105)
{
    MockTransport t;
    UpdateChecker c(t);
    c.setLastCheckTime(std::chrono::system_clock::now());  // just now
    auto result = c.checkIfDue("0.1.42", "stable");
    EXPECT_TRUE(std::holds_alternative<TooSoon>(result));
}

TEST(UpdateChecker, ReturnsRateLimited_WhenHeaderRemainingZero_PHASE105) { /*…*/ }
```

- [ ] **Step 2: Implement.** Channel selection: `stable` queries `/releases/latest`; `nightly` queries `/releases?per_page=1` and filters by tag suffix. Manifest URL is in the release body (assets attached on GH Release page); the manifest itself is fetched in a second call.

- [ ] **Step 3: Commit.**

```bash
git commit -m "Phase 10.5 Slice 3 T8: UpdateChecker"
```

---

## Task 9: BinaryDownloader

Goal: streams archive to `~/.cache/vestige/updates/<v>/<archive>`; verifies SHA256 against manifest.

**Files:**
- Create: `engine/lifecycle/binary_downloader.{h,cpp}`
- Test: `tests/test_binary_downloader.cpp`

- [ ] **Steps:** `MockTransport` returns a fixed-bytes response; verify file written + SHA256 matched. Hash mismatch returns failure + leaves file at `<archive>.bad` for diagnosis. Progress callback for UI.

```bash
git commit -m "Phase 10.5 Slice 3 T9: BinaryDownloader"
```

---

## Task 10: BinarySwapper — the platform-specific install swap

Goal: atomic swap of install tree on Linux/macOS via `rename(2)`; Windows launcher pattern. Path-isolation enforcement (Section 5 hard invariant) asserts at construction.

**Files:**
- Create: `engine/lifecycle/binary_swapper.{h,cpp}`
- Create: `engine/lifecycle/binary_swapper_posix.cpp` (Linux + macOS)
- Create: `engine/lifecycle/binary_swapper_win.cpp` (Windows)
- Create: `tools/vestige_updater_win/main.cpp` (Windows launcher exe)
- Test: `tests/test_binary_swapper.cpp`

- [ ] **Step 1: Path-isolation test.**

```cpp
TEST(BinarySwapper, RejectsInstallRootEqualToOpenProjectRoot_PHASE105)
{
    MemFs fs;
    EXPECT_THROW(BinarySwapper(fs, "/proj", {"/proj"}), std::invalid_argument);
}

TEST(BinarySwapper, RejectsInstallRootInsideUserConfigDir_PHASE105)
{
    MemFs fs;
    EXPECT_THROW(BinarySwapper(fs, "/home/u/.config/vestige/sub",
                               {/*projects=*/}, /*userConfig=*/"/home/u/.config/vestige"),
                 std::invalid_argument);
}

TEST(BinarySwapper, AcceptsValidIsolatedInstallRoot_PHASE105)
{
    MemFs fs;
    BinarySwapper s(fs, "/usr/local/share/vestige", {"/proj"}, "/home/u/.config/vestige");
    SUCCEED();
}
```

- [ ] **Step 2: Posix swap test (full archive → install).**

```cpp
TEST(BinarySwapper, PosixSwapReplacesInstallTreeAtomically_PHASE105)
{
    // Stage at /tmp/staging; install at /opt/vestige. Run swap.
    // Pre-state: /opt/vestige/bin/vestige size 100; staging size 200.
    // Post-state: install size 200; backup at ~/.cache/vestige/binary-backups/<old-v>/ size 100.
}
```

- [ ] **Step 3: Implement Posix.** `rename(staging, install + ".new")`, then `rename(install, backup-dir)`, then `rename(install + ".new", install)`. Use `engine/utils/atomic_write.h` shape (write-temp-fsync-rename-fsync-dir).

- [ ] **Step 4: Implement Windows launcher.** Editor writes `<install>/.update-pending/<v>.cmd`; exits; launcher exe (separate small binary) reads the cmd file, performs the swap, restarts editor.

- [ ] **Step 5: Commit.**

```bash
git commit -m "Phase 10.5 Slice 3 T10: BinarySwapper (posix + win launcher)"
```

---

## Task 11: RollbackController

Goal: post-startup checks `~/.cache/vestige/startup-success-v<currently-installed>`; if absent, restores `~/.cache/vestige/binary-backups/<previous-v>/` and surfaces a notification.

**Files:**
- Create: `engine/lifecycle/rollback_controller.{h,cpp}`
- Test: `tests/test_rollback_controller.cpp`

- [ ] **Steps:** mock FileIo. Test states: success-marker present → no rollback. Success-marker absent → rollback fires. No backup available → log warning + no rollback (leaves user on the broken version with a manual-recovery hint).

```bash
git commit -m "Phase 10.5 Slice 3 T11: RollbackController"
```

---

## Task 12: UpdatePromptToast — non-modal "update available"

Goal: ImGui toast appears when `UpdateChecker` returns `UpdateAvailable`. Three buttons: release notes / Install / Later.

**Files:**
- Create: `engine/editor/panels/update_prompt_toast.{h,cpp}`
- Modify: `engine/editor/editor.{h,cpp}` (instantiate + drive lifecycle)
- Manual UI checklist (no unit test).

- [ ] **Steps:** toast slides in from top-right; respects `Settings → Accessibility → Reduced motion` (no slide animation when reduced); keyboard-navigable (Tab between buttons, Enter activates focused button, Escape dismisses); screen-reader label "Update v<new-v> available". Wire `onClickInstall` to open `UpdateDialog`.

```bash
git commit -m "Phase 10.5 Slice 3 T12: UpdatePromptToast"
```

---

## Task 13: UpdateDialog — full pre-install dialog

Goal: modal dialog with three-tier warning panel (project features migrated automatically / settings to double-check / files-being-replaced summary), changelog diff between running version and target, three buttons (Backup-then-install / Install now / Cancel), pre-1.0 caveat banner.

**Files:**
- Create: `engine/editor/panels/update_dialog.{h,cpp}`
- Manual UI checklist.

- [ ] **Steps:** uses `ChangelogParser` + `ProjectFeatureScanner` + `BreakingChangeIntersector` to compute the three-tier warning. Renders changelog body via existing `engine/editor/markdown_render` (re-used from the in-editor docs browser). Backup-then-install kicks `ProjectBackup::snapshot` then `BinaryDownloader::download` then `BinarySwapper::swap`. Progress bar for download. Pre-1.0 caveat from the spec verbatim.

```bash
git commit -m "Phase 10.5 Slice 3 T13: UpdateDialog"
```

---

## Task 14: PostUpdateWelcomePanel

Goal: post-relaunch modal with rendered changelog, migrations performed, settings to double-check, stale data needing rebake, "Don't show this again for v<new-v>" footer.

**Files:**
- Create: `engine/editor/panels/post_update_welcome_panel.{h,cpp}`
- Manual UI checklist.

- [ ] **Steps:** opens at first launch on the new version where `PostUpdateReviewState::shouldShowFor(VERSION) == true`. Inputs: parsed changelog entry for VERSION, `MigrationLogReader` output, `Settings::diffAgainstDefaults()`, `AutoRebakeOrchestrator::staleFeatures()`. Settings deep-link uses existing settings-panel routing. Modal but dismissible; "Don't show this again" default ticked → calls `PostUpdateReviewState::recordDismissal(VERSION)`.

```bash
git commit -m "Phase 10.5 Slice 4 T14: PostUpdateWelcomePanel"
```

---

## Task 15: Wire it all into `Editor`

Goal: `Editor::initialize` constructs `UpdateChecker`, fires startup check off the existing async-task pool. `Editor::drawPanels` draws the toast + dialog + welcome panel. `Help → Check for Updates` menu item triggers `UpdateChecker::checkNow`.

**Files:**
- Modify: `engine/editor/editor.{h,cpp}`
- Modify: `engine/core/settings.{h,cpp}` (channel + frequency keys)

- [ ] **Steps:** add member instances mirroring the W14 pattern (`m_updateChecker`, `m_updatePromptToast`, `m_updateDialog`, `m_postUpdateWelcomePanel`). Add `Help` menu entry. Settings panel: channel combo (Stable / Nightly with the explicit opt-in dialog when switching to Nightly per Q15), frequency combo (Always / 24h / Startup / Never). Per-project pin via `<project>/.vestige/update-channel.json`.

```bash
git commit -m "Phase 10.5 Slice 4 T15: wire updater into Editor + Settings"
```

---

## Task 16: ROADMAP / CHANGELOG / VERSION

- [ ] Mark `Phase 10.5 Slice 2-4` complete in ROADMAP.
- [ ] CHANGELOG entries.
- [ ] VERSION bump.

```bash
git commit -m "Phase 10.5 Slices 2-4 (doc): auto-updater shipped"
git push origin main
```

---

## Self-review

**Spec coverage:**
- §4 happy-path data flow → tasks 8 → 13 → 10 → 4 → 7 → 14.
- §4 migration mechanism → task 4.
- §4 rollback flow → task 11.
- §4 channels / frequency → task 15.
- §5 settings preservation hard invariant → task 10 (BinarySwapper path-isolation assertions).
- §6 PostUpdateWelcomePanel → task 14.
- §6 affected_settings metadata → task 1 (ChangelogParser).

**Placeholder scan:** every task names files + commands; "/* full release JSON */" placeholders in test fixtures expand to real fixtures during implementation (one author-time choice, acceptable here).

**Type consistency:** `MigrationResult` shape matches §4 spec. `BreakingFeatureScope`/`Severity` come from the shared-infra plan's `release_manifest.h`.

---

## Execution

Plan complete and saved to `docs/superpowers/plans/2026-04-25-phase-10-5-auto-updater-plan.md`.

Two execution options:

1. **Subagent-Driven (recommended)** — I dispatch a fresh subagent per task.
2. **Inline Execution** — execute tasks in this session.

When ready, say which approach and I'll proceed.
