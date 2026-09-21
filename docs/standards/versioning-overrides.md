# Versioning — Vestige's answers

Overrides the machine-wide versioning standard at
`~/.claude/standards/versioning.md`. Nothing here is a delta. That standard
asks every project two questions and deliberately refuses to answer them: § 3
asks what "breaking" means where nothing imports you, and § 4 asks what would
make a `0.x` project `1.0`. This file is Vestige's answers and holds nothing
else.

An outside contributor cannot open that path. The rules this file depends on
are restated where they are used, so this file stands alone.

Semantic Versioning 2.0.0 governs, unchanged. Inside `0.x` the levels shift
down one per § 4: a breaking change bumps the MINOR, everything else bumps the
PATCH. Vestige is `0.x`, so a new feature is a PATCH here.

## 1. Breaking surfaces

A release is breaking if a user or an integrator upgrades and something that
used to work stops working. These are the surfaces where that can happen.

**A surface not listed is still a surface.** This list makes the common cases
cheap to judge. It does not bound the promise.

### Files a user owns

- **The settings file.** `engine/core/settings.h` declares the current schema
  version; `engine/core/settings_migration.h` declares how an older file is
  carried forward. Two cases, and conflating them is how a breaking change
  ships as a PATCH.

  **Adding a field with a default and NOT bumping the schema is a PATCH.** It
  needs no migration arm: the loader takes a missing field's value from the
  struct initialiser and ignores fields it does not know.

  **Bumping the schema always needs a migration arm, additive or not.**
  `migrate()` walks every version from the file's to the current one, and its
  `switch` has a `default:` arm that refuses. Bump to 6 without a
  `migrate_v5_to_v6` and every existing v5 file fails to migrate, so the caller
  falls back to defaults and the user's saved settings are gone. An arm that
  only advances the version marker is enough.

  **Removing a field, renaming one, or changing what a value means is breaking
  unless a migration arm carries the old shape forward.**
- **Scene files.** `engine/editor/scene_serializer.h` declares
  `CURRENT_FORMAT_VERSION` and the engine version stamped into every saved
  scene. The loader reads `format_version` out of the envelope, runs
  `migrateScene`, and refuses a file newer than it understands. Adding an
  optional block that older files load without is a PATCH — the `music` block
  was added that way. Changing or removing an existing block is breaking unless
  `migrateScene` carries the old shape forward.

  The same header declares `ENGINE_VERSION`, stamped into every saved scene.
  **No rule here attaches to it, deliberately.** It currently reads a version
  this project has never released. The loader does parse it back into
  `SceneMetadata::engineVersion`, but nothing in engine code branches on it —
  only a test asserts it. Whether it should track the project version at all is
  undecided; `3D_E-0683` settles that, and this list gains a rule for it in the
  same change.

### Files an author or translator owns

- **Localisation string tables.** `assets/localization/`. Removing a key, or
  changing what a key means, breaks every translation that already supplies it.
  Adding a key is a PATCH. **Adding the key to `en.json` is part of adding
  it** — `tools/localization_audit.py` fails the build on a `tr()` whose key is
  absent from the reference language, `assets/localization/en.json`. A key
  missing from a *secondary* language is reported and never fails, so
  translations can lag.
- **Audio material banks.** `assets/audio/synthesis/footstep_modal.json`
  declares per-material modal banks. It ships with the engine and a user may
  replace it. Adding a bank is a PATCH. Removing or renaming a material key,
  or changing what an existing coefficient means, is breaking.
- **The ducking route format.** `engine/audio/audio_ducking.cpp` parses
  `assets/audio/mix_graph.json`. No such file ships today, and its absence
  means manual ducking only — so nothing breaks yet. The *format* is still a
  surface, because a user who authors one is relying on it.

### The command line

`app/` defines the flags. Removing a flag, renaming one, or changing what a
flag does is breaking. Changing what a flag *prints* is breaking only where the
output is structured for a machine — `--profile-log` writes a CSV that
`tools/perf_gate.py` parses. Three parts, because they do not behave alike.

**The column order and what each column means are the contract.** That parser
reads every field by position, so reordering or repurposing a column is
breaking.

**The row labels are the contract too.** It builds its lookup key from the
`category` and `name` *values* and special-cases `frame,total`, so renaming a
profiling category or pass is breaking — every consumer keyed on that string
silently stops finding the series rather than failing.

