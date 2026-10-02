<!-- ants-spec-format: 1 -->
# 3D_E-0024 — Translatable in-game interface and five more languages

**Status:** draft (2026-10-02).
**Kind:** feature.
**Source:** ROADMAP 3D_E-0024 (user request 2026-07-04).

Layman: the menus and on-screen messages can be shown in French, German, Spanish, Italian and Brazilian Portuguese as well as English, Greek, Hebrew and Latin, and accented letters display properly.

## 1. Goal

Every piece of text a player sees in the running app's menus, prompts,
captions and overlays resolves through the string table, so it follows the
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

`tools/localization_audit.py` reports clean because it only flags a literal
passed straight to a text sink; a key held in a table, or a literal reaching a
sink through a variable, is invisible to it.

The roadmap item assumed Latin-script languages need no font work. They do:
`TextRenderer::initialize` loads the UI face with printable ASCII (0x20–0x7E)
and Greek only, so é, ü, ñ, ç and ã, and the em dash the input code already
prints for an unbound key, render as the fallback glyph. The bundled
`inter_tight.ttf` has those glyphs; they are not loaded.

Menus are built once per screen change. `LocalizationService::setLanguage`
publishes `LanguageChangedEvent`, which nothing in `UISystem` hears, so an open
menu keeps the old language until the screen changes.

The language list is written out twice — `Settings` validation
(`settings.cpp`) and the editor's picker (`settings_editor_panel.cpp`) — both
`{"en", "he", "el", "la"}`.

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
`struct SupportedLanguage { const char* code; const char* nativeName; }` and a
`supportedLanguages()` span: en English, el Ελληνικά, he עברית, la Latina,
fr Français, de Deutsch, es Español, it Italiano, pt-BR Português (Brasil).
`Settings` validation and the editor picker both read it. A table file is
`assets/localization/<code>.json`, so pt-BR's is `pt-BR.json`.

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

### 4.4 Keys

All new keys live in `en.json` first. Prefixes:

- `ui.menu.*`, `ui.pause.*`, `ui.settings.*` — the menu labels, the pause
  buttons and the settings categories. A settings category's number prefix
  ("01  ") stays outside the key, joined in code.
- `ui.keybind.press_key`, `ui.hud.fps` (`"{fps} FPS"`),
  `ui.slider.percent` (`"{value} %"`).
- `ui.prompt.interact` (`"Press [{key}] to {action}"`) and
  `ui.prompt.verb.use` for the default verb.
- `subtitle.speaker_line` (`"{speaker}: {text}"`) and `subtitle.sound_cue`
  (`"[{text}]"`).

Proper nouns and the product name ("Vestige") are not keyed.

### 4.5 Language change rebuilds the open screens

`UISystem` subscribes to `LanguageChangedEvent` at initialisation and, on it,
rebuilds the root canvas and the top modal with their current builders. It
does not emit `onRootScreenChanged`, `onModalPushed` or `onModalPopped`,
since no screen changed. The subscription is released in `shutdown`.

### 4.6 The lint

`localization_audit.py` gains a check: every string literal in `engine/`
shaped like a key under the `ui.`, `subtitle.` or `input.` prefixes must exist
in `en.json`. This catches keys held in tables, which the `tr("...")` pattern
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
  *Test:* `tests/test_localization_format.cpp`, with a test string table whose
  pattern puts `{action}` before `{key}`.
  *Breaks when:* a widget still concatenates English around the values.

- **INV-3** — On `LanguageChangedEvent`, `UISystem` rebuilds the open root
  screen and the top modal once each, and fires none of its screen-change
  signals.
  *Test:* `tests/test_ui_system_screen_stack.cpp`, with counting builders
  and signal listeners.
  *Breaks when:* the rebuild goes through `setRootScreen`, which clears the
  modal stack and emits `onRootScreenChanged`.

- **INV-4** — Every code in `supportedLanguages()` is accepted by `Settings`
  validation, has a table file under `assets/localization/`, and that table
  holds every key in `en.json`.
  *Test:* `tests/test_localization_tables.cpp`.
  *Breaks when:* a language is added to the list without its table, or a key
  is added to `en.json` alone.

- **INV-5** — The primary UI face's glyph ranges include 0x00E9 (é), 0x00F1
  (ñ), 0x0153 (œ) and 0x2014 (—).
  *Test:* `tests/test_localization_tables.cpp`, against the named range
  constant.
  *Breaks when:* the ranges shrink back to ASCII.

- **INV-6** — `localization_audit.py` fails on a key-shaped literal under
  `ui.`, `subtitle.` or `input.` that `en.json` lacks.
  *Test:* a new fixture directory, `tests/fixtures/localization_audit_keys/`,
  whose only defect is such a literal, run by a new `WILL_FAIL` ctest beside
  `LocalizationAuditCatchesHardcoded`. A separate fixture, because the
  existing one already fails on its hardcoded literal.
  *Breaks when:* the new check is skipped or its pattern does not match a key
  in a table.

## 6. Failure modes

- A table missing a key: `tr` falls back to English, then to the key itself,
  as today.
- A pattern missing a slot its call fills: that value is not shown; INV-4's
  check against `en.json` keys does not catch it, so translators keep every
  slot (stated in `docs/localization/review.md`).

## 7. Tests

- `tests/test_localization_format.cpp` (new) — INV-1, INV-2.
- `tests/test_ui_system_screen_stack.cpp` — INV-3.
- `tests/test_localization_tables.cpp` (new) — INV-4, INV-5.
- The new audit fixture and its `WILL_FAIL` ctest — INV-6.
- Manual, in the app: switch to French in Settings with the main menu open,
  see it change, and see accented letters render.

## 8. Alternatives considered (and rejected)

- **ICU MessageFormat or fmt-style positional `{0}`.** Named slots are
  readable to a translator and need no dependency; plural forms are already
  out of scope.
- **Make the coverage check strict in CI.** The localization design chose
  report-only so a new key need not wait for every translation; INV-4 checks
  the languages this change ships complete.

## 9. Out of scope

- Editor (ImGui) translation; input action and key names (§3).
- Plural forms, number and date formatting, right-to-left menu layout, and
  non-Latin scripts beyond Greek and Hebrew (the localization design's
  deferred list).
- Mock-up data on the menus (3D_E-0744).

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1, INV-2 | `tests/test_localization_format.cpp` |
| INV-3 | `tests/test_ui_system_screen_stack.cpp` |
| INV-4, INV-5 | `tests/test_localization_tables.cpp` |
| INV-6 | the new audit fixture and its `WILL_FAIL` ctest |
| A translation says what the English says | **nothing** — `docs/localization/review.md` lists the languages no speaker has checked |
| The open screen changes language in the app | **nothing** automated; the manual check in §7 |

## 11. Cross-doc impact

- `CHANGELOG.md`: one entry.
- `docs/localization/review.md` (new): one row per language, reviewer and date.
- The ROADMAP item's "no new FontStack work" is corrected by this spec's §2.

## 12. Cold-eyes loop log

Rows live in `../reviews/3D_E-0024-interface-languages-loop-log.md`.
