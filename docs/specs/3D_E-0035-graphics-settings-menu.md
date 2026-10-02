<!-- ants-spec-format: 1 -->
# 3D_E-0035 — Give players a graphics settings page

**Status:** accepted (2026-10-02).
**Kind:** implement.
**Source:** ROADMAP 3D_E-0035 (user-request-2026-07-11).
**Pairs with:** 3D_E-0746 (menus answer the mouse), 3D_E-0747 (the overlay draws).

Layman: the game's Settings screen gets a Graphics page where a player
picks Low, Medium, High or Ultra, or tunes each option, and the choice is
kept between sessions.

## 1. Goal

A player in a game built on Vestige opens Settings, sees the graphics
options on the Display page, and changes them with mouse or keyboard. A
named preset sets every option at once; changing one option marks the
preset Custom. Changes preview live, Apply saves them, Revert or closing
without Apply undoes them. Every option, Custom included, survives a
restart.

## 2. Problem

1. **The player's Settings screen is an empty frame.** `buildSettingsMenu`
   (`engine/ui/menu_prefabs.cpp`) draws the header, a five-category
   sidebar and a footer, and its own comment says per-category controls
   are left to game projects. Its Restore Defaults, Revert and Apply
   buttons do nothing.
2. **Custom does not survive a restart.** `DisplaySettings`
   (`engine/core/settings.h`) stores `qualityPreset` and `renderScale`
   only. The options a preset sets — anti-aliasing, SSAO, bloom, the
   heavy-post gate and the terrain, foliage, grass and tree tiers — live
   only in the renderers. `applyQualityPreset`
   (`engine/core/settings_apply.cpp`) applies nothing for `Custom`, so a
   Custom player restarts with whatever the renderers default to.
3. **No widget can be adjusted from the keyboard.** `UISystem::handleKey`
   maps Left and Right to focus movement, so a focused slider or dropdown
   cannot change value without a mouse.

The phase-10 preset design (`docs/phases/phase_10_tier1_render_scale_and_presets_design.md`
§4.1, *Custom transition*) says Custom applies nothing and the player's
toggles stand. That held while the toggles were never saved. This spec
replaces that clause: Custom applies the stored values (§4.2).

## 3. Scope decisions (agreed with the user)

The user was away on 2026-10-02 and asked for decisions to be made and
recorded. Claude made these; each is reversible in one place.

- **Options shown are the ones with a live setter today** (§4.4). Shadow
  quality, view distance, field of view, motion blur, texture detail,
  reflections, ray tracing and a resolution list have no runtime setter
  and are out of scope (§9).
- **A named preset is a label for its row, re-derived on load.** A file
  saying `High` gets High's current values, so retuning a preset reaches
  every player on it. Only Custom keeps its own stored values.
- **Closing Settings without Apply reverts**, as most games do.
- **Render scale in the player menu runs 50–100 %.** The editor keeps
  its 25–200 % range for developers.
- **Graphics options go on the existing Display page.** The other four
  categories stay empty here; they are not this item.

## 4. Design

### 4.1 Saved settings

`DisplaySettings` gains one struct. The JSON key is `display.graphics`.

```cpp
// engine/core/settings.h
enum class DetailTier { Low, Medium, High };      // on disk "low" / "medium" / "high"

struct GraphicsSettings
{
    AntiAliasMode antiAlias      = AntiAliasMode::TAA;  // "off" "fxaa" "smaa" "taa" "msaa4x"
    bool          ambientOcclusion = true;              // SSAO
    bool          bloom            = true;
    bool          volumetrics      = true;              // volumetric fog + dynamic GI (heavy-post gate)
    DetailTier    terrainDetail    = DetailTier::High;
    DetailTier    foliageDetail    = DetailTier::High;
    DetailTier    grassDetail      = DetailTier::High;
    DetailTier    treeDetail       = DetailTier::High;
    bool operator==(const GraphicsSettings&) const;
};

struct DisplaySettings
{
    // ...existing fields...
    GraphicsSettings graphics;   // 3D_E-0035
};
```

An unknown string on disk falls back to the field's default, the policy
`qualityPresetFromString` already follows. `kCurrentSchemaVersion` goes
from 6 to 7, with `migrate_v6_to_v7` in `engine/core/settings_migration.cpp`
(§14). The four subsystem tier enums (`TerrainGroundQuality`,
`FoliageQuality`, `GrassQuality`, `TreeQuality`) each map from
`DetailTier` by name.

### 4.2 Presets and apply

`engine/core/settings_apply.{h,cpp}` replaces `applyQualityPreset` with
three functions:

