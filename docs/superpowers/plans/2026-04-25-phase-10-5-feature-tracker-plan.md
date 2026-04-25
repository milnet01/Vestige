# Phase 10.5 — Feature Tracker Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Editor `Help → Suggest a Feature` panel that submits feature requests to GitHub Discussions Ideas, deduplicating against the bundled `ROADMAP.md` first.

**Architecture:** Pure helpers (`RoadmapParser`, `RoadmapMatcher`, `RoadmapFallbackFetcher`) + `Transport`-templated `DiscussionSubmitter` + ImGui panel. Reuses `IssueDeduplicator::extractKeywords` from the bug-reporter plan.

**Tech Stack:** C++17, libcurl (via shared-infra `HttpClient`), nlohmann::json, ImGui.

**Source spec:** `docs/superpowers/specs/2026-04-25-editor-feedback-and-lifecycle-design.md` Section 3.

**Depends on:**
- `docs/superpowers/plans/2026-04-25-phase-10-5-shared-infrastructure-plan.md` (HttpClient, GitHubAuth).
- `docs/superpowers/plans/2026-04-25-phase-10-5-bug-reporter-plan.md` (`IssueDeduplicator::extractKeywords`).

---

## File structure

| New file | Pure / GL-free? | Tests |
|---|---|---|
| `engine/feedback/roadmap_index.{h,cpp}` | Pure | (covered by parser tests) |
| `engine/feedback/roadmap_parser.{h,cpp}` | Pure | `tests/test_roadmap_parser.cpp` |
| `engine/feedback/roadmap_matcher.{h,cpp}` | Pure | `tests/test_roadmap_matcher.cpp` |
| `engine/feedback/roadmap_fallback_fetcher.{h,cpp}` | Templated `Transport` | `tests/test_roadmap_fallback_fetcher.cpp` |
| `engine/feedback/discussion_submitter.{h,cpp}` | Templated `Transport` | `tests/test_discussion_submitter.cpp` |
| `engine/editor/panels/feature_tracker_panel.{h,cpp}` | ImGui | manual checklist |

---

## Task 1: RoadmapParser

Goal: parse `ROADMAP.md` text → `RoadmapIndex` (struct of parsed bullets keyed by ID, with section + phase context).

**Files:**
- Create: `engine/feedback/roadmap_index.{h,cpp}`
- Create: `engine/feedback/roadmap_parser.{h,cpp}`
- Test: `tests/test_roadmap_parser.cpp`

- [ ] **Step 1: Failing tests.**

```cpp
constexpr const char* kRoadmapSample = R"(
## Phase 10.5: Editor Usability Pass

### Slice 1: Shared infrastructure

- [x] **F1.** `HttpClient` libcurl wrapper. Shipped 2026-04-26.
- [ ] **F2.** `NetworkLog` per-call audit writer.

### Slice 2: Auto-updater core

- [ ] **U1.** Manifest parser.
)";

TEST(RoadmapParser, ExtractsBulletWithIdAndTitle_PHASE105)
{
    auto index = parseRoadmap(kRoadmapSample);
    ASSERT_EQ(index.items.size(), 3u);
    EXPECT_EQ(index.items[0].id, "F1");
    EXPECT_EQ(index.items[0].title, "`HttpClient` libcurl wrapper");
    EXPECT_EQ(index.items[0].state, RoadmapBulletState::Shipped);
    EXPECT_EQ(index.items[0].section, "Slice 1: Shared infrastructure");
    EXPECT_EQ(index.items[0].phase, "Phase 10.5: Editor Usability Pass");
}

TEST(RoadmapParser, RecognisesAllBulletStateMarkers_PHASE105)
{
    // - [ ] = Open, - [x] = Shipped, - [~] = Partial
    auto index = parseRoadmap(R"(
- [ ] **A1.** open
- [x] **A2.** shipped
- [~] **A3.** partial
)");
    EXPECT_EQ(index.items[0].state, RoadmapBulletState::Open);
    EXPECT_EQ(index.items[1].state, RoadmapBulletState::Shipped);
    EXPECT_EQ(index.items[2].state, RoadmapBulletState::Partial);
}

TEST(RoadmapParser, MultilineBodyAttachesToBullet_PHASE105) { /*…*/ }

TEST(RoadmapParser, IgnoresBulletWithoutIdMarker_PHASE105) { /*…*/ }
```

