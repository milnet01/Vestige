# Phase 10.5 — First-Run Wizard (Design Doc)

**Status:** Draft — awaiting user sign-off on §10 open questions.
**Roadmap item:** Phase 10.5 Editor Usability Pass → Onboarding → "First-run welcome dialog — project template picker (…) with live preview thumbnails. Already partially covered by Phase 9D template system; this adds the first-run wrapper."
**Scope:** A single, skippable, resumable modal flow on first editor launch that (a) greets the user, (b) lets them pick a starting scene from the existing Phase 9D template library, (c) applies the pick, and (d) records completion so it does not reappear. Replaces the current informational `WelcomePanel`.

---

## 1. Why this doc exists

Two drive-bys motivate this:

1. **The demo scene problem (fixed in a parallel slice).** Until 2026-04-22 the engine defaulted to `Engine::setupTabernacleScene()`, which references assets under `assets/textures/tabernacle/` that live in a separate private repo. Fresh public clones fell back on placeholder materials. The parallel slice switched the default to `setupDemoScene()` — a neutral CC0 showroom. Good enough to not embarrass the project, but a showroom is not what a new user *wants* on first launch. They want to decide: **"am I building a first-person walkthrough, a 2.5D side-scroller, an isometric RPG, or starting empty?"**
2. **Infrastructure already exists, unwrapped.** Phase 9D shipped `TemplateDialog` (eight game-type templates: `FIRST_PERSON_3D`, `THIRD_PERSON_3D`, `TWO_POINT_FIVE_D`, `ISOMETRIC`, `TOP_DOWN`, `POINT_AND_CLICK`, `SIDE_SCROLLER_2D`, `SHMUP_2D`) and `WelcomePanel` (an info-only first-run modal that writes a `welcome_shown` flag file). Neither references the other. This doc composes them.

This doc's job is to specify the **composed first-run flow**, not to design new templates or rewrite the template dialog — those exist.

---

## 2. What already exists (inventory)

Surveyed by an exploration pass on 2026-04-22.

| Area | Location | State |
|---|---|---|
| **Welcome panel** | `engine/editor/panels/welcome_panel.{h,cpp}` | Complete as an informational modal. First-run detection via `<configDir>/welcome_shown` flag file. Does NOT offer scene templates — it shows keyboard shortcuts and a tools overview. Ships a "Don't show on startup" checkbox. |
| **Template dialog** | `engine/editor/panels/template_dialog.{h,cpp}` | Complete, 8 templates. Modal, invoked from menu (not from first-run). Each template sets projection, camera position, gravity, whether to create a ground plane / player / directional light / skybox. Returns via `applyTemplate(config, scene, resources, renderer)`. |
| **Settings primitive** | `engine/core/settings.{h,cpp}` (slice 13.1) | JSON-backed, atomic-write, migration-aware. Adds a sensible home for a persistent `hasCompletedFirstRun` flag, rather than another one-off flag file. |
| **ConfigPath resolver** | `engine/utils/config_path.{h,cpp}` (slice 13.1) | Cross-platform per-user config directory. Same source of truth for both the settings JSON and any remaining flag files. |
| **Demo-scene default** | `engine/core/engine.cpp::setupDemoScene` + `biblicalDemo` flag | The engine's fallback content. First-run wizard's **Empty / Showroom** picks should be served by this function; custom picks construct via `TemplateDialog`. |

**What is missing:**

1. **No composition.** The welcome panel and the template dialog are two independent modals. A first-run user sees the welcome (keyboard shortcuts), clicks *Close*, and lands on the demo scene with no prompt to pick a starting template.
2. **Ad-hoc first-run flag.** `WelcomePanel` writes `<configDir>/welcome_shown` — a separate persistence channel from the JSON-backed settings. Two sources of truth for "is this a new user?" will drift.
3. **No template visibility gating.** `TemplateDialog::getTemplates()` returns all eight unconditionally. A private sibling repo with a *biblical walkthrough* template should not advertise itself in a public clone; a public user should not see options that depend on missing assets.
4. **No "skip" path.** The user must close the welcome panel explicitly; there is no single-click "just give me the editor, I'll pick later" button.
5. **No resumability.** If the user quits mid-wizard (closes the window after welcome but before picking a template), there is no concept of *which step* they had reached.

