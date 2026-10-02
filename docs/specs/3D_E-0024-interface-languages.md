<!-- ants-spec-format: 1 -->
# 3D_E-0024 — Translatable in-game interface and five more languages

**Status:** accepted (2026-10-02).
**Kind:** feature.
**Source:** ROADMAP 3D_E-0024 (user request 2026-07-04).

Layman: the menus and on-screen messages can be shown in French, German, Spanish, Italian and Brazilian Portuguese as well as English, Greek, Hebrew and Latin, and accented letters display properly.

## 1. Goal

Every piece of text the engine itself puts in front of a player — menus,
prompts, the framing of captions, overlays — resolves through the string table, so it follows the
language chosen in Settings, and switching language updates the screen that is
open. Eight languages besides English carry every key.

## 2. Problem

`tr()` and the string tables exist (`engine/localization/`), but
`assets/localization/en.json` holds six keys: the five main-menu buttons and
the keybind row's clear label. The rest of the in-game interface is English
literals:

- `engine/ui/menu_prefabs.cpp` builds the main, pause and settings menus from
  `makeLabel("...")` literals, a pause-button table and the settings
  `categories[]` array.
- `UIKeybindRow` draws `"PRESS KEY..."`; `UIFpsCounter` formats `"%.0f FPS"`;
  `UISlider` formats `"%d %%"`.
- `UIInteractionPrompt` builds `"Press [" + keyLabel + "] to " + actionVerb`,
  and `engine/ui/subtitle_renderer.cpp` builds a dialogue line as
  `speaker + ": " + text` and a sound cue as `"[" + text + "]"`.
  Word order differs between languages, so these need patterns with named
  slots, not concatenation.

`tools/localization_audit.py` reports clean because its sink list does not
include the menu builders' `makeLabel` and `makeButton`, so the menus' literals
are never checked; it also cannot see a key held in a table, or a literal
reaching a sink through a variable.

The roadmap item assumed Latin-script languages need no font work. They do:
`TextRenderer::initialize` loads the UI face with printable ASCII (0x20–0x7E)
and Greek only, so é, ü, ñ, ç and ã, and the em dash the input code already
prints for an unbound key, render as the fallback glyph. The bundled
`inter_tight.ttf` has those glyphs; they are not loaded.

Menus are built once per screen change. `LocalizationService::setLanguage`
publishes `LanguageChangedEvent`, which nothing in `UISystem` hears, so an open
menu keeps the old language until the screen changes.

The language list is written out three times — `Settings` validation
(`settings.cpp`), the editor's picker (`settings_editor_panel.cpp`) and the
audit's default secondary languages (`localization_audit.py`) — each naming
en/he/el/la.

## 3. Scope decisions (agreed with the user)

Proposed by Claude on 2026-10-02 while the user was away; the user's standing
instruction for the day was to decide and record. Each can be reversed before
the build.

- **The editor (ImGui) stays English.** The localization design defers editor
  i18n (`string_table.h`), and translating hundreds of tool strings serves
  authors, not players.
- **Input action and key names stay English for now.** They are shown only in
  the editor's settings panel today; the in-game Controls tab does not exist
  yet. When it is built, it keys them.
- **Translations are written by Claude and marked for native-speaker review**
  in `docs/localization/review.md`, one row per language. Machine-quality UI
  strings are better than English-only, and the review list says which have
  not been checked by a speaker.
- **Mock-up placeholder labels are not keyed**; 3D_E-0744 removed them.

## 4. Design

### 4.1 One language list

`engine/localization/supported_languages.h` (new) declares
`struct SupportedLanguage { const char* code; const char* englishName; const
char* nativeName; }` and a `supportedLanguages()` span: en, el, he, la, fr, de,
es, it, pt-BR. `Settings` validation and the editor picker both read it; the
picker keeps today's "English name + native name" labels. A table file is
`assets/localization/<code>.json`, so pt-BR's is `pt-BR.json`. The audit, which
cannot read C++, takes its default secondary languages from the table files
present in `assets/localization/` other than `en.json`.

### 4.2 Fonts

`TextRenderer::initialize`'s primary-face ranges add Latin-1 Supplement
(0x00A0–0x00FF), Latin Extended-A (0x0100–0x017F) and General Punctuation
(0x2010–0x2027). The ranges become a named constant so a test can check them.