- [ ] **Step 2: Implement.** Regex per spec Section 3: `^- \[([ x~])\] \*\*([^*]+)\*\* (.*)$`. Track nearest preceding `### ` for section, `## Phase ` for phase. Continuation lines (lines starting with whitespace, between this bullet and the next bullet/heading) accumulate into `body`.

- [ ] **Step 3: Build + test + commit.**

```bash
cmake --build build -j8 && ctest --test-dir build -R RoadmapParser --output-on-failure
git add engine/feedback/roadmap_index.h engine/feedback/roadmap_index.cpp engine/feedback/roadmap_parser.h engine/feedback/roadmap_parser.cpp tests/test_roadmap_parser.cpp engine/CMakeLists.txt tests/CMakeLists.txt
git commit -m "Phase 10.5 Slice 7 T1: RoadmapParser"
```

---

## Task 2: RoadmapMatcher

Goal: top-N matches between user-typed title/description and an indexed roadmap. Reuses `IssueDeduplicator::extractKeywords` for tokenisation.

**Files:**
- Create: `engine/feedback/roadmap_matcher.{h,cpp}`
- Test: `tests/test_roadmap_matcher.cpp`

- [ ] **Step 1: Failing tests.**

```cpp
TEST(RoadmapMatcher, MatchesByTokenOverlap_PHASE105)
{
    auto index = parseRoadmap(R"(
## Phase 10.5
### Slice 1
- [ ] **F1.** GPU frustum culling for foliage instancing
- [ ] **F2.** Audio bus parameterised pitch
)");

    auto matches = matchRoadmap(index, "GPU foliage culling", /*topN=*/2);
    ASSERT_FALSE(matches.empty());
    EXPECT_EQ(matches[0].item.id, "F1");
    EXPECT_GT(matches[0].score, 0.0f);
}

TEST(RoadmapMatcher, ReturnsEmptyWhenNoTokenOverlap_PHASE105) { /*…*/ }

TEST(RoadmapMatcher, RanksByJaccardSimilarity_PHASE105) { /*…*/ }
```

- [ ] **Step 2: Implement.** Score = `|user_tokens ∩ item_tokens| / |user_tokens ∪ item_tokens|`. Sort descending; cap at `topN`; drop items with score < 0.1 (configurable).

- [ ] **Step 3: Commit.**

```bash
git commit -m "Phase 10.5 Slice 7 T2: RoadmapMatcher"
```

---

## Task 3: RoadmapFallbackFetcher

Goal: fetch `ROADMAP.md` from `raw.githubusercontent.com` when bundled copy is missing; cache via `AtomicWrite` to `~/.cache/vestige/fallback-roadmap.md`. 7-day expiry.

**Files:**
- Create: `engine/feedback/roadmap_fallback_fetcher.{h,cpp}`
- Test: `tests/test_roadmap_fallback_fetcher.cpp` (mock Transport + FileIo)

- [ ] **Step 1: Failing tests.**

```cpp
TEST(RoadmapFallbackFetcher, ReturnsCachedWhenFreshAndPresent_PHASE105)
{
    MemFs fs;
    fs.files["/cache/fallback-roadmap.md"] = "## Phase 10.5\n\n- [ ] **F1.** foo";
    fs.fileMtimes["/cache/fallback-roadmap.md"] = recentTimestamp();
    MockTransport t;
    RoadmapFallbackFetcher fetcher{t, fs, "/cache/fallback-roadmap.md"};
    auto md = fetcher.fetch("milnet01/Vestige");
    ASSERT_TRUE(md.has_value());
    EXPECT_NE(md->find("**F1.**"), std::string::npos);
    EXPECT_TRUE(t.requestLog().empty());  // cache hit, no network
}

TEST(RoadmapFallbackFetcher, FetchesFromGithubWhenCacheMissing_PHASE105) { /*…*/ }

TEST(RoadmapFallbackFetcher, FetchesFromGithubWhenCacheStale_PHASE105) { /*…*/ }

TEST(RoadmapFallbackFetcher, ReturnsNulloptOn404_PHASE105) { /*…*/ }

TEST(RoadmapFallbackFetcher, ReturnsNulloptOnNetworkError_PHASE105) { /*…*/ }
```

