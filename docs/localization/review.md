# Interface translations — review status

The in-game interface strings live in `assets/localization/<code>.json`,
one table per language in `engine/localization/supported_languages.h`.
`en.json` is the reference: every key starts there.

## Who has checked each language

| Language | Code | Written by | Checked by a native speaker |
|---|---|---|---|
| English | `en` | project | reference |
| Greek | `el` | Claude, 2026-10-02 (menu buttons earlier) | not yet |
| Hebrew | `he` | Claude, 2026-10-02 (menu buttons earlier) | not yet |
| Latin | `la` | Claude, 2026-10-02 (menu buttons earlier) | not yet |
| French | `fr` | Claude, 2026-10-02 | not yet |
| German | `de` | Claude, 2026-10-02 | not yet |
| Spanish | `es` | Claude, 2026-10-02 | not yet |
| Italian | `it` | Claude, 2026-10-02 | not yet |
| Portuguese (Brazil) | `pt-BR` | Claude, 2026-10-02 | not yet |

When a speaker checks a language, put their name or handle and the date in
the last column.

## Rules for a translator

- **Keep every `{slot}`.** A pattern such as `Press [{key}] to {action}` is
  filled in by the engine. Move the slots to suit the language's word order,
  but do not drop or rename one: a missing slot means that value never shows,
  and no check catches it.
- **`{{` and `}}` write a single brace** if a translation needs a literal one.
- **Keep key names that are keys on the keyboard** (`ESC`, `ENTER`) unless the
  language normally names them differently on keyboards sold there.
- **`ui.prompt.verb.*` values are slotted into `ui.prompt.interact`**, so
  write each verb in the form that pattern needs (for example an infinitive).

## Checking coverage

`python3 tools/localization_audit.py --lint` lists, per language, any key in
`en.json` that the table lacks. Missing keys fall back to English at run time.
