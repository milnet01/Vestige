# 3D_E-0024 interface languages — review loop log

Written by `review-contract`, one row per loop, oldest first.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|---|---|---|---|---|---|---|---|
| 1 | 2026-10-02 | 2 (neutral-lane; each held every question) | 2 | 2 | 3 | 1 | Verified 8, fixed 8, dismissed 0. Q1: the language list had a third copy, the audit's default secondaries (now taken from the table files present); the audit's sink list lacks `makeLabel`/`makeButton`, the real reason the menus pass (now added). Q2: INV-4 as a CI test made coverage strict, which §8 and the design rejected (INV-4 now checks list, validation and table files only); §1 promised caption text the keys never reach (narrowed to caption framing, words out of scope). Q3: `actionVerb` key-or-text undefined (now a key, `tr` per build); the picker would lose its English names (`englishName` added); rebuilding inside the synchronous language handler could clear a canvas a click callback is iterating (from an open question; rebuild deferred to the next update). Q4: INV-2's test had no seam (now `ScopedStringTableOverride` under `VESTIGE_TEST_HOOKS` and pure compose functions). Open questions resolved clean: `Font::loadFromFile` skips a codepoint the face lacks (`FT_Get_Char_Index`); inter_tight.ttf is the face engine.cpp passes. Collateral: §8 still credited INV-4 with full coverage; corrected in the same pass. |