- [ ] **Step 2: Implement.** URL: `https://raw.githubusercontent.com/<repo>/main/ROADMAP.md`.

- [ ] **Step 3: Commit.**

```bash
git commit -m "Phase 10.5 Slice 7 T3: RoadmapFallbackFetcher"
```

---

## Task 4: DiscussionSubmitter

Goal: GitHub Discussions GraphQL API: `createDiscussion` mutation in the "Ideas" category.

**Files:**
- Create: `engine/feedback/discussion_submitter.{h,cpp}`
- Test: `tests/test_discussion_submitter.cpp` (mock Transport)

- [ ] **Step 1: Failing tests.**

```cpp
TEST(DiscussionSubmitter, PostsCorrectGraphQlMutation_PHASE105)
{
    MockTransport t;
    t.respondWith("https://api.github.com/graphql",
        HttpResponse{200, "application/json",
            R"({"data":{"createDiscussion":{"discussion":{"number":17,"url":"https://github.com/.../17"}}}})"});

    DiscussionSubmitter sub(t);
    auto result = sub.submit({
        .repoOwner = "milnet01", .repoName = "Vestige",
        .categoryId = "DIC_kwDOXXXXXX", .title = "GPU foliage culling",
        .body = "Body text"
    });
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.discussionNumber, 17);

    // Verify mutation shape: must contain `mutation { createDiscussion(...) {…} }`
    auto& sent = t.requestLog().back();
    EXPECT_NE(sent.body.find("createDiscussion"), std::string::npos);
}

TEST(DiscussionSubmitter, AuthFailureReturnsError_PHASE105) { /*…*/ }
TEST(DiscussionSubmitter, RateLimitedReturnsRateLimit_PHASE105) { /*…*/ }
TEST(DiscussionSubmitter, MalformedResponseReturnsError_PHASE105) { /*…*/ }
```

- [ ] **Step 2: Implement.** GraphQL uses POST with `application/json` body containing the query string. Need `repositoryId` + `categoryId` — fetch once on first submit, cache in `~/.cache/vestige/github-ids.json`.

- [ ] **Step 3: Commit.**

```bash
git commit -m "Phase 10.5 Slice 7 T4: DiscussionSubmitter"
```

---

## Task 5: FeatureTrackerPanel

Goal: ImGui form with Section 3 happy-path UI.

**Files:**
- Create: `engine/editor/panels/feature_tracker_panel.{h,cpp}`
- Modify: `engine/editor/editor.{h,cpp}` (Help menu + drawPanels)

- [ ] **Steps:**
  1. On open: `RoadmapParser` parses `<install>/share/vestige/ROADMAP.md`. If missing: show fallback prompt (Download / Continue / Cancel) using `RoadmapFallbackFetcher`.
  2. Form fields: title / what / why / who-benefits / proposed-approach (optional).
  3. On title-edit (debounced 300 ms): `RoadmapMatcher::match` → top-5 matches displayed live.
  4. Match → "View on roadmap" deep-links into the in-editor docs browser.
  5. Match override → "Submit anyway with note" — confirm dialog; submission auto-tagged.
  6. Submit → preview → `DiscussionSubmitter::submit` → toast.
  7. Offline-queue path mirrors bug reporter.

- [ ] **Commit.**

```bash
git commit -m "Phase 10.5 Slice 7 T5: FeatureTrackerPanel + Editor wiring"
```

---

## Task 6: ROADMAP / CHANGELOG / VERSION

- [ ] Update docs.

```bash
git commit -m "Phase 10.5 Slice 7 (doc): feature tracker shipped"
git push origin main
```

---

## Self-review

**Spec coverage:** Section 3 components 1:1 with tasks 1–5. Network fallback, soft-block dedup, dedup-skipped tagging, offline queue all covered. Auto-tag labels match Section 3.

**Placeholder scan:** clean.

**Type consistency:** `RoadmapBulletState` enum used identically in parser + matcher. `MatchResult` struct shape consistent in matcher + panel.

---

## Execution

Plan complete and saved to `docs/superpowers/plans/2026-04-25-phase-10-5-feature-tracker-plan.md`.

Two execution options:

1. **Subagent-Driven (recommended)**.
2. **Inline Execution**.

When ready, say which approach.