### 4.3 Patterns with named slots

`std::string trf(std::string_view key, std::initializer_list<TrArg> args)`
(new, beside `tr`), where `TrArg` is `{std::string_view name; std::string
value;}`. It looks the key up as `tr` does and replaces each `{name}` with its
value. A placeholder with no matching argument is left as written; `{{` and
`}}` write a literal brace. The substitution is a pure function,
`substituteTrArgs(pattern, args)`, so it is tested without a service.

For tests, `ScopedStringTableOverride` (in `localization_service.h`, compiled
only under `VESTIGE_TEST_HOOKS` like the hook in `atomic_write.h`) installs a
`StringTable` that `tr` and `trf` consult before any service, for its lifetime.
Each widget's text is built by a function a test can call without a renderer:
`UIInteractionPrompt`'s existing composition,
`composeSubtitleText(const Subtitle&)` (today `composeText` in
`subtitle_renderer.cpp`'s anonymous namespace, moved out and declared in
`subtitle_renderer.h`), and new `UIFpsCounter::composeText(float fps)` and
`UISlider::composePercentText(int percent)`, which `render()` then draws.

### 4.4 Keys

All new keys live in `en.json` first. Prefixes:

- `ui.menu.*`, `ui.pause.*`, `ui.settings.*` — the menu labels, the pause
  buttons and the settings categories. A settings category's number prefix
  ("01  ") stays outside the key, joined in code.
- `ui.keybind.press_key`, `ui.hud.fps` (`"{fps} FPS"`),
  `ui.slider.percent` (`"{value} %"`).
- `ui.prompt.interact` (`"Press [{key}] to {action}"`). `actionVerb` holds a
  key, `ui.prompt.verb.use` by default, resolved with `tr` each time the text
  is built, so it follows a language change. Game code sets
  `ui.prompt.verb.*` keys; a plain word it sets instead (`"open"`) still shows,
  untranslated, because `tr` returns an unknown key as written.
- `subtitle.speaker_line` (`"{speaker}: {text}"`) and `subtitle.sound_cue`
  (`"[{text}]"`).

Proper nouns, the product name and the copyright line are not keyed; each such
`makeLabel` carries the audit's existing `// i18n-exempt` marker, since §4.6
makes `makeLabel` a checked sink.

### 4.5 Language change rebuilds the open screens

`UISystem` subscribes to `LanguageChangedEvent` at initialisation; the handler
calls a public `markTextStale()`, which only marks the open screens stale. The
next `UISystem::update` rebuilds the root canvas and the top modal with their
current builders. The event is published synchronously inside `setLanguage`,
so rebuilding in the handler would clear a canvas that any caller of
`setLanguage` from a click callback (a future in-game language control) may
still be iterating. The rebuild does not emit `onRootScreenChanged`, `onModalPushed` or
`onModalPopped`, since no screen changed. The subscription is released in
`shutdown`.

### 4.6 The lint

`localization_audit.py` adds `makeLabel(` and `makeButton(` to check 1's
sinks, and gains a check: every string literal in `engine/` shaped like a key
under the `ui.`, `subtitle.` or `input.` prefixes must exist in `en.json`. This catches keys held in tables, which the `tr("...")` pattern
misses. It is strict, like the existing key check.

### 4.7 Tables

`en.json` holds every key. `fr`, `de`, `es`, `it` and `pt-BR` are new and
complete; `el`, `he` and `la` are completed. The audit's report-only coverage
check (a key missing from a secondary language) stays report-only, as the
localization design decided; full coverage at this change is checked by
running the audit and reading its report.

## 5. Invariants

- **INV-1** — `substituteTrArgs` replaces every `{name}` that has an argument,
  leaves one without an argument as written, and turns `{{` / `}}` into
  single braces.
  *Test:* `tests/test_localization_format.cpp`.
  *Breaks when:* an unmatched placeholder is erased, so a missing argument
  silently drops a word.

- **INV-2** — The interaction prompt, subtitle lines, FPS counter and slider
  build their text from `trf` patterns, so a table that reorders the slots
  reorders the output.
  *Test:* `tests/test_localization_format.cpp`: under a
  `ScopedStringTableOverride` whose `ui.prompt.interact` puts `{action}` before
  `{key}`, the prompt's composed text puts the verb first; the subtitle, FPS
  and slider compose functions follow their overridden patterns the same way.
  *Breaks when:* a widget still concatenates English around the values.