**The header row is not the contract.** It is discarded on read, so renaming a
column heading is a PATCH.

### How the engine is installed

**The packaging layout.** Where the binary, the assets and the config directory
land, and what the installed tree is called. A user's launcher, script or
shortcut points at those paths, so moving them is breaking even when the engine
itself is unchanged. `## 0.6.0` on the roadmap names this surface.

### Not yet a surface

Two things look like surfaces and are not, because a user cannot reach either
one. Each becomes a surface when its roadmap item ships, and this list is
updated in the same change.

- **Keybindings.** `3D_E-0628` records that key rebinding and mouse
  sensitivity are wired to nothing. A binding nobody can change is not
  something a user can rely on.
- **Visual scripting graphs.** `3D_E-0627` records that `ScriptingSystem` is
  never constructed outside tests, so no authored graph runs. `NodeEditor.json`
  is not this surface — it is the node editor's own layout state, written to
  the user's config directory by the editor widget, not authored content.

## 2. What makes Vestige 1.0

**MAJOR stays 0 until the Tabernacle walkthrough runs standalone at the 60 FPS
minimum `CLAUDE.md` states, on the machine its "Dev hardware" section names.**

Standalone means a user launches it and walks through the structure without
the editor and without a development build.

**What an outside checker can and cannot do, stated plainly, because § 4's bar
is that someone else can check it.** The Tabernacle content is not in this
repository — `.gitignore` excludes `assets/textures/tabernacle/`, and the
scene code is held for a separate private repo. So the condition **cannot** be
checked from a clone of the engine.

It is checked against the **published walkthrough build**: it runs or it does
not, and the frame rate holds or it does not. Anyone holding that build can
perform the check without reading this repository. Until such a build is
published, only the maintainer can evaluate the 1.0 gate, and that is a
limitation of the gate rather than a property this file may assert away.

## 3. Milestone versions on the roadmap

`ROADMAP.md` groups its open work under **release headings** of the form
`## <MAJOR>.<MINOR>.0 — <theme>`, each holding the phases that close it.

**A release heading is not a `Milestone` block, and this rule reaches only the
first.** The roadmap carries many `Milestone` subsections at various heading
depths; each states a goal in prose and most of them break nothing. They are
untouched by everything in this section.

**A release heading is the only section inside its own span.** Everything below
it — the phases it closes, and their subsections — is heading text rather than
a section, so the roadmap viewer groups the work queue by release. That is the
point of the arrangement: a reader scanning what is planned sees releases, not
phases.

Under § 4 a MINOR bump inside `0.x` means a breaking change, so **every MINOR
release heading is followed by a line naming the surface it breaks.** Work that
breaks no surface does not need a MINOR release heading — it ships in the
ordinary PATCH cadence.

**Listed or not.** § 1's list makes the common cases cheap; it does not bound
the promise, and that catch-all reaches this rule. A heading may name a surface
§ 1 does not list — and **§ 1 gains that entry in the same change**, so the list
stays the cheap path rather than drifting behind the roadmap.

**A release that CREATES a surface takes the MINOR too, and names it.** § 1
defines breaking as something that used to work stopping, which a new surface
never does — so this rule would otherwise send it to the PATCH cadence. It takes
the MINOR because that release is the last moment the semantics are free: after
it, users depend on them. A heading may therefore name a `### Not yet a surface`
entry whose roadmap item ships in that release, and **that entry moves out of
the subsection in the same change.**

**`## 1.0.0` is the exception, because § 4's reasoning does not reach it.** That
bump leaves `0.x` rather than moving within it, and § 2 gates it on an exit
condition instead of a broken surface. Its following line names that exit
condition, and `Breaks nothing.` is correct there.

**Two kinds of heading are not release headings at all, and nothing above
applies to them.** The completed phases keep their own `## Phase N` headings,
because they shipped before this convention existed. `## Unscheduled — no release committed` holds
work not scheduled against any release; it names no surface, and an item
sitting there is not a claim that it breaks nothing.

The weekly cadence in `.github/workflows/release-cadence.yml` bumps the PATCH
only. A MINOR bump is therefore always deliberate, and closing a milestone is
the occasion for one.

