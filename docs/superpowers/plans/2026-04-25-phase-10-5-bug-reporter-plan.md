# Phase 10.5 — Bug Reporter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Editor `Help → Report a Bug` panel that submits issues to the engine's GitHub Issues with auto-attached diagnostics, deduplicates against existing issues via GitHub Search, and surfaces fixes for already-resolved bugs.

**Architecture:** Pure helpers (`DiagnosticScrubber`, `IssueDeduplicator`, `OfflineQueue`, `ResolvedFixLookup`) + transport-templated submitters + ImGui panel. The diagnostic-collection and submission paths use the shared infrastructure (`HttpClient`, `GitHubAuth`, network-log).

**Tech Stack:** C++17, libcurl (via shared-infra `HttpClient`), nlohmann::json, ImGui. No GL beyond what already exists.

**Source spec:** `docs/superpowers/specs/2026-04-25-editor-feedback-and-lifecycle-design.md` Section 2.

**Depends on:** `docs/superpowers/plans/2026-04-25-phase-10-5-shared-infrastructure-plan.md` (HttpClient, GitHubAuth, NetworkLog, FileIo).

---

## File structure

| New file | Pure / GL-free? | Tests |
|---|---|---|
| `engine/feedback/diagnostic_collector.{h,cpp}` | Mostly pure (one `glGetString` at init) | `tests/test_diagnostic_collector.cpp` (mock GL info) |
| `engine/feedback/diagnostic_scrubber.{h,cpp}` | Pure | `tests/test_diagnostic_scrubber.cpp` |
| `engine/feedback/issue_deduplicator.{h,cpp}` | Pure | `tests/test_issue_deduplicator.cpp` |
| `engine/feedback/issue_submitter.{h,cpp}` | Templated `Transport` | `tests/test_issue_submitter.cpp` |
| `engine/feedback/offline_queue.{h,cpp}` | Pure (`FileIo` mocked) | `tests/test_offline_queue.cpp` |
| `engine/feedback/resolved_fix_lookup.{h,cpp}` | Pure | `tests/test_resolved_fix_lookup.cpp` |
| `engine/editor/panels/bug_reporter_panel.{h,cpp}` | ImGui | manual checklist |
| Modify `engine/editor/editor.{h,cpp}` | Wire into `Help` menu + `drawPanels` | (compile check) |
| Create `.github/ISSUE_TEMPLATE/editor-reported-bug.md` | n/a | (visual review on GitHub) |

---

## Task 1: DiagnosticScrubber

Goal: Q4 hybrid — whitelist for auto-attached fields + regex strip for free-form fields. Removes paths, emails, IPs, MACs.

**Files:**
- Create: `engine/feedback/diagnostic_scrubber.{h,cpp}`
- Test: `tests/test_diagnostic_scrubber.cpp`

- [ ] **Step 1: Failing tests cover every regex.**

```cpp
// tests/test_diagnostic_scrubber.cpp
#include <gtest/gtest.h>
#include "feedback/diagnostic_scrubber.h"

namespace Vestige::Feedback::Test
{
TEST(DiagnosticScrubber, StripsLinuxAbsolutePaths_PHASE105)
{
    EXPECT_EQ(scrubFreeForm("crash at /home/alice/proj/scenes/main.json"),
              "crash at <PATH>/scenes/main.json");
}
TEST(DiagnosticScrubber, StripsWindowsPaths_PHASE105)
{
    EXPECT_EQ(scrubFreeForm("missing C:\\Users\\bob\\AppData\\config.ini"),
              "missing <PATH>\\config.ini");
}
TEST(DiagnosticScrubber, StripsEmailAddresses_PHASE105)
{
    EXPECT_EQ(scrubFreeForm("contact alice@example.com"), "contact <EMAIL>");
}
TEST(DiagnosticScrubber, StripsIPv4Addresses_PHASE105)
{
    EXPECT_EQ(scrubFreeForm("server 192.168.1.42 unreachable"), "server <IP> unreachable");
}
TEST(DiagnosticScrubber, StripsIPv6Addresses_PHASE105) { /*…*/ }
TEST(DiagnosticScrubber, StripsMacAddresses_PHASE105) { /*…*/ }
TEST(DiagnosticScrubber, LeavesProjectRelativePathsAlone_PHASE105)
{
    EXPECT_EQ(scrubFreeForm("error in scenes/main.json line 42"),
              "error in scenes/main.json line 42");
}
TEST(DiagnosticScrubber, IsIdempotent_PHASE105)
{
    auto once = scrubFreeForm("/home/alice/proj/x");
    EXPECT_EQ(scrubFreeForm(once), once);
}
}
```