---

## 3. CPU / GPU placement (CLAUDE.md Rule 12)

Entirely CPU. ImGui modal rendering, file I/O for the persistence flag, scene construction via existing template code. No per-pixel or per-vertex work. Inherits CPU placement from `TemplateDialog` and `WelcomePanel`. The *scene* the wizard builds runs on the usual CPU scene-graph + GPU render path, unchanged.

---

## 4. Flow

The wizard is a **two-step state machine** plus an optional tour handoff. Each step is its own ImGui modal; closing any modal treats the wizard as complete.

```
┌───────────────────┐   ┌───────────────────┐   ┌─────────────────────────┐
│ STEP 1 — Welcome  │──▶│ STEP 2 — Template │──▶│ DONE — markFirstRunDone │
│                   │   │ picker            │   │ Optional: trigger tour. │
│ "Pick a starting  │   │ (wrapping existing│   └─────────────────────────┘
│  scene"           │   │  TemplateDialog)  │
│                   │   │                   │
│ Buttons:          │   │ Primary:  Create  │
│ - Pick template → │   │ Secondary: Back   │
│ - Start empty     │   │ Link:   Skip for  │
│ - Skip for now    │   │         now       │
└───────────────────┘   └───────────────────┘
```

- **"Pick template"** advances to Step 2.
- **"Start empty"** calls `setupDemoScene()` (already the default) and marks first-run complete. This is the existing behaviour, just with an explicit button so the user knows it is a choice.
- **"Skip for now"** closes the wizard *without* marking it complete — next launch re-opens at Step 1. Matches the VSCode "remind me later" semantic.
- **"Back"** from Step 2 returns to Step 1 (does not mark complete).
- **Closing the window/modal** via the × or Esc counts as "Skip for now" at Step 1, and "Back" at Step 2. Do not silently swallow the intent.

No blocking overlay. The editor is navigable behind the modal (the user can drag panels, see the viewport, etc.) — this matches how Phase 9D's `TemplateDialog` already behaves.

---

## 5. Persistence

A single new flag on `Settings`:

```cpp
// AccessibilitySettings and UIStateSettings already exist as sibling
// sections. Add a new "Onboarding" section rather than overloading
// either; these are distinct concerns.
struct OnboardingSettings
{
    /// True once the user has reached a terminal state in the wizard
    /// (Finish or Start Empty). False means the wizard re-opens next
    /// launch. Migration: promoted from legacy flag file if present.
    bool hasCompletedFirstRun = false;

    /// Wall-clock ISO-8601 timestamp when completion was recorded.
    /// Useful for "welcome back to Vestige 0.3" messaging later.
    /// Empty string = never completed.
    std::string completedAt;
};
```

**Migration from legacy flag file:** if `<configDir>/welcome_shown` exists at first load and `Settings.onboarding.hasCompletedFirstRun == false`, promote it — set `hasCompletedFirstRun = true`, delete the flag file. This prevents upgraders from being ambushed with an unexpected wizard on first post-upgrade launch. The promotion happens once in `Settings::loadFromDisk()`'s post-parse hook and is lossless.

**Schema version bump:** add a migration `v1 → v2` in `settings_migration.cpp` that inserts the default `onboarding` block when absent. This is the first exercise of the chained-migration scaffolding shipped in slice 13.1.

---

## 6. Template visibility gating

The public engine repo should **not** advertise templates whose assets are absent.

- `TemplateDialog::getTemplates()` returns the full list; the wizard filters it.
- A template carries an optional list of required asset paths (`GameTemplateConfig::requiredAssets`, new field, default empty). Empty list = always visible.
- The wizard's Step 2 filter tests each `requiredAssets` path relative to `config.assetPath` and hides templates that fail the test.
- The biblical walkthrough template (to be added in the private sibling repo) lists its tabernacle textures + HDRI as required assets. Public users do not see the option; the maintainer with the private repo on disk sees it surface automatically.
- The non-wizard path (`File → New from Template…`) does not filter — a developer explicitly opening the dialog can still see unavailable options greyed-out, so they know the feature exists. Wizard is friendlier; power-menu is complete.