## Cold-eyes loop log

Written by `review-contract`. One row per loop, oldest first. Never
back-filled; a correction goes in the current loop's row.

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Verified | Fixed | Outcome |
|---|---|---|---|---|---|---|---|---|
| 1 | 2026-09-21 | 3 | 3 | 1 | 1 | 5 | 5 | Genre `standard` (pinned), so Q4 not asked. All three lanes independently found the same three defects: § 3 described a roadmap structure that did not exist, the 1.0 exit condition asserted outside-checkability that `.gitignore` refutes, and the audio material-bank bullet stated no rule. A fourth — the settings bullet crediting the migration chain for absorbing a defaulted field — was found by all three, one framing it as the sharper Q2 against "a schema bump with no migration arm is breaking". A fifth came from a lane's routed open question: the localisation claim held for secondary languages but not for the reference language, where a missing key fails the build. Phase 1b had already caught three false claims before dispatch (scene format wrongly called unversioned, `NodeEditor.json` misidentified as authored content, `mix_graph.json` described as shipping). Phase 4c then caught two defects created by this loop's own fixes: an unresolvable `en.json` citation, and § 3 failing to account for `## After 1.0.0`, which is not a release heading. ROADMAP.md was restructured in the same change so § 3's description is true rather than aspirational. |
| 2 | 2026-09-21 | 3 | 2 | 1 | 1 | 4 | 4 | Every finding landed on text loop 1 had written, so this loop is measuring its own collateral. The one that matters reverses a loop-1 fix: two lanes read `engine/core/settings_migration.cpp` and found that `migrate()` walks every version from the file's to the current one through a `switch` whose `default:` arm refuses, so a schema bump with no arm makes every existing file fail to migrate and fall back to defaults. Loop 1 had rewritten that rule to say a purely additive bump needs no arm, which would have shipped silent settings loss as a PATCH. The original draft was right and loop 1 talked itself out of it. All three lanes found the second: section 3's rule reached the `## 1.0.0` heading, which is gated by section 2's exit condition rather than by a broken surface, so a maintainer would have deleted the one release heading section 2 makes mandatory. Third, "Two headings" was a miscount for two KINDS of heading, covering nine. Fourth, "its columns are a contract" was ambiguous where `tools/perf_gate.py` discards the header and reads by position. The 4b sweep then found the roadmap's 0.5.0 line naming a surface by a name section 1 does not use; corrected in the roadmap. |
| 3 | 2026-09-21 | 3 | 0 | 2 | 1 | 3 | 3 | CAP (a standard caps at 3). All three lanes found that section 3's rule said "breaks no LISTED surface -> PATCH" while section 1 says an unlisted surface is still a surface; the roadmap's 0.6.0 line already named one section 1 did not list. Two lanes then sharpened a second: section 1 defines breaking as something that used to work stopping, so a release that CREATES a surface breaks nothing and both rules sent it to the PATCH cadence, while the roadmap cuts 0.5.0 as a MINOR for exactly that. Two lanes found that the CSV contract omitted the row-label VALUES: `perf_gate.py` keys on `category,name` and special-cases `frame,total`, so renaming a profiling pass silently loses the series while this document called it a PATCH. Fixes: section 3 now reaches unlisted surfaces and requires section 1 to gain the entry in the same change, covers a release that creates a surface, and section 1 gained a packaging-layout entry. DISMISSED 1: two lanes independently declined to file that "nothing reads it back" about ENGINE_VERSION was false on the ground that it changes no version decision; corrected anyway because 3D_E-0683's implementer reads that sentence, and recorded here as the orchestrator overriding a lane's materiality call rather than as a lane finding. CAP DISCLOSURE. 3 of 3 verified findings anchored on text this run wrote, which is the violent-cap share -- but the measurement does not carry its usual meaning here, because this document was AUTHORED in this run, so there is no pre-existing text for the denominator to contain. Reported so a reader can disagree. Read against the trend instead: verified findings went 5, 4, 3 and changed class each loop -- false claims about code, then a reversal and scope errors, then edge cases of the rule's own scope. That is narrowing, not oscillating. SECOND SHARE: none. Phase 1c recorded no gated span because the file did not exist before this run, so the gate-versus-audit split has no denominator either; reported as an absence rather than as zero. |