- [ ] **Step 2: Header.**

```cpp
// engine/feedback/diagnostic_scrubber.h
#pragma once
#include <string>

namespace Vestige::Feedback
{
/// @brief Scrub free-form user text per Q4 hybrid privacy floor.
std::string scrubFreeForm(const std::string& input);

/// @brief Whitelist a structured-field name. Returns the value unchanged if
///        the field is in the whitelist, an empty string otherwise.
std::string filterStructuredField(const std::string& fieldName,
                                  const std::string& value);
}
```

- [ ] **Step 3: Implementation.** `std::regex` (C++17). Patterns: `(?:/|^)home/[^/\s]+`, `[A-Z]:\\Users\\[^\\\s]+`, `[\w._%+-]+@[\w.-]+\.[A-Za-z]{2,}`, `\b\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}\b`, IPv6 8-group hex, `(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}`.

- [ ] **Step 4: Build + test + commit.**

```bash
cmake --build build -j8 && ctest --test-dir build -R DiagnosticScrubber --output-on-failure
git add engine/feedback/diagnostic_scrubber.h engine/feedback/diagnostic_scrubber.cpp tests/test_diagnostic_scrubber.cpp engine/CMakeLists.txt tests/CMakeLists.txt
git commit -m "Phase 10.5 Slice 6 T1: DiagnosticScrubber"
```

---

## Task 2: DiagnosticCollector

Goal: builds the auto-attach `DiagnosticBundle`. Q8 maximalist defaults: VERSION, build commit, OS/kernel, GL vendor/renderer/version, GPU model, log tail (capped 200 lines, paths regex-stripped), recent-actions ring buffer, scene name (path-stripped), optional screenshot.

**Files:**
- Create: `engine/feedback/diagnostic_collector.{h,cpp}`
- Test: `tests/test_diagnostic_collector.cpp`

- [ ] **Step 1: Failing test injecting mock providers.**

```cpp
TEST(DiagnosticCollector, BuildsBundleWithAllSourcesEnabled_PHASE105)
{
    DiagnosticCollectorIo io;
    io.versionFn   = []{ return "0.1.42"; };
    io.commitFn    = []{ return "abc1234"; };
    io.osInfoFn    = []{ return "Linux 6.19.12 x86_64"; };
    io.glVendorFn  = []{ return "AMD"; };
    io.glRendererFn= []{ return "Radeon RX 6600"; };
    io.glVersionFn = []{ return "4.5 (Core Profile) Mesa 25.0"; };
    io.gpuModelFn  = []{ return "Radeon RX 6600 (RDNA2)"; };
    io.logTailFn   = []{ return std::vector<std::string>{"line1","line2"}; };
    io.recentActionsFn = []{ return std::vector<std::string>{"open scene","place block"}; };
    io.sceneNameFn = []{ return "main.json"; };

    DiagnosticCollector c{io};
    auto bundle = c.collect();

    EXPECT_EQ(bundle.version, "0.1.42");
    EXPECT_EQ(bundle.commit,  "abc1234");
    EXPECT_EQ(bundle.glVendor,   "AMD");
    EXPECT_EQ(bundle.gpuModel,   "Radeon RX 6600 (RDNA2)");
    EXPECT_EQ(bundle.logTail.size(), 2u);
    EXPECT_EQ(bundle.recentActions.size(), 2u);
    EXPECT_EQ(bundle.sceneName, "main.json");
}

TEST(DiagnosticCollector, RespectsOptOutToggles_PHASE105)
{
    /* set toggles.includeLogTail = false; verify bundle.logTail empty */
}
```

- [ ] **Step 2: Header + impl.** `DiagnosticCollectorIo` is a struct of `std::function`s; production wires GL queries; tests inject lambdas. The non-trivial bit is the log-tail scrub: pipes through `DiagnosticScrubber::scrubFreeForm` per line.

- [ ] **Step 3: Commit.**

```bash
git commit -m "Phase 10.5 Slice 6 T2: DiagnosticCollector"
```

---

## Task 3: IssueDeduplicator

Goal: Q3 deterministic keyword extraction → GitHub Search query string.

**Files:**
- Create: `engine/feedback/issue_deduplicator.{h,cpp}`
- Test: `tests/test_issue_deduplicator.cpp`

- [ ] **Step 1: Failing tests.**