**Rationale:** hiding a menu entry in the wizard but leaving a greyed-out version in the menu means the user can discover *what is possible* without being tripped up *right now*.

---

## 7. Replacing the existing `WelcomePanel`

The current `WelcomePanel` is information-only — keyboard shortcuts + tools overview — and auto-opens on first launch. The wizard subsumes its first-run role. Two options:

- **Option A: delete `WelcomePanel`, move the keyboard-shortcuts / tools-overview content into the wizard's Step 1.** Simpler. Loses the "re-open Welcome from Help menu" affordance that exists today.
- **Option B: keep `WelcomePanel` as a user-invokable "Help → Show Welcome" modal, but remove its auto-open-on-first-launch behaviour.** Wizard handles first-run; welcome is only for re-reading the shortcuts later.

Recommendation: **B**. The keyboard-shortcuts table is load-bearing reference material — users come back to it. Keeping it accessible post-onboarding preserves that. The wizard's Step 1 can link to it (*"Need the shortcut list? Help → Show Welcome"*).

---

## 8. Slice breakdown

| Slice | Summary | Tests (target) |
|---|---|---|
| 14.1 | `OnboardingSettings` struct + `Settings` integration + `v1 → v2` migration + legacy flag promotion. No UI yet. | 6 — default values, round-trip, migration, legacy flag promotion, promotion deletes flag file, skipped promotion when already complete. |
| 14.2 | `FirstRunWizard` panel class (Step 1 + Step 2 + state machine). Wraps `TemplateDialog` as Step 2; reuses `applyTemplate`. Reads / writes `OnboardingSettings`. Modal, dismissable. | 8 — state transitions (welcome → picker → done, welcome → skip, picker → back → welcome, each close path), "Start Empty" applies `setupDemoScene()`, "Finish" writes `hasCompletedFirstRun = true` + timestamp. |
| 14.3 | Template visibility filter (`GameTemplateConfig::requiredAssets` + wizard-side filter). | 4 — empty `requiredAssets` always visible; missing asset hides template; present asset shows template; non-wizard menu always lists all. |
| 14.4 | Engine wiring (cold-start trigger), `WelcomePanel` auto-open disabled, Help-menu "Show Welcome" entry retained. Wizard and welcome no longer overlap. | 3 — first-run opens wizard (not welcome), second-run opens neither, welcome opens on demand from Help menu. |

**Why four slices, not one?** Matches the 13.x cadence — each slice is independently reviewable and committable, and 14.1 ships a Settings migration exercise (first use of the chained-migration scaffolding) that is worth landing separately from the UI change.

---

## 9. Non-goals

- **Thumbnails in the picker.** Phase 10.5 roadmap bullet mentions "live preview thumbnails". Out of scope for this doc — ships as a 14.5 follow-on or defers to a dedicated "template thumbnail capture" slice. Thumbnails are a content-pipeline concern (rendering each template at 256×256 and saving to disk) and entangling them with the wizard state machine would bloat the scope.
- **Guided tour.** Roadmap bullet "Guided tour (dismissable, resumable) — highlights the 6 things a new user needs to know: viewport navigation, panel layout, entity inspector, assets panel, save/load, play mode" is a separate ~14.6 slice. The wizard only triggers its start ("Take the tour?" checkbox on the Finish step, default off).
- **Project management.** The wizard picks a scene template, not a project. Vestige's project concept is a single directory with `assets/`, `scenes/`, settings, and recents — there is no project-file format yet. If a project file lands later, the wizard is the natural place to prompt "open existing, create new, use defaults", and §10 Q5 exists to flag that seam now.
- **Telemetry.** Roadmap mentions opt-in local telemetry. Deliberately not part of this doc — the wizard ships without observing which template was picked.
- **Custom templates.** Users cannot add their own templates through the wizard in this phase. `TemplateDialog` already does not support this; the wizard inherits the limitation.

---

## 10. Open questions (need user sign-off)

