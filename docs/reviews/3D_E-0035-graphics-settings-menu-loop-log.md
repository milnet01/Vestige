# 3D_E-0035 graphics settings menu — review loop log

Written by `review-contract`, one row per loop, oldest first.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|---|---|---|---|---|---|---|---|
| 1 | 2026-10-02 | 2 (neutral-lane; each held every question) | 2 | 2 | 0 | 2 | Verified 6, fixed 6, dismissed 0. Both lanes: INV-7's test could never see the engine's close-revert wiring (now one `wireSettingsScreen` function the engine and the test both call); the render-scale "step 5" disagreed with `keyStep`'s default of a twentieth of the range (now `keyStep` 5 %, drag continuous). Lane 1: Restore Defaults called `restoreDisplayDefaults`, which also resets the window size this page does not show (now a new `restoreGraphicsDefaults` keeping the size); INV-6's breaks-when named a renderer bypass its test cannot see (narrowed to what `applyGraphics` sends). Lane 2 plus both lanes' measurement: the tables live in `assets/localization/<code>.json`, not `supported_languages.h`; `LocalizationAuditStrict` checks English only, so the nine-language claim had no check (now INV-10 over `settingsPageKeys()`). Open questions resolved clean: `--no-vsync` overrides the window only, not the editor; `onModalPopped` emits the popped screen; the settings editor is always created; settings load from `Settings::defaultPath()`; `fromJson` passes the field default as fallback. Packet yield (1b): every citation windowed, none defective. Mechanical pass: `spec_lint` clean, paths checked by hand (surfaces not resolvable on this layout). |