```cpp
TEST(IssueDeduplicator, ExtractsKeywordsFromTitleStripsStopWords_PHASE105)
{
    auto kws = extractKeywords("Crash on the IBL probe bake when scene is empty");
    EXPECT_TRUE(std::find(kws.begin(), kws.end(), "crash") != kws.end());
    EXPECT_TRUE(std::find(kws.begin(), kws.end(), "ibl") != kws.end());
    EXPECT_TRUE(std::find(kws.begin(), kws.end(), "probe") != kws.end());
    EXPECT_FALSE(std::find(kws.begin(), kws.end(), "the") != kws.end());
    EXPECT_FALSE(std::find(kws.begin(), kws.end(), "is") != kws.end());
}

TEST(IssueDeduplicator, BuildsGitHubSearchQuery_PHASE105)
{
    auto q = buildSearchQuery({"crash", "ibl", "probe"}, "milnet01/Vestige");
    EXPECT_EQ(q, "repo:milnet01/Vestige is:issue crash ibl probe");
}

TEST(IssueDeduplicator, DropsTokensShorterThanThreeChars_PHASE105) { /*…*/ }

TEST(IssueDeduplicator, LowercasesAndDeduplicates_PHASE105) { /*…*/ }
```

- [ ] **Step 2: Implement** (split on non-alphanumerics; lowercase; drop stopwords from a curated 80-word list; drop tokens <3 chars; preserve insertion order).

- [ ] **Step 3: Commit.**

```bash
git commit -m "Phase 10.5 Slice 6 T3: IssueDeduplicator"
```

---

## Task 4: ResolvedFixLookup

Goal: given a closed GitHub issue payload, parse the close-event for the linked PR/commit and the version it shipped in.

**Files:**
- Create: `engine/feedback/resolved_fix_lookup.{h,cpp}`
- Test: `tests/test_resolved_fix_lookup.cpp`

- [ ] **Step 1: Failing tests.**

```cpp
TEST(ResolvedFixLookup, ExtractsPrNumberFromCloseEvent_PHASE105)
{
    constexpr const char* kClosedIssueJson = R"({
      "state": "closed",
      "state_reason": "completed",
      "closed_at": "2026-04-15T10:00:00Z",
      "pull_request": null,
      "events_url": "...",
      "milestone": {"title": "v0.1.42"}
    })";
    auto fix = parseResolvedFix(kClosedIssueJson);
    ASSERT_TRUE(fix.has_value());
    EXPECT_EQ(fix->shippedInVersion, "0.1.42");
}
TEST(ResolvedFixLookup, ReturnsNulloptForOpenIssue_PHASE105) { /*…*/ }
TEST(ResolvedFixLookup, ReturnsNulloptForClosedAsNotPlanned_PHASE105) { /*…*/ }
```

- [ ] **Step 2: Implement.** Parse `state`, `state_reason`, `milestone.title` (extract version via regex `v?(\d+\.\d+\.\d+)`). If milestone absent, fall back to `closed_at + linked PR` lookup (separate Transport call — kept out of this pure helper; the IssueSubmitter wires it).

- [ ] **Step 3: Commit.**

```bash
git commit -m "Phase 10.5 Slice 6 T4: ResolvedFixLookup"
```

---

## Task 5: OfflineQueue

Goal: persist unsent reports to `~/.config/vestige/pending-reports/<timestamp>.json`; drain on next online launch.

**Files:**
- Create: `engine/feedback/offline_queue.{h,cpp}`
- Test: `tests/test_offline_queue.cpp` (mock `FileIo`)

- [ ] **Step 1: Failing tests.**

```cpp
TEST(OfflineQueue, EnqueueWritesJsonFile_PHASE105)
{
    MemFs fs;
    OfflineQueue q{fs, "/cfg/pending"};
    q.enqueue("{\"title\":\"x\"}");
    EXPECT_EQ(q.pendingCount(), 1u);
}

TEST(OfflineQueue, DrainCallsCallbackInOrderRemovesOnSuccess_PHASE105)
{
    MemFs fs;
    OfflineQueue q{fs, "/cfg/pending"};
    q.enqueue("{\"id\":1}");
    q.enqueue("{\"id\":2}");

    std::vector<std::string> seen;
    q.drain([&](const std::string& payload) {
        seen.push_back(payload);
        return true;  // success
    });
    EXPECT_EQ(seen.size(), 2u);
    EXPECT_EQ(q.pendingCount(), 0u);
}

TEST(OfflineQueue, FailedDrainLeavesEntryInPlace_PHASE105) { /*…*/ }
```