1. **Which of the eight existing Phase 9D templates should be featured in Step 2?** All eight makes the picker a wall of text; featuring 3-4 is more welcoming but then the rest feel second-class.
   - Proposal: feature **FIRST_PERSON_3D** (matches the engine's original walkthrough intent), **THIRD_PERSON_3D**, **TWO_POINT_FIVE_D**, **ISOMETRIC**. The remaining four (top-down, point-and-click, side-scroller, shmup) live under a *"More templates"* expander. Rationale: the featured four cover the archetype-coverage space; the rest are refinements inside those archetypes.

2. **What does "Start Empty" actually give the user?** Options: (a) the current `setupDemoScene()` showroom with four textured blocks (hand-holding); (b) a truly empty scene — just a camera and a directional light, no geometry (blank slate). The second is more honest.
   - Proposal: **(b)** — empty scene, one camera, one directional light. "Start Empty" should be empty. Add an explicit "Show Me the Demo" separate button for the current showroom behaviour if demo discovery still matters.

3. **Replace `WelcomePanel` (Option A) or keep it behind Help menu (Option B)?** See §7.
   - Proposal: **Option B** — keyboard-shortcuts reference survives as a re-openable panel.

4. **Should the biblical walkthrough template live in the private sibling repo, or is it a third-party-author template that the public engine merely *supports* registering?** First form is simpler (hardcoded, gated by file presence); second form needs a template plugin mechanism that does not exist.
   - Proposal: **private-repo form for now.** The biblical repo's `CMakeLists.txt` can simply add its template `.cpp` to the engine build when the private repo is present (same pattern as `VESTIGE_FETCH_ASSETS`). A proper template-plugin mechanism is a Phase 11+ concern.

5. **Do we pre-allocate space for a future "project" concept?** I.e., when the wizard writes `onboarding.hasCompletedFirstRun`, should it also write `onboarding.currentProjectPath` for a later phase, or is YAGNI the right call?
   - Proposal: **YAGNI.** Add the field when the project concept lands. The schema migration machinery (slice 13.1) makes it cheap to add later.

6. **Where does the wizard live architecturally — `engine/editor/panels/first_run_wizard.{h,cpp}` (beside welcome_panel) or a new `engine/editor/onboarding/` directory?**
   - Proposal: **`engine/editor/panels/first_run_wizard.{h,cpp}`.** Matches the existing one-class-per-file convention and keeps navigation simple. A subdirectory would make sense only if a tour / hints / next-step pane all land in the same phase.

7. **What does "Skip for now" do on the *second* launch — silently skip and assume the user meant "I don't want this"? Or re-prompt forever?**
   - Proposal: **re-prompt twice, then silently skip.** After two "Skip for now" clicks the wizard sets `hasCompletedFirstRun = true` with no-template and moves on. User can always re-open via Help menu. Matches the gentler VSCode "we'll stop asking now" behaviour.

8. **Accessibility: should the wizard respect `AccessibilitySettings.reducedMotion` on its Step 1 → Step 2 transition?** If so, the transition animation (if any) is suppressed under reduced-motion.
   - Proposal: **yes, inherit the global reduced-motion.** Consistent with every other modal in the editor. If we add any transition animation at all, it is a crossfade ≤ 150 ms, and reduced-motion cuts it to instant.

---

## 11. Tests (per slice)

All slices ship with GoogleTest coverage. Key surfaces:

- **Settings round-trip** — write `hasCompletedFirstRun=true`, reload, verify preserved.
- **Migration** — load a `schemaVersion:1` file lacking the `onboarding` block, verify post-migration block is present with defaults.
- **Legacy flag promotion** — stage a `welcome_shown` file + missing `onboarding.hasCompletedFirstRun`, load settings, verify flag promoted, file deleted, settings saved.
- **State machine** — from welcome, every button maps to the correct successor state.
- **Close semantics** — closing the modal via Esc / × is treated as "Skip for now" at Step 1 and "Back" at Step 2, not as "Finish".
- **Template filter** — `requiredAssets` pointing at a non-existent path hides the template; empty list shows it; non-wizard `File → New from Template…` lists everything.

No visual tests required; the wizard is covered by unit + headless integration. If a visual regression lands later, one golden screenshot of each step suffices.

---

## 12. Sign-off log

Awaiting user response to §10 Q1–Q8.

(Pattern matches `PHASE10_SETTINGS_DESIGN.md` — once approved, this section records the eight answers and the implementation proceeds through slices 14.1 – 14.4.)