- **INV-3** — After a `LanguageChangedEvent`, the next `UISystem::update`
  rebuilds the open root screen and the top modal once each, the handler
  itself rebuilds nothing, and none of the screen-change signals fire.
  *Test:* `tests/test_ui_system_screen_stack.cpp`, which builds `UISystem`
  without `initialize`, calls `markTextStale()` as the handler would, and
  counts builder calls and signal emissions across the call and the next
  `update`.
  *Breaks when:* the rebuild goes through `setRootScreen`, which clears the
  modal stack and emits `onRootScreenChanged`.

- **INV-4** — Every code in `supportedLanguages()` is accepted by `Settings`
  validation and has a table file under `assets/localization/`. Whether each
  table holds every key stays the audit's report-only check (§4.7).
  *Test:* `tests/test_localization_tables.cpp`.
  *Breaks when:* a language is added to the list without its table, or
  validation keeps its own list.

- **INV-5** — The primary UI face's glyph ranges include 0x00E9 (é), 0x00F1
  (ñ), 0x0153 (œ) and 0x2014 (—).
  *Test:* `tests/test_localization_tables.cpp`, against the named range
  constant.
  *Breaks when:* the ranges shrink back to ASCII.

- **INV-6** — `localization_audit.py` fails on a key-shaped literal under
  `ui.`, `subtitle.` or `input.` that `en.json` lacks, and on a literal passed
  to `makeLabel` or `makeButton`.
  *Test:* two new fixture directories, each with one defect and its own
  `WILL_FAIL` ctest beside `LocalizationAuditCatchesHardcoded`:
  `tests/fixtures/localization_audit_keys/` holds a key-shaped literal missing
  from its `en.json`, and `tests/fixtures/localization_audit_menu/` holds a
  `makeLabel("...")` literal. Separate from the existing fixture, which already
  fails on its own literal.
  *Breaks when:* either new check is skipped, or the key pattern does not
  match a key held in a table.

## 6. Failure modes

- A table missing a key: `tr` falls back to English, then to the key itself,
  as today.
- A pattern missing a slot its call fills: that value is not shown, and no
  check here catches it, so translators keep every slot (stated in
  `docs/localization/review.md`).

## 7. Tests

- `tests/test_localization_format.cpp` (new) — INV-1, INV-2.
- `tests/test_ui_system_screen_stack.cpp` — INV-3.
- `tests/test_localization_tables.cpp` (new) — INV-4, INV-5.
- The two new audit fixtures and their `WILL_FAIL` ctests — INV-6.
- Manual, in the app: choose French in the editor's Settings panel (the only
  language control today), enter play mode, and see the main and pause menus
  in French with accented letters rendered.

## 8. Alternatives considered (and rejected)

- **ICU MessageFormat or fmt-style positional `{0}`.** Named slots are
  readable to a translator and need no dependency; plural forms are already
  out of scope.
- **Make the coverage check strict in CI.** The localization design chose
  report-only so a new key need not wait for every translation; this change
  reads that report once to confirm the languages it ships are complete.

## 9. Out of scope

- Editor (ImGui) translation; input action and key names (§3).
- Plural forms, number and date formatting, right-to-left menu layout, and
  non-Latin scripts beyond Greek and Hebrew (the localization design's
  deferred list).
- Mock-up data on the menus (3D_E-0744).
- The words inside captions and subtitles: they come from the producer and
  the caption map's per-clip data, not from this change's keys.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1, INV-2 | `tests/test_localization_format.cpp` |
| INV-3 | `tests/test_ui_system_screen_stack.cpp` |
| INV-4, INV-5 | `tests/test_localization_tables.cpp` |
| INV-6 | the two new audit fixtures and their `WILL_FAIL` ctests |
| A translation says what the English says | **nothing** — `docs/localization/review.md` lists the languages no speaker has checked |
| The menus show the chosen language in the app | **nothing** automated; the manual check in §7 |

## 11. Cross-doc impact

- `CHANGELOG.md`: one entry.
- `docs/localization/review.md` (new): one row per language, reviewer and date.
- The ROADMAP item's "no new FontStack work" is corrected by this spec's §2.

## 12. Cold-eyes loop log

Rows live in `../reviews/3D_E-0024-interface-languages-loop-log.md`.