```cpp
struct QualityRow { float renderScale; GraphicsSettings graphics; };

/// The Low / Medium / High / Ultra rows. Custom has no row.
QualityRow qualityRowFor(QualityPreset preset);

/// Sets preset, renderScale and every graphics field from the preset's row.
/// Custom only sets the label.
void selectQualityPreset(DisplaySettings& display, QualityPreset preset);

/// Pushes display.graphics to the sink. Never reads the preset.
void applyGraphics(const DisplaySettings& display, RendererQualitySink& sink);
```

The rows keep the values `applyQualityPreset` uses today; High and Ultra
stay identical until a Tier-2 setter separates them.

- **Load.** After migration and validation, a named preset is passed
  through `selectQualityPreset`, so its stored values never matter.
- **Push.** `SettingsEditor::pushPendingToSinks` calls `applyGraphics`.
  It no longer writes to `m_pending`, so loading leaves the editor clean.
- **Edit.** Selecting a preset calls `selectQualityPreset`. Changing any
  single graphics option or the render scale sets `qualityPreset =
  Custom`, as the editor's render-scale slider already does. This covers
  the player page and the editor's Display tab
  (`SettingsEditorPanel::drawDisplayTab`), whose preset combo switches to
  `selectQualityPreset`.

Picking a named preset rewrites every value. A preset that kept earlier
overrides is the commonest complaint about such menus (Source:
https://discussions.unity.com/t/qualitysettings-setqualitylevel-and-custom-changes/800756).

### 4.3 The page

New `engine/ui/graphics_settings_page.{h,cpp}`:

```cpp
/// Adds the graphics controls to the Settings content area, bound to
/// `editor`. Every change goes through SettingsEditor::mutate, so it
/// previews live.
void buildGraphicsSettingsPage(UICanvas& canvas, const UITheme& theme,
                               TextRenderer* text, SettingsEditor& editor);
```

The controls sit in one container element. Before drawing its children
each frame, it copies their shown state from `editor.pending()`, so the
preset dropdown reads Custom the moment an option changes, and a Revert
shows at once. The page holds no settings state of its own.

`buildSettingsMenu` gains an overload taking the footer actions:

```cpp
struct SettingsMenuActions
{
    std::function<void()> apply;            // SettingsEditor::apply(settingsPath)
    std::function<void()> revert;           // SettingsEditor::revert()
    std::function<void()> restoreDefaults;  // SettingsEditor::restoreGraphicsDefaults()
    std::function<bool()> isDirty;          // SettingsEditor::isDirty()
};
```

`SettingsEditor::restoreGraphicsDefaults()` is new. It resets the page's
fields — preset, render scale, `graphics`, `vsync`, `fullscreen` — to
`DisplaySettings{}`'s values and keeps the window size, which this page
does not show. `restoreDisplayDefaults` stays as it is for the editor.

Apply and Revert are enabled only while `isDirty()` is true, and the
footer status reads `ui.settings.saved` or `ui.settings.unsaved` to match.
These update each frame like the page.

One free function in `graphics_settings_page.h` does the wiring, so the
engine and the tests run the same code:

```cpp
/// Registers the Settings builder (chrome, actions, page) and connects
/// onModalPopped: when Settings closes and `editor` is dirty, revert().
void wireSettingsScreen(UISystem& ui, SettingsEditor& editor,
                        const std::filesystem::path& settingsPath);

/// Every localisation key the page and the footer use (INV-10).
std::vector<std::string_view> settingsPageKeys();
```

`Engine::initialize` calls it, with `Settings::defaultPath()`, when game
screens are on.

### 4.4 Controls

| Control | Type | Values | Field |
|---|---|---|---|
| Quality preset | dropdown | Low, Medium, High, Ultra, Custom | `qualityPreset` |
| Render scale | slider | 50–100 %; `keyStep` 5 %; drag is continuous | `renderScale` |
| Anti-aliasing | dropdown | Off, FXAA, SMAA, TAA, MSAA 4× | `graphics.antiAlias` |
| Ambient occlusion | checkbox | on / off | `graphics.ambientOcclusion` |
| Bloom | checkbox | on / off | `graphics.bloom` |
| Volumetric fog and lighting | checkbox | on / off | `graphics.volumetrics` |
| Terrain detail | dropdown | Low, Medium, High | `graphics.terrainDetail` |
| Foliage detail | dropdown | Low, Medium, High | `graphics.foliageDetail` |
| Grass detail | dropdown | Low, Medium, High | `graphics.grassDetail` |
| Tree detail | dropdown | Low, Medium, High | `graphics.treeDetail` |
| Vertical sync | checkbox | on / off | `vsync` |
| Window mode | dropdown | Windowed, Fullscreen | `fullscreen` |

Picking Custom from the preset dropdown changes nothing but the label.
Vertical sync and window mode are not part of a preset and do not set
Custom. Every label and option is a `ui.settings.graphics.*` key, present
in each `assets/localization/<code>.json` table that
`kSupportedLanguages` lists.

### 4.5 Keyboard adjustment

`UIElement` gains `virtual bool adjust(int direction)`; the default
returns false. `UISlider` moves by `keyStep` (new field, default a
twentieth of its range) and fires `onValueChanged`. `UIDropdown` moves
the selection one option, clamped, and fires `onSelectionChanged`.
A slider or dropdown returns true even when clamped at an end, so the
key is consumed and focus stays. `UISystem::handleKey` sends Left and
Right to the focused element's `adjust` first and moves focus only when
it returns false. Up, Down and
Tab always move focus.

## 5. Invariants

- **INV-1** — `selectQualityPreset(display, p)` for a named `p` leaves
  `renderScale` and every `graphics` field equal to `qualityRowFor(p)`,
  whatever they held before.
  *Test:* `tests/test_settings.cpp`, `GraphicsSettings.SelectingPresetRewritesEveryField`.
  *Breaks when:* a field is left out of the row copy, or the copy skips
  fields that were customised.

- **INV-2** — On the player page, changing any option in §4.4 except the
  preset, vertical sync and window mode sets `qualityPreset` to Custom.
  *Test:* `tests/test_graphics_settings_page.cpp`, one case per control.
  *Breaks when:* a control's handler writes its field without the label.

- **INV-3** — `applyGraphics` pushes `display.graphics` to every sink
  setter, for every preset including Custom.
  *Test:* `tests/test_settings.cpp`, `GraphicsSettings.ApplyPushesStoredValues`
  with a recording sink.
  *Breaks when:* apply reads the preset, or misses a setter.

- **INV-4** — A Custom display section saves and loads back equal; a
  named preset loads with its row's values, whatever the file stored.
  *Test:* `tests/test_settings.cpp`, `GraphicsSettings.RoundTrip*`.
  *Breaks when:* a field is not serialised, or load trusts a named
  preset's stored values.

- **INV-5** — A version-6 file migrates to version 7: Custom gains High's
  graphics values with its own `renderScale` kept; a named preset gains
  its row.
  *Test:* `tests/test_settings.cpp`, `SettingsMigration.V6ToV7*`.
  *Breaks when:* the step is missing from `migrate`, or overwrites
  `renderScale`.

- **INV-6** — The volumetrics option never turns on a pass accessibility
  turned off. This is the phase-10 INV-A11Y AND gate; this item must not
  bypass it.
  *Test:* `tests/test_settings.cpp`,
  `SettingsApply.QualityPresetCannotReEnableAccessibilityDisabledHeavyPost`,
  rewritten to go through `selectQualityPreset` and `applyGraphics`.
  *Breaks when:* `applyGraphics` sends `graphics.volumetrics` anywhere
  but `setHeavyPostEnabled`.

- **INV-7** — Closing Settings with unapplied changes reverts them: the
  sinks receive the applied state again, and `isDirty()` is false.
  *Test:* `tests/test_graphics_settings_page.cpp`, `CloseWithoutApplyReverts`,
  which calls `wireSettingsScreen`.
  *Breaks when:* `wireSettingsScreen` does not connect the close handler,
  or the handler runs before the modal pops.

- **INV-8** — Left and Right adjust a focused slider or dropdown, and
  keep focus on it even at the end of its range; from any other element
  they move focus.
  *Test:* `tests/test_ui_system_input.cpp`, `UISystemKeys.*`.
  *Breaks when:* `handleKey` moves focus before asking `adjust`, or a
  clamped `adjust` returns false.

- **INV-9** — Loading settings leaves the editor clean: `isDirty()` is
  false after construction and `forceLiveApply()`.
  *Test:* `tests/test_settings.cpp`, `SettingsEditor.LoadIsNotDirty`.
  *Breaks when:* the push path writes into `m_pending` again.

- **INV-10** — Every key the page and the footer use, `ui.settings.unsaved`
  included, is in all nine tables. `LocalizationAuditStrict` checks
  English only and reports the rest without failing, so it does not
  cover this.
  *Test:* `tests/test_localization_tables.cpp`,
  `LocalizationTables.SettingsPageKeysInEveryTable`, over
  `settingsPageKeys()`.
  *Breaks when:* a key is added to `en.json` and not to another table.

## 6. Failure modes

- **Unknown string on disk** (hand edit, newer build): the field takes its
  default and the file loads with status Ok, as other enum fields do.
- **A subsystem is absent** (no grass or trees in the scene): the sink
  no-ops that setter already (`RendererQualityApplySinkImpl`); the
  control still saves its value.
- **Apply fails to write the file:** `SettingsEditor::apply` leaves the
  editor dirty; the footer keeps reading unsaved and Apply stays enabled.
- **Window mode change fails:** the dropdown still shows the pending
  value, and Revert or closing restores the previous mode through
  `applyDisplay`. What `applyDisplay` does on a failed mode switch is
  unchanged by this item.
- **Language change while Settings is open:** `rebuildOpenScreens`
  rebuilds the page with the new strings; it reads `pending()`, so no
  edit is lost.

## 7. Tests

| Invariant | Where | Notes |
|---|---|---|
| INV-1, INV-3, INV-4, INV-5, INV-6, INV-9 | `tests/test_settings.cpp` | pure CPU |
| INV-2, INV-7 | `tests/test_graphics_settings_page.cpp` (new) | builds the page on a canvas, drives it through `UISystem` press/key calls, recording sinks |
| INV-8 | `tests/test_ui_system_input.cpp` | |
| INV-10 | `tests/test_localization_tables.cpp` | |

Each new test is seen failing against the code without its rule before
it lands. The in-app check is `vestige --player`, Esc, Settings: change
the preset and one option, Apply, restart, and see both kept.

## 8. Alternatives considered (and rejected)

- **Store the preset only, keep options in the renderers** (today's
  design). Custom is lost on restart; §2 item 2.
- **Trust stored values for named presets too.** A retuned preset would
  never reach existing players, and a file could claim High while
  holding Low values.
- **Rebuild the Settings canvas after each change.** Simple, but it
  drops keyboard focus on every change; a keyboard player could not
  adjust two options in a row.
- **A separate Graphics category in the sidebar.** The sidebar's five
  categories are fixed by the menu design; Display is where graphics
  options belong.

## 9. Out of scope

- Shadow quality, view distance, field of view, motion blur, texture
  detail, reflection quality, ray tracing, resolution list — each needs a
  runtime setter first; deferred; not yet queued.
- Content for the Audio, Controls, Gameplay and Accessibility pages —
  deferred; not yet queued.
- Clicking a sidebar category to switch pages — needs content on more
  than one page first.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/test_settings.cpp::GraphicsSettings.SelectingPresetRewritesEveryField` |
| INV-2 | `tests/test_graphics_settings_page.cpp` |
| INV-3 | `tests/test_settings.cpp::GraphicsSettings.ApplyPushesStoredValues` |
| INV-4 | `tests/test_settings.cpp::GraphicsSettings.RoundTrip*` |
| INV-5 | `tests/test_settings.cpp::SettingsMigration.V6ToV7*` |
| INV-6 | `tests/test_settings.cpp::SettingsApply.QualityPresetCannotReEnableAccessibilityDisabledHeavyPost` |
| INV-7 | `tests/test_graphics_settings_page.cpp::CloseWithoutApplyReverts` |
| INV-8 | `tests/test_ui_system_input.cpp::UISystemKeys.*` |
| INV-9 | `tests/test_settings.cpp::SettingsEditor.LoadIsNotDirty` |
| INV-10 | `tests/test_localization_tables.cpp::LocalizationTables.SettingsPageKeysInEveryTable` |
| Options stay readable and centred | **nothing** automated — the in-app check in §7 |

## 11. Cross-doc impact

- `docs/phases/phase_10_tier1_render_scale_and_presets_design.md` §4.1
  *Custom transition* — superseded by §4.2; a one-line pointer is added
  there.
- `docs/localization/review.md` — the new keys join the unreviewed
  translations.
- `CHANGELOG.md`, `ROADMAP.md` — at ship.

## 12. Cold-eyes loop log

Rows live in `../reviews/3D_E-0035-graphics-settings-menu-loop-log.md`.

## 13. CPU / GPU placement

All of this is CPU work: settings data, menu widgets and calls into the
existing renderer setters. No GPU work is added or moved; the passes the
options switch are unchanged. `CODING_STANDARDS.md` §17 needs no new row.

## 14. Migration / compatibility

`migrate_v6_to_v7` adds `display.graphics`. For a named preset it writes
that preset's row; for Custom it writes High's row and leaves
`renderScale` alone. Load then re-derives named presets anyway (§4.2),
so the step matters only for Custom files. A version-7 file read by an
older build is refused by the existing future-version guard in `migrate`.