- [ ] **Step 2: Implement.** File names use a monotonic timestamp; drain iterates `listDirectory` sorted; uses `AtomicWrite::writeFile` for enqueue durability.

- [ ] **Step 3: Commit.**

```bash
git commit -m "Phase 10.5 Slice 6 T5: OfflineQueue"
```

---

## Task 6: IssueSubmitter

Goal: `searchSimilarIssues(query)` and `submitNewIssue(bundle, body)` and `commentOnIssue(number, body)`. Templated on `Transport`.

**Files:**
- Create: `engine/feedback/issue_submitter.{h,cpp}`
- Test: `tests/test_issue_submitter.cpp` (mock Transport)

- [ ] **Step 1: Failing tests.**

```cpp
TEST(IssueSubmitter, SearchReturnsMatches_PHASE105)
{
    MockTransport t;
    t.respondWith("https://api.github.com/search/issues?q=repo:milnet01/Vestige+is:issue+crash+ibl+probe",
        HttpResponse{200, "application/json",
            R"({"total_count":1,"items":[{"number":42,"title":"Crash in IBL bake","state":"closed","html_url":"...","milestone":{"title":"v0.1.40"}}]})"});

    IssueSubmitter sub(t);
    auto matches = sub.searchSimilar({"crash", "ibl", "probe"}, "milnet01/Vestige");
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches[0].number, 42);
    EXPECT_EQ(matches[0].state, "closed");
}

TEST(IssueSubmitter, SubmitNewIssuePostsCorrectBody_PHASE105) { /*…*/ }
TEST(IssueSubmitter, RateLimitedReturnsRateLimitInfo_PHASE105) { /*…*/ }
TEST(IssueSubmitter, MalformedResponseReturnsEmpty_PHASE105) { /*…*/ }
```

- [ ] **Step 2: Implement.** Auth header uses `GitHubAuth::getStoredToken()` if user is authenticated, otherwise relay-endpoint token. Auto-tags issues with `bug`, `from-editor`, `version-X.Y.Z`, attribution label.

- [ ] **Step 3: Commit.**

```bash
git commit -m "Phase 10.5 Slice 6 T6: IssueSubmitter"
```

---

## Task 7: BugReporterPanel

Goal: ImGui form with the Section 2 happy-path UI.

**Files:**
- Create: `engine/editor/panels/bug_reporter_panel.{h,cpp}`
- Modify: `engine/editor/editor.{h,cpp}` (`Help` menu + draw)

- [ ] **Steps:** title field, "What happened" multiline, "Expected" multiline, "Reproduction" multiline. Per-row diagnostic toggles (auto-on, opt-out). Submit button → preview dialog with exact bytes → confirm → search → results panel → user picks "Add comment" / "Update" (chains to UpdateDialog) / "Submit new". Toast on completion. Offline-queue if unreachable. Keyboard-only navigation per Section 6.

```bash
git commit -m "Phase 10.5 Slice 6 T7: BugReporterPanel + Editor wiring"
```

---

## Task 8: GitHub issue template + ROADMAP / CHANGELOG / VERSION

- [ ] **Step 1: Create `.github/ISSUE_TEMPLATE/editor-reported-bug.md`** so non-editor reporters using the GitHub UI follow the same shape.

- [ ] **Step 2: ROADMAP entry** under Phase 10.5 section.

- [ ] **Step 3: CHANGELOG entry** with bug-reporter feature description.

- [ ] **Step 4: VERSION bump.**

```bash
git commit -m "Phase 10.5 Slice 6 (doc): bug reporter shipped"
git push origin main
```

---

## Self-review

**Spec coverage:** Section 2 components mapped 1:1 to tasks 1–7. Privacy floor (Section 1) — task 1 + task 2's scrub pipe. Offline-queue (Section 2 error-handling) → task 5. Resolved-fix-update chain (Section 2) → task 4 + task 7 wiring.

**Placeholder scan:** test bodies use `/*…*/` for non-distinguishing variants; the implementer expands to mirror the headline tests. Acceptable.

**Type consistency:** `IssueMatch` shape in task 6 matches the panel's expected display (number, title, state, url, milestone) — both use the same struct from `issue_submitter.h`.

---

## Execution

Plan complete and saved to `docs/superpowers/plans/2026-04-25-phase-10-5-bug-reporter-plan.md`.

Two execution options:

1. **Subagent-Driven (recommended)**.
2. **Inline Execution**.

When ready, say which approach and I'll proceed.
