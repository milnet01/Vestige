# Phase 10 Localization — i18n + RTL + Multi-script Text Rendering Design

## Status

**Reviewer decisions locked 2026-06-06** (§ 12 answered) — Revision 3.
**Cold-eyes: 5 loops run 2026-06-06, converged** — every CRITICAL / HIGH /
MEDIUM finding fixed (incl. a missing `Font::hasGlyph` design gap and the
Frank Ruehl CLM → Frank Ruhl Libre licence correction); remaining surface
is LOW wording polish. Ready for implementation (start at slice L1) per
project `CLAUDE.md` rules 1, 8, 9 + global `~/.claude/CLAUDE.md` rule 14
(research → design → review → cold-eyes → code; a new bundled-font
dependency was added in this revision so the dependency-review gate applied).

**L2 prerequisite before any code on that slice:** commit
`assets/fonts/frank_ruhl_libre.ttf` (OFL) + add the fifth row to
`ASSET_LICENSES.md` and flip `THIRD_PARTY_NOTICES.md`'s "four"→"five"
(§ 7 "Bundling precondition").

The four blocking questions in § 12 were answered by the reviewer:

1. **Missing-key behaviour** — *active → English → key* (silent English
   fallback). § 5.6 already specified this; no change.
2. **Settings field placement** — its **own top-level `Localization`
   group**, not folded into Accessibility. § 2 L5 already specifies the
   `Settings::Localization` group; now locked, and § 2 L5 reworded so
   the picker UI is its own Language section rather than an Accessibility
   tab.
3. **Hebrew font for v1** — **bundle a dedicated biblical-Hebrew serif
   now**, as a real second font in the `FontStack`. The reviewer first
   chose Frank Ruehl CLM; on verification that font is **plain GPLv2
   with no font-embedding exception** (Culmus / Maxim Iorsh families —
   the exception in the Culmus LICENSE attaches only to the Yoram Gnat
   families), which is unsafe to bundle in a commercially-bound engine.
   Substituted with **Frank Ruhl Libre** (Yanek Iontef, Google Fonts
   2016) — the OFL-1.1 open-source revival of the *same* Frank Rühl
   typeface. OFL matches the project's existing font licences
   (Arimo, Cormorant Garamond, Inter Tight, JetBrains Mono — all four
   existing bundled fonts are OFL). This makes the default
   stack a genuine **2-font** stack (Arimo for Latin+Greek, Frank Ruhl
   Libre for Hebrew) — see § 2 L2, § 5.3, § 7, § 9.
4. **CI audit strictness** — **strict from day one**, with the scope
   pinned in § 5.7: the *hardcoded-literal* check and the
   *reference-language (en.json) missing-key* check both fail the build;
   the *secondary-language* (he/el/la) coverage check stays report-only,
   because runtime English-fallback (decision 1) makes a not-yet-
   translated secondary string a non-bug. (This scoping of "strict" is
   the implemented interpretation; it can be tightened to gate
   secondary-language completeness later without a redesign.)

Revision 2 folded in cold-eyes review findings 2026-05-18. Closes four
roadmap bullets in the Phase 10 → Localization section of `ROADMAP.md`:

1. Multi-language text support (UTF-8, language selection).
2. Translatable string table system (all UI/plaque text referenced
   by key, not hardcoded).
3. Hebrew, Greek, and Latin text rendering (right-to-left support
   for Hebrew).
4. Language selection in settings menu.

These four are tightly coupled — the string table is foundational
for the language selector; the language selector is moot without
multi-language support; multi-language support is moot if the font
stack only renders Latin. They ship as one coherent design that
slices into 6 commits (§ 2).

This is also the **prerequisite** for the Phase 10 Features bullet
"Information plaques" (the in-world text panels that explain
biblical artefacts) — those plaques are the primary i18n consumer
once Localization lands; the `PlaqueComponent` itself is tracked
separately.

---

## Contents

- [1. Goals](#1-goals)
- [2. Scope Split — what ships when](#2-scope-split--what-ships-when) — the L1–L6 slice table
- [3. CPU / GPU placement](#3-cpu--gpu-placement-project-claudemd-rule-7)
- [4. Inventory — what exists, what's missing](#4-inventory--what-exists-whats-missing)
- [5. API design](#5-api-design) — utf8 · font/FontStack · rtl · StringTable · LocalizationService · audit
- [6. Out of scope](#6-out-of-scope-explicit-non-goals)
- [7. Dependencies](#7-dependencies) — incl. the Frank Ruhl Libre (OFL) bundle + licence-ledger edits
- [8. Verify-step plan](#8-verify-step-plan-global-claudeclaudemd-rule-12) — the 23-test table
- [9. Performance budget](#9-performance-budget--estimates-with-measurement-plan)
- [10. Backwards compatibility](#10-backwards-compatibility--precise-scope)
- [11. Alternatives considered](#11-alternatives-considered)
- [12. Open questions — RESOLVED](#12-open-questions-for-the-reviewer--resolved-2026-06-06)
- [13. Implementation order](#13-implementation-order-suggested)
- [14. References](#14-references)

---

## 1. Goals

- Render UI / world text in Latin, Greek (polytonic), and Hebrew
  from the same call sites that today render English-only ASCII.
- Switch language at runtime (no restart) from the existing
  Settings menu.
- Author all user-facing strings as keys (`"plaque.tabernacle.title"`),
  with values living in `assets/localization/<lang>.json` — never
  hardcoded.
- Hebrew renders right-to-left, with each Hebrew run reversed into
  visual order before glyph emission (full Unicode BiDi UAX#9 is
  **out of scope**; see § 6).
- HUD-pass budget: today's pass is estimated at ~0.14 ms / frame
  (~144 µs — see the § 9 table)
  for a typical workload (no benchmark exists today — see § 9 for
  the harness scaffolding plan in slice L6); the i18n addition
  must keep within **≤ 0.30 ms / frame** for the same workload
  *measured against the same harness*. The formal benchmark and budget
  gate (§ 8 test 23, `tests/test_text_renderer_perf.cpp`) land in **L6**,
  so earlier slices can't "regress" what doesn't exist yet — each earlier
  slice instead records a lightweight ad-hoc timing probe of the work it
  adds, which L6's test 23 later supersedes as the pinned gate.
- Atlas-memory budget: see § 9 for estimated numbers (pinned in L2 by
  test 6). Two bundled
  fonts cover the three scripts — Arimo (already on disk) for Latin
  + Greek, and **Frank Ruhl Libre** (added this revision, § 7) for
  Hebrew, each in its own `Font` instance / atlas. The combined
  multi-script atlas footprint grows from 1 MB (today, 95 Latin
  glyphs) to an estimated ~3.8 MB (≈ 4 MB ceiling) at full coverage
  (~485 glyphs across the three scripts at 48 px pixel-size; see the
  § 9 memory table for the per-atlas breakdown).
- Zero behaviour change for existing English-only call sites:
  scenes / panels / labels that don't use the string-table API
  keep rendering today's literal `std::string` text through the
  same code paths. (Narrower claim than rev 1; see § 10 for the
  precise scope of the compat guarantee.)

---

## 2. Scope Split — what ships when

Six slices, ordered by dependency. Slice IDs follow the per-roadmap
ID pattern used by other Phase-10 design docs — `L1` … `L6` for
**L**ocalization. Each lands independently and unblocks later ones.
The cold-eyes loop runs per-slice on each diff.

| Slice | Title | Complexity | Ships |
|-------|-------|------------|-------|
| **L1** | UTF-8 decoder + codepoint Font API | M | `engine/utils/utf8.{h,cpp}` (pure-function decoder), Font glyph map keyed by `uint32_t`, the new `Font::hasGlyph(uint32_t)` presence query (§ 5.2) + `GlyphInfo::operator==` (so § 8 test 5 can field-compare), `text_renderer.cpp` glyph loops at lines 249 / 354 / 478 / 549 switched to UTF-8 walk. Plus a lightweight ad-hoc timing probe so the work has a baseline (the formal HUD-pass benchmark, § 8 test 23, lands in L6). Backwards compatible for ASCII inputs: identical output, see § 10 for precise scope. |
| **L2** | Font fallback chain + glyph-range expansion | M | `FontStack` class — ordered list of Font instances. `getGlyph(codepoint)` walks the list until a font claims the codepoint. Default stack contains **two fonts**: Arimo loaded with Latin + Greek ranges, and **Frank Ruhl Libre** loaded with the Hebrew range (§ 7, reviewer decision 3 — a dedicated biblical-Hebrew serif). **Prerequisite:** `frank_ruhl_libre.ttf` must be committed to `assets/fonts/` + the two licence ledgers updated (§ 7 "Bundling precondition") before this slice lands. The MRU cache (§ 5.3) keeps the per-glyph cost at baseline for pure-script strings (the common case: an all-Hebrew plaque, an all-Latin label). The stack abstraction also lets future per-script swaps land without re-plumbing call sites. |
| **L3** | Right-to-left logical→visual reorder | S | `engine/utils/rtl.{h,cpp}` — per-run reversal for pure-Hebrew strings (no BiDi algorithm). `TextRenderer` calls the reorder before glyph emission. |
| **L4** | String table + Localization service | M | `engine/localization/string_table.{h,cpp}` (JSON loader, key→value, miss reporting), `LocalizationService` ISystem wrapper, `assets/localization/{en,he,el,la}.json` initial files, plus the SystemRegistry registration + `LanguageChangedEvent` on the existing event bus. |
| **L5** | Settings integration + language dropdown | S | Bump `kCurrentSchemaVersion` 2 → 3 in `settings.h`; add `Settings::Localization { language: "en" }`; add `migrate_v2_to_v3(json&)` in `settings_migration.{h,cpp}` — **it must set `j["schemaVersion"] = 3`** (mirroring `migrate_v1_to_v2`'s `= 2` at `settings_migration.cpp:83`); if it forgets, the migrate loop's non-advancement guard (`settings_migration.cpp:54` — `if (next <= version) return false`) logs "version did not advance" and `migrate()` aborts with `false`, so the v2→v3 bump silently fails to apply rather than looping forever; add the language-picker UI under its **own `Language` section** of the Settings menu (reviewer decision 2 — the field is its own top-level `Settings::Localization` group, surfaced as a dedicated Language section, *not* folded into Accessibility). **Owns the v2 → v3 schema bump (locked 2026-06-01, CE4):** the v1 → v2 bump (onboarding) already shipped via Phase 10.5; this slice is the next bump in sequence and fills the commented `// case 2:` placeholder in `settings_migration.cpp`. |
| **L6** | Editor / debug tooling + HUD-pass benchmark | M | Editor "missing keys" overlay; CMake-driven `tools/localization_audit.py` — **strict by default** (reviewer decision 4): fails CI on any hardcoded user-visible string and on any `tr()` key absent from the reference `en.json`; secondary-language coverage stays report-only (§ 5.7). **`tests/test_text_renderer_perf.cpp`** — a 20-label / 800-glyph HUD-pass benchmark that pins the § 1 budget as a ctest target. The benchmark is the gate every prior slice's perf claim is measured against. |

Each slice is independently mergeable and tested. The shape mirrors
the per-slice cadence used by Phase 10 Fog (slices 11.1 – 11.10). The
reference language is always loaded, so partial
deployment (only `en.json` shipped) never produces visible blanks.

---

## 3. CPU / GPU placement (project `CLAUDE.md` Rule 7)

Every piece of this subsystem runs on the **CPU**:

- **UTF-8 decode** — sequential byte-level state machine, per-glyph
  granularity. Estimated cost ~10 ns / codepoint (one branch + one
  comparison + one mask), based on the Hoehrmann DFA-style decoder
  (see § 14 reference [Hoehrmann]). To be measured in L1 — labelled
  as estimate until the L6 benchmark confirms it.
- **Codepoint → glyph lookup** — `std::unordered_map<uint32_t, GlyphInfo>`.
  Memory layout and miss rate match the existing `char`-keyed map;
  same constant-factor performance class. Measured cost from the
  L6 benchmark replaces the estimate.
- **RTL run reversal** — `std::reverse` over a `std::vector<uint32_t>`,
  one pass per string. Bounded by string length (plaque titles are
  ≤ 32 codepoints; plaque body text is ≤ 256 codepoints per
  paragraph). Trivially CPU.
- **String-table lookup** — `std::unordered_map<std::string, std::string>`,
  called once per UI string per **panel rebuild**, not per frame
  (see § 5.6 for the caching contract). CPU.
- **JSON parsing** — only at boot + language-change, not per-frame.

The existing atlas texture upload (one-time, at font init) is the
only GPU touch and is unchanged. No new GPU work is introduced.

This placement is inherited from the existing text renderer (which
is correctly CPU-side for string-shaping, GPU-side for the quad
emission) and is right.

**TODO: revisit via Formula Workbench** — the per-codepoint UTF-8
decode cost and the per-string RTL-reverse cost are first-cut
estimates. After L6 benchmark data exists, a Workbench fit of
"cost vs. string length" replaces these prose numbers with a
documented model. Per project `CLAUDE.md` rule 6.

---

## 4. Inventory — what exists, what's missing

Surveyed against the current source on 2026-05-18:

### Existing (verified by grep + fc-query)

| Area | Location | State |
|---|---|---|
| **FreeType integration** | `engine/renderer/font.cpp::loadFromFile` lines 85-269 | Complete. `FT_Init_FreeType` / `FT_New_Face` / `FT_Set_Pixel_Sizes`. Pinned via `external/CMakeLists.txt:140-152` (`FetchContent_Declare(freetype GIT_TAG VER-2-13-3)`). |
| **Glyph atlas** | `engine/renderer/font.cpp` second pass (line 198) | Complete. Single-channel `GL_R8`, immutable storage via DSA. Atlas-packing is a single-shelf packer with naïve row-wrap at 2048 px (`font.cpp:169-188`) — fine for the ~95 ASCII glyphs but needs a real 2D shelf/skyline packer for the ~485 multi-script glyphs (L2 covers this; see the § 9 memory table). |
| **Text rendering** | `engine/renderer/text_renderer.cpp` lines 249, 354, 478, 549 | 4 glyph loops, all `for (char c : text)` over `std::string`. Calls `m_font.getGlyph(c)`. L1 must edit all 4 sites to walk UTF-8 codepoints instead. Batched (`beginBatch2D` / `endBatch2D`) — Phase 10.9 Pe1. Italic-oblique shear — Phase 10.9 P6. |
| **`Font::getGlyph`** | `engine/renderer/font.h:50` | `const GlyphInfo& getGlyph(char codepoint) const;` — `char` overload only; can't address codepoints beyond U+00FF. |
| **`Font` glyph map** | `engine/renderer/font.h:80` | `std::unordered_map<char, GlyphInfo>`. L1 swaps the key type to `uint32_t`. |
| **UI text consumers** | `engine/ui/{ui_label,ui_world_label,ui_keybind_row}.h`, subtitle renderer, FPS counter, toast notifications | All consume `std::string text` directly. Today's strings come from C++ literals at call sites. |
| **`ISystem` contract** | `engine/core/i_system.h:88-100` | Four pure virtuals: `getSystemName() const` (:88), `initialize(Engine&)` (:93), `shutdown()` (:96), `update(float)` (:100). `LocalizationService` must implement all four (see § 5.6 — it does). |
| **System lookup pattern** | `engine/core/system_registry.h` (`SystemRegistry::getSystem<T>()`) | The project does not use a free-function service locator. The free-function `tr()` in § 5.6 forwards via the registry, not a separate locator file. |
| **Bundled fonts (fc-query 2026-05-18)** | `assets/fonts/` | `arimo.ttf` — Liberation-Sans-compatible, ships with Latin + 87/88 Hebrew (U+0590..U+05FF) + 127/144 basic Greek (U+0370..U+03FF) + 233/256 polytonic Greek (U+1F00..U+1FFF). `cormorant_garamond.ttf` — Latin only (no Greek, no Hebrew, despite the typeface's reputation). `inter_tight.ttf` — Latin + basic Greek. `jetbrains_mono.ttf` — Latin + basic Greek. |
| **Settings system** | `engine/core/settings.h:64` (`kCurrentSchemaVersion = 2`), `settings_migration.h:64` (`migrate_v1_to_v2`) | Complete (Phase 10 Settings — slice 13.5e). JSON-backed, atomic-write, runtime live-apply. Schema versioning exists; new fields land via `migrate_vN_to_vN+1`. |
| **JSON loading** | `engine/utils/json_size_cap.h` (`JsonSizeCap::loadJsonWithSizeCap`) | Canonical helper. Localization files are small (estimated ≤ 32 KB even at 1000 keys × 32-byte values) so the default cap is fine. **Estimate** — pinned by the slice-L4 acceptance test (test #13) once real content exists. |
| **Path sandbox** | `engine/utils/path_sandbox.h` | Localization JSON loaded from `assets/localization/`; the sandbox roots already cover this path. No change needed. |
| **Event bus** | `engine/core/event_bus.h` | Existing publish/subscribe. `LanguageChangedEvent` is one new event type on it (no new bus needed). |

### Missing (the work this design covers)

1. **UTF-8 decoder.** Today `for (char c : text)` truncates
   multi-byte sequences into garbage. Every text consumer is
   broken for any non-ASCII codepoint.
2. **Codepoint-keyed glyph map.** `Font::getGlyph(char)` can't
   address codepoints beyond U+00FF.
3. **Font fallback / multi-range glyph loading.** The design loads
   Latin + Greek from Arimo and Hebrew from Frank Ruhl Libre (§ 7),
   but `loadFromFile` hard-loops ASCII 32-126 (see `font.cpp:128`)
   and there is no fallback chain across fonts. Loading additional
   ranges + the `FontStack` chain is the L2 work.
4. **RTL handling.** No reverse-on-render path; Hebrew strings
   would render left-to-right in logical (memory) order, which is
   visually wrong.
5. **String table.** No key→value lookup; every user-visible
   string is a C++ literal at the call site.
6. **Language selection.** Settings carries no language field; the
   menu has no language picker.
7. **Schema migration v2 → v3.** Bumping `kCurrentSchemaVersion`
   without a `migrate_v2_to_v3` defaults pre-rev3 files into a
   silent zero-language state.
8. **Atlas packing for ~485 glyphs.** The single-shelf row-wrap
   packer (`font.cpp:169-188`) is fine for 95; for the full
   multi-script set it needs a real 2D shelf/skyline packer to fit a
   square power-of-two texture without wasting > 50 % space.
9. **HUD-pass benchmark.** No `test_text_renderer_perf.cpp` or
   equivalent exists today. The closest is
   `tests/test_text_rendering.cpp` (correctness only) and
   `tests/test_benchmark.cpp` (Formula Workbench-only, see header
   comment). L6 scaffolds the benchmark.

Slices L1 – L6 close these gaps in order.

---

## 5. API design

### 5.1 `engine/utils/utf8.h` (slice L1)

```cpp
namespace Vestige::utf8
{

/// Decode result for one codepoint.
struct DecodeResult
{
    uint32_t codepoint;   // 0xFFFD on invalid sequence
    int      bytesRead;   // 1..4; always ≥1 so callers can advance
};

/// Decode the codepoint at byte offset `pos` in `s`.
/// On invalid input, returns {0xFFFD (replacement char), 1}.
DecodeResult decodeAt(std::string_view s, size_t pos);

/// Decode an entire string into a codepoint vector.
/// Convenience wrapper around `decodeAt`. Useful at call sites
/// that need random access (RTL reversal) rather than a
/// stream-style walk.
std::vector<uint32_t> decode(std::string_view s);

/// Encode a single codepoint into 1-4 bytes appended to `out`.
/// Returns the number of bytes written. Codepoints > U+10FFFF are
/// clamped to U+FFFD (replacement character).
int encode(uint32_t codepoint, std::string& out);

/// True iff `cp` belongs to the Hebrew block (U+0590..U+05FF or
/// the Hebrew Presentation Forms block U+FB1D..U+FB4F).
bool isHebrew(uint32_t cp);

/// True iff `cp` belongs to the Greek block (U+0370..U+03FF) or
/// Greek Extended (U+1F00..U+1FFF) — polytonic glyphs live in the
/// latter.
bool isGreek(uint32_t cp);

} // namespace Vestige::utf8
```

**Invariants** (tests in `tests/test_utf8.cpp`):

- **UTF8-INV-1** — `decodeAt(s, 0).bytesRead ≥ 1` for any non-empty `s`.
  Guarantees forward progress; callers can advance unconditionally.
- **UTF8-INV-2** — `decodeAt` over a valid UTF-8 string visits
  every codepoint exactly once when `pos += bytesRead` is iterated.
- **UTF8-INV-3** — `decode("")` → empty vector, no allocation.
- **UTF8-INV-4** — Invalid byte sequences emit `U+FFFD` and
  advance exactly 1 byte (matches the Unicode TR-36 "best practices
  for replacement"; see § 14 reference [UnicodeTR36]).
- **UTF8-INV-5** — `decode(encode_all(decode(s))) == decode(s)` for
  all valid UTF-8 strings (round-trip via the codepoint
  representation). The `encode_all` helper is the test-side
  convenience that calls `encode` for each codepoint.

### 5.2 `engine/renderer/font.h` (slice L1 + L2)

Two surface changes. The struct precedes every use of it.

```cpp
struct CodepointRange { uint32_t firstInclusive; uint32_t lastInclusive; };

inline const std::vector<CodepointRange> ASCII_RANGE   = {{0x0020, 0x007E}};
inline const std::vector<CodepointRange> LATIN1_RANGE  = {{0x0020, 0x007E}, {0x00A0, 0x00FF}};
inline const std::vector<CodepointRange> GREEK_RANGES  = {{0x0370, 0x03FF}, {0x1F00, 0x1FFF}};
inline const std::vector<CodepointRange> HEBREW_RANGE  = {{0x0590, 0x05FF}};

// BEFORE (slice L0 baseline)
//   const GlyphInfo& getGlyph(char codepoint) const;
//   bool loadFromFile(const std::string& filePath, int pixelSize = 48);

// AFTER (slice L1)
const GlyphInfo& getGlyph(uint32_t codepoint) const;
bool loadFromFile(const std::string& filePath, int pixelSize = 48,
                  const std::vector<CodepointRange>& ranges = ASCII_RANGE);

// NEW glyph-presence query (slice L1) — distinguishes "this font has a
// real glyph for cp" from "getGlyph would return the fallback '?'".
// `FontStack::lookup` (§ 5.3) and § 8 tests 7 & 8 are built on it
// (test 7 asserts per-script routing, test 8 the miss fallback);
// the current Font has no such primitive (getGlyph silently returns the
// fallback on a miss), so it must be added here.
bool hasGlyph(uint32_t codepoint) const;   // true iff m_glyphs.count(cp)
// Population contract: loadFromFile inserts an m_glyphs entry ONLY for
// codepoints the face actually rasterised — it does NOT pad the map with
// fallback entries for requested-but-absent codepoints. So hasGlyph(cp)
// means "this face has a real glyph for cp", which is exactly what
// FontStack::lookup (§ 5.3) and § 8 tests 7 & 8 require.

// Lifetime contract on the GlyphInfo reference: valid until the
// next loadFromFile() call on the same Font instance. Same lifetime
// as the slice-L0 baseline.
```

The default `ASCII_RANGE` keeps the existing call sites byte-for-byte
identical to today — the `loadFromFile(path, 48)` two-arg form picks
up the default and loads exactly the ASCII range the current code
loads.

**Char-overload shim (slice L1 only).** A 1-line inline forwarder
keeps the existing ~10 callers compiling across L1 → L2:

```cpp
// Slice L1 only — removed in L2 after callers migrate.
const GlyphInfo& getGlyph(char c) const
{ return getGlyph(static_cast<uint32_t>(static_cast<unsigned char>(c))); }
```

The double-cast is intentional — `char` is signed on many platforms;
the `unsigned char` step prevents sign-extension turning 0x80..0xFF
into negative `uint32_t` values.

`std::unordered_map<char, GlyphInfo>` becomes
`std::unordered_map<uint32_t, GlyphInfo>` in L1. ABI note: no
external consumer holds a reference to this map today (grep
`m_glyphs` confirms — only `Font::getGlyph` reads it). The map's
iteration order is not stable in either form, so no test that
compares iteration order can exist (none do).

### 5.3 `engine/renderer/font_stack.h` (slice L2)

```cpp
namespace Vestige
{

/// Ordered fallback chain — first font that has a glyph for the
/// codepoint wins. Per-script atlases live in their own Font instance,
/// so each script can be loaded / unloaded independently of the others.
class FontStack
{
public:
    /// Add a font to the back of the chain. Search order is insertion
    /// order. Default stack (constructed in TextRenderer::initialize)
    /// holds two fonts: Arimo loaded with Latin + Greek ranges, then
    /// Frank Ruhl Libre loaded with the Hebrew range (the biblical-
    /// Hebrew serif — § 7). The class also lets future per-script swaps
    /// (e.g. a different Greek face) land without re-plumbing call sites.
    void addFont(std::shared_ptr<Font> font);

    /// Locate the (font, glyph) pair for a codepoint. Returns a
    /// raw Font* whose lifetime is "until the next addFont/clear
    /// call on this stack" — same guarantee as a vector::front()
    /// pointer survives until the next vector mutation. The
    /// TextRenderer's default stack is built once in
    /// TextRenderer::initialize() and is NOT mutated during rendering,
    /// so `Hit::font` raw pointers are stable for the whole frame.
    struct Hit { Font* font; const GlyphInfo* glyph; };

    /// First font that covers `codepoint` wins. If nothing covers it,
    /// returns `Hit{ first font, that font's fallback "?" glyph }`
    /// (the "?" mark from `font.cpp:241-245`). On a miss `Hit::font` is the
    /// FIRST font in the stack (never null) — the fallback glyph lives
    /// in that font's atlas, so the caller can bind the atlas texture
    /// unconditionally. Pinned by § 8 test 8 (asserts both glyph AND
    /// font on a miss).
    Hit lookup(uint32_t codepoint) const;

private:
    std::vector<std::shared_ptr<Font>> m_fonts;
};

} // namespace Vestige
```

**Performance contract.** The naïve "walk the stack on every glyph"
cost is O(stacks × stringLen) per render. The default stack now holds
**two fonts** (Arimo Latin+Greek, Frank Ruhl Libre Hebrew — reviewer
decision 3), so the per-glyph hop can cost one extra `unordered_map::find`
when a font misses the codepoint. The text-renderer side maintains a
one-element MRU cache (`m_lastHitFont`) — when the same font claims two
adjacent glyphs (~99 % of cases for natural text, where script
boundaries are at most a handful per line), the cache skips the stack
walk. Because shipped strings are overwhelmingly pure-script (an
all-Hebrew plaque, an all-Latin label), the MRU cache holds and the
2-font default costs the same as the slice-L0 baseline per glyph.

The cost numbers below are *estimates* pending the L6 benchmark:

- **2-font stack, pure-script string (all-Latin OR all-Hebrew — the
  shipped case):** zero overhead vs. baseline with the MRU cache — every
  glyph after the first hits the cached font (one `unordered_map::find`
  per glyph, same as L0). Without the cache: 1 extra `unordered_map::find`
  per glyph.
- **2-font stack, alternating-script string** (worst case): 2 extra
  `unordered_map::find` per glyph — pathological for documents that
  alternate every codepoint, not realistic for plaque text.

The L6 benchmark pins these (§ 8 test 9 for the MRU cache, test 23 for
the HUD-pass budget). If the worst case is
within budget, no further optimisation needed.

### 5.4 `engine/utils/rtl.h` (slice L3)

Lightweight scope: per-string check, per-run reverse. **Not** the
full Unicode UAX#9 BiDi algorithm; that's a multi-thousand-line
state machine that pulls in ICU. For the project's audience
(biblical-Hebrew plaques + UI strings that are pure-Hebrew or
pure-Latin, never mixed-Hebrew-with-numerals), the lightweight
approach is sufficient. Mixed-script strings with embedded numerals
have a documented wrong-rendering case (§ 6 deferral 1).

```cpp
namespace Vestige::rtl
{

/// Visual order = the order glyphs appear left-to-right on screen.
/// Logical order = the order codepoints appear in memory (the order
/// the user types them).
///
/// For a pure-LTR string, visual == logical.
/// For a pure-RTL string, visual is `reverse(logical)`.
/// For mixed-script strings, each RTL run is reversed in place while
/// LTR runs are left alone — correct for pure-script content, wrong
/// for mixed-script strings that need full UAX#9 BiDi (out of scope).
std::vector<uint32_t> toVisualOrder(const std::vector<uint32_t>& logical);

/// True iff the string contains any RTL codepoint (Hebrew today;
/// Arabic etc. can be folded in later by extending the range set).
bool containsRTL(const std::vector<uint32_t>& cps);

} // namespace Vestige::rtl
```

Tests (`tests/test_rtl.cpp`):

- **RTL-INV-1** — pure-Latin input passes through unchanged.
- **RTL-INV-2** — pure-Hebrew input is reversed.
- **RTL-INV-3** — mixed `"Hello שלום"` reverses only the Hebrew
  run, leaves "Hello " intact. Documents the limitation: this is
  *not* full BiDi; bidirectional numeric and punctuation rules
  are not implemented.
- **RTL-INV-4** — empty input → empty output (no crash).

### 5.5 `engine/localization/string_table.h` (slice L4)

```cpp
namespace Vestige
{

/// Key→value lookup table loaded from a JSON file per language.
/// Keys are dot-delimited paths ("plaque.tabernacle.title");
/// values are UTF-8 strings.
class StringTable
{
public:
    /// Load a localization JSON file from `path`. Returns false on
    /// missing file, parse error, or oversized payload (`JsonSizeCap`
    /// default applies). On reload, replaces the current map.
    bool loadFromFile(const std::string& path);

    /// Lookup. Missing keys ALWAYS return the key itself (every build —
    /// pins § 8 test 14). Additionally, when built with the
    /// `VESTIGE_LOCALIZATION_WARN_MISSING` CMake option (set by default
    /// in debug builds), the missing key is logged via `Logger::warning`
    /// exactly once per session per key (no per-frame spam). Release
    /// builds omit only the *warning* path to keep the hot path
    /// branchless — the key-return behaviour is unchanged.
    std::string_view get(std::string_view key) const;

    /// True iff the table has a value for this key.
    bool contains(std::string_view key) const;

    /// Number of loaded keys. Used by the editor "missing keys"
    /// overlay (§ 5.7).
    size_t size() const;

private:
    std::unordered_map<std::string, std::string> m_map;
#if defined(VESTIGE_LOCALIZATION_WARN_MISSING)
    mutable std::unordered_set<std::string> m_loggedMisses;
#endif
};

} // namespace Vestige
```

**JSON schema** — flat key→value:

```json
{
  "ui.menu.start_game": "Start Game",
  "ui.menu.settings": "Settings",
  "plaque.tabernacle.title": "The Tabernacle",
  "plaque.tabernacle.body": "A portable sanctuary..."
}
```

Flat (no nesting) is chosen over hierarchical to keep the loader
trivial — `for (auto& [k, v] : json.items())` and we're done.
Dot-delimited keys *look* hierarchical and grep cleanly without
needing tree-walk logic in the loader.

**`std::string_view` lifetime.** Both `StringTable::get` (this section)
and `LocalizationService::tr` (§ 5.6) return views; the cases below note
which method each applies to. All are covered by the same call-site rule.
1. **Active-table hit** (`get` and `tr`) — a view into the active
   `StringTable`'s `m_map` value storage; valid until the next
   `loadFromFile()` (which `LocalizationService::setLanguage` triggers).
2. **English-fallback hit** (`tr` only — `get` has no fallback) — the view
   aliases the service's `m_reference` (English) table storage;
   `m_reference` is loaded once at boot and never reloaded, so this view
   is effectively boot-lifetime.
3. **Key-passthrough miss** (`get` and `tr`) — returns the key itself, so
   the view aliases the *caller's* `key` argument, not any table; its
   lifetime is the caller's, which is typically shortest.

Callers that store the result must **materialise to `std::string` at the
call site** (and reissue lookup on `LanguageChangedEvent` — see § 5.6).
Materialising covers all three cases, so the call-site rule is uniform.

### 5.6 `engine/localization/localization_service.h` (slice L4)

```cpp
namespace Vestige
{

/// Engine-wide language state. Wired as an ISystem so it
/// participates in the same lifecycle as audio / physics / etc.
class LocalizationService : public ISystem
{
public:
    // ISystem contract: the four PURE virtuals (engine/core/i_system.h:88-100)
    // must be implemented; the non-pure opt-in virtuals (getUpdatePhase :111,
    // getOwnedComponentTypes, etc.) are intentionally left at their defaults.
    const std::string& getSystemName() const override { return s_name; }
    bool initialize(Engine& engine) override;   // Loads default language "en".
    void shutdown() override;
    void update(float dt) override {}            // No per-frame work.
    // getUpdatePhase() is NOT overridden — the default UpdatePhase::Update
    // (i_system.h:111) is correct: the service does no per-frame work, and
    // setLanguage() is invoked from menu/input handling, not from update().
    // LanguageChangedEvent is published synchronously inside setLanguage(),
    // so panel rebuilds are driven by the event, not by dispatch ordering.

    /// Active language code (BCP 47 short tag: "en", "he", "el", "la").
    const std::string& languageCode() const;

    /// Switch language. Loads `assets/localization/<code>.json`,
    /// publishes a `LanguageChangedEvent` on the existing event bus
    /// so panels can re-fetch their strings. Returns false if the
    /// language file is missing or malformed (caller keeps the old
    /// language).
    bool setLanguage(const std::string& code);

    /// Lookup a string by key in the active language. Falls back
    /// to the reference language ("en") if the key is missing in
    /// the active one. Falls back to the key itself as a last
    /// resort. Fallback order is locked: active → English → key
    /// (§ 12 question 1, resolved 2026-06-06).
    std::string_view tr(std::string_view key) const;

private:
    static inline const std::string s_name = "Localization";
    Engine*     m_engine = nullptr;
    std::string m_languageCode;
    StringTable m_active;
    StringTable m_reference;   // English, kept loaded for fallback.
};

/// Free-function convenience. Resolves the registered instance via
/// `SystemRegistry::getSystem<LocalizationService>()` against the
/// engine pointer captured at initialize() time. Returns the key
/// itself if no LocalizationService is registered (safe for unit
/// tests that don't spin up the full registry).
std::string_view tr(std::string_view key);

} // namespace Vestige
```

`tr("ui.menu.start_game")` is the canonical call site at the
consumer:

```cpp
// BEFORE
m_startButton.text = "Start Game";

// AFTER — assigns at panel-build time, not per frame.
m_startButton.text = std::string(Vestige::tr("ui.menu.start_game"));
```

The `std::string` materialisation at the call site is intentional —
`UILabel::text` already holds a `std::string`, and the view returned
by `tr()` would dangle if `setLanguage` is called between the
lookup and the copy. Materialising at the assignment site forces a
deep copy. The lookup happens at panel-rebuild time, not per-frame.
A `LanguageChangedEvent` subscriber on each panel triggers the
rebuild.

### 5.7 Editor "missing keys" overlay (slice L6)

A small dev-only panel that lists keys present in `en.json` but
missing in the currently-loaded language. Updates on language
change. Surfaces the same data the runtime warns about in the
log, but in a sortable / filterable form so translators can work
through the list. Tucked into the Editor → Debug menu.

CMake-driven audit:

```python
# tools/localization_audit.py — invoked from CI, STRICT by default
# (reviewer decision 4 — § Status / § 12 Q4).
#
# FAILS the build (exit non-zero) on either:
#   1. Any std::string literal passed to ui_label::text or
#      text_renderer methods that is not wrapped in tr()
#      (hardcoded user-visible string).
#   2. Any tr("key") whose key is absent from the reference
#      en.json (an untranslatable key — even English-fallback
#      can't resolve it, so it would render the raw key string).
#
# REPORTS ONLY (does not fail the build):
#   3. Keys present in en.json but missing from a secondary
#      language (he/el/la). Runtime English-fallback (decision 1)
#      makes a not-yet-translated secondary string a non-bug, so
#      gating this would block incremental translation. Surfaced
#      via the editor overlay above + a CI summary line.
#
# `--strict` is the default; `--lint` downgrades (1)+(2) to warnings
# for local pre-commit runs. Follows the existing tools/audit/
# pattern (tools/audit/lint_*.py).
```

**Detection mechanism (check 1).** Check 1 is a **regex line-scan**, not
a clang-AST pass — same lightweight approach as the existing
`tools/audit/lint_*.py` scripts. It flags assignments/arguments of the
form `<sink> = "literal"` / `<sink>("literal")` where `<sink>` matches a
known user-visible text sink (`.text`, `setText(`, `TextRenderer::draw*(`,
`UILabel`/`UIWorldLabel`/subtitle/toast constructors). Known false-positive
envelope: it cannot see a literal that reaches a sink through an
intermediate variable (`std::string s = "hi"; label.text = s;`) — those
are accepted as a documented blind spot, not chased with data-flow
analysis. Known false-positive suppression: a trailing `// i18n-exempt`
comment on the line (debug-only labels, format templates) skips the check.
§ 8 test 21 must therefore assert *both* a caught direct literal AND an
exempted line that is correctly ignored, so the regex envelope is pinned.

---

## 6. Out of scope (explicit non-goals)

These are deliberately deferred to keep the design small enough to
review:

1. **Full Unicode BiDi UAX#9.** The lightweight reorder in § 5.4 is
   wrong for mixed-script strings with embedded numerals (e.g.
   "He read שלום 7 times" — the "7" sits in an RTL run but should
   visually appear LTR within its directional context). Shipped
   strings in this project are pure-script; mixed-script strings
   come back in their own roadmap item when they're needed.
2. **Arabic, Persian, Devanagari, CJK shaping.** These need
   HarfBuzz for ligatures / contextual forms. Adding HarfBuzz is
   a separate dependency-pin decision and a separate ~3 ms shaping
   cost per string. Out of scope for biblical-content Phase 10.
3. **CJK rendering at all.** Even non-shaped CJK glyphs need a
   CJK font (~10 MB minimum) and the L2 stack would have to grow.
   Out-of-script codepoints (anything Arimo doesn't cover, including
   any CJK codepoint a user might paste into a text field) render
   as Arimo's fallback glyph — a "?" mark per `font.cpp:241-245`. This
   is the **deliberate degraded-but-stable** behaviour, not a
   crash. Pinned by § 8 test 8.
4. **Plural forms (gettext-style ngettext).** Plaque body text is
   declarative; the UI strings ("Start Game", "Settings") don't
   need plural rules. Add when the first call-site needs it.
5. **Date / number formatting.** Same reason — no call site
   formats numbers today, no need to design the cross-locale
   formatter ahead of demand.
6. **Editor (ImGui) i18n.** The editor is developer-facing; it
   stays English-only. This design covers the **runtime** UI
   (UILabel, world labels, plaques, subtitles, toasts).
7. **Right-to-left layout for the menu chrome.** Buttons / sliders
   stay LTR even when content is Hebrew. Mirrored chrome is its
   own UI-layout decision separate from text rendering.

Items 1, 2, 3, 6, 7 are noted in `engine/localization/string_table.h`
as Phase 11+ targets so the gap is documented in code.

---

## 7. Dependencies

### Already pinned (no change)

- **FreeType `VER-2-13-3`** (`external/CMakeLists.txt:140-152`) —
  covers per-codepoint glyph rasterization for the three target
  scripts. No version change needed.
- **nlohmann_json** (transitive via the existing JSON call sites
  in scene / settings / formula serialisers) — loads the
  `<lang>.json` string tables.

### Bundled fonts

- **`assets/fonts/arimo.ttf`** (existing, SIL OFL 1.1 — per
  `ASSET_LICENSES.md:49` / `THIRD_PARTY_NOTICES.md`) — covers Latin
  + Greek (basic + polytonic), verified via `fc-query` + `ttx -t cmap`
  on 2026-05-18 (see § 4 inventory). Used for the Latin + Greek ranges
  in the default stack. No change.
- **`assets/fonts/frank_ruhl_libre.ttf`** (NEW this revision,
  **SIL OFL 1.1**) — Frank Ruhl Libre by Yanek Iontef (Fontef),
  Google Fonts 2016: the open-source revival of the classic Frank Rühl
  Hebrew serif, the standard body face for print/biblical Hebrew. Used
  for the Hebrew range in the default stack (reviewer decision 3 — a
  proper biblical-Hebrew serif rather than Arimo's sans-serif Hebrew).
  Source: <https://fonts.google.com/specimen/Frank+Ruhl+Libre>
  (upstream <https://github.com/google/fonts/tree/main/ofl/frankruhllibre>).

  **Bundling precondition (L2 prerequisite).** The `.ttf` is **not yet in
  tree** — `assets/fonts/` today holds only arimo / cormorant_garamond /
  inter_tight / jetbrains_mono. L2 cannot land (and § 8 test 6 stays red)
  until `frank_ruhl_libre.ttf` is committed to `assets/fonts/`. Bundling it
  also requires updating the two licence ledgers, which currently enumerate
  a **closed set of four** fonts:
  - `ASSET_LICENSES.md` — add a fifth font-table row, matching the
    table's actual columns (`File | Source | License | Attribution`).
    Suggested row: `frank_ruhl_libre.ttf` | Frank Ruhl
    Libre by Yanek Iontef | **OFL 1.1** | "Copyright 2016 The Frank Ruhl
    Libre Project Authors (https://github.com/google/fonts/tree/main/ofl/frankruhllibre)
    — Reserved Font Name 'Frank Ruhl Libre'".
  - `THIRD_PARTY_NOTICES.md` — the sentence "All **four** bundled font
    files are licensed under … OFL 1.1" (`:125`) must become "five".
  OFL is the same licence class as the project's four existing OFL fonts
  (Arimo, Cormorant Garamond, Inter Tight, JetBrains Mono), so this adds no new
  licence *type* — just the fifth row + the count edit. The shared
  `OFL.txt` already in `assets/fonts/` covers the licence text;
  reserved-font-name handling per
  OFL §§ 1–5 applies. These ledger edits ship in the L2 commit alongside
  the font file.

### Considered and rejected

- **Frank Ruehl CLM (Culmus).** The reviewer's first pick for the
  biblical-Hebrew face. **Rejected on licence:** the Culmus Frank-Ruehl
  family is **plain GPLv2 with NO font-embedding exception** — the
  Culmus `LICENSE` attaches the "special exception" only to the Yoram
  Gnat families (Shofar / Keter YG / Hadasim / Simple), not to the
  Maxim Iorsh families (Frank-Ruehl / Nachlieli / David / Miriam Mono).
  Bundling a plain-GPL asset is unsafe for the project's stated
  commercial Steam future. Frank Ruhl **Libre** (OFL, above) is the
  same typeface design without the copyleft risk. (The design doc's
  earlier "Public Domain, ~190 KB" claim for Frank Ruehl CLM was
  incorrect; corrected here after verifying the Culmus LICENSE on
  2026-06-06.)
- **Arimo Hebrew only (defer the serif face).** The rev-2 § 12 Q3
  recommendation (now superseded). Rejected by the reviewer in favour
  of shipping the proper serif now. Arimo's 87/88 Hebrew coverage is a
  sans-serif workmanlike face that visually clashes with the serif
  Latin used for biblical content — the reason the dedicated face was
  brought forward to v1.
- **Bundle Noto Sans/Serif Hebrew.** Not chosen: Noto Serif Hebrew
  (OFL) would also be licence-clean, but Frank Ruhl Libre *is* the
  canonical Frank Rühl design the reviewer asked for. The `FontStack`
  abstraction supports swapping it later if the typographic bar shifts.
- **HarfBuzz.** Would enable proper shaping (Hebrew vowel marks
  positioned correctly under their consonants, polytonic Greek
  accent placement). Cost: ~600 KB binary + ~1.5 MB build-time +
  ~3 ms per shaped string. Verdict: defer. Plaque text uses
  pre-shaped strings (no live combining-mark composition) so the
  visual quality gap is acceptable for v1. Documented upgrade
  path in `engine/localization/string_table.h`.
- **ICU.** Full Unicode toolkit. ~25 MB. Rejected: too heavy for
  the four-language scope. Same UAX#9 limitation noted in § 6.
- **gettext / .po files.** Battle-tested translator workflow.
  Rejected: requires `msgfmt` toolchain dependency for editors, and
  the project's translator audience is small enough that JSON is
  easier to author by hand. Plus `nlohmann_json` is already in the
  build.

---

## 8. Verify-step plan (global `~/.claude/CLAUDE.md` rule 12)

Each slice has explicit pass/fail criteria. Tests live under
`tests/` alongside their peers and run via the existing ctest
target.

| # | Slice | Test | Pass criterion |
|---|---|---|---|
| 1 | L1 | `Utf8Decode.AsciiRoundTrip` | Encoding "Hello" then decoding yields {72,101,108,108,111} with bytesRead 1 per glyph. |
| 2 | L1 | `Utf8Decode.HebrewRoundTrip` | "שלום" → {1513,1500,1493,1501} (4 codepoints, 2 bytes each). |
| 3 | L1 | `Utf8Decode.GreekRoundTrip` | "Πρωτότυπο" → 9 codepoints (with diacritics in U+1F00 range). |
| 4 | L1 | `Utf8Decode.InvalidByteEmitsReplacement` | The sequence `\xC3\x28` (invalid 2nd byte) → {0xFFFD, advance=1}. |
| 5 | L1 | `Font.AsciiBackwardCompat` | After L1, the GlyphInfo for 'A' compared field-by-field (atlasOffset, atlasSize, size, bearing, advance) is bit-equal to the L0 baseline. `GlyphInfo` gains a `operator==` in L1 to make this assertable. |
| 6 | L1/L2 | `Font.LoadsHebrewRangeFromFrankRuhlLibre` | `frank_ruhl_libre.ttf` loaded with `HEBREW_RANGE` produces ≥ 27 non-fallback glyphs in the Hebrew block (22 Hebrew letters + 5 final forms — the ≥ 27 floor forces the finals to be present, not just the base letters; with punctuation this is the same ~30 estimate as the § 9 memory table, both pinned to the real count once the bundled face is in tree). Companion: `arimo.ttf` loaded with `GREEK_RANGES` produces ≥ 120 non-fallback Greek glyphs. |
| 7 | L2 | `FontStack.RoutesLatinAndHebrewToDistinctFonts` | Default 2-font stack (Arimo Latin+Greek, Frank Ruhl Libre Hebrew): `lookup('A').font` is the Arimo instance and `lookup(U+05D0).font` is the Frank Ruhl Libre instance — different `Font*`, each returning a real (non-fallback) glyph. Pins the per-script routing. |
| 8 | L2 | `FontStack.MissingCodepointReturnsFallback` | Unmapped codepoint (e.g. 0x4E2D Chinese) returns `Hit{font, glyph}` where `glyph` is the fallback `?` AND `font` is the first font in the stack (non-null), not a crash. Pins § 6 deferral 3 + the miss contract in § 5.3. |
| 9 | L2 | `TextRenderer.MruCacheSkipsStackWalk` | The MRU cache lives in the **TextRenderer** (`m_lastHitFont`, § 5.3), wrapping `FontStack::lookup`. 1000-glyph pure-Latin render through the renderer: the MRU short-circuits the stack walk, so `FontStack::lookup` (and its `hasGlyph` probes on the non-cached fonts) is invoked only on a script-boundary transition. Verified by counting `FontStack::lookup` calls — must equal 1 (the first glyph, before the MRU is warm) plus one per script-boundary transition; i.e. exactly 1 for a Latin-only string (0 additional after the first). The cached font's own per-glyph `getGlyph` is the baseline cost, not counted here. |
| 10 | L3 | `Rtl.HebrewReverse` | `toVisualOrder([1513,1500,1493,1501])` → `[1501,1493,1500,1513]`. |
| 11 | L3 | `Rtl.LatinPassthrough` | `toVisualOrder([72,105])` → `[72,105]` (unchanged). |
| 12 | L3 | `Rtl.MixedScriptRunReversal` | "Hi שלום" → "Hi <hebrew-reversed>". Documents the lightweight semantics. |
| 13 | L4 | `StringTable.LookupHit` | After loading `en.json`, `t.get("ui.menu.start_game")` returns "Start Game". |
| 14 | L4 | `StringTable.LookupMissReturnsKey` | `t.get("missing.key")` returns "missing.key". |
| 15 | L4 | `StringTable.LoadMissingFileReturnsFalse` | `t.loadFromFile("/does/not/exist.json")` returns false, leaves any prior load intact. Pins boot-without-translations behaviour. |
| 16 | L4 | `LocalizationService.FallbackToEnglish` | Active "he" doesn't have key X; `tr(X)` returns the English value, not the key. |
| 17 | L4 | `LocalizationService.LanguageChangedEvent` | `setLanguage("he")` publishes the event exactly once on the event bus. |
| 18 | L5 | `SettingsMigration.V2ToV3PopulatesLanguage` | A v2 settings file loaded via `loadOrMigrate()` emerges with `schemaVersion=3` and `localization.language="en"` (default). |
| 19 | L5 | `Settings.PersistsLanguage` | Save with `language="he"`, reload, the field round-trips. |
| 20 | L5 | `Settings.LiveApplyHotSwapsTable` | UILabel reads "Start Game" → user picks "he" in the menu → next panel rebuild reads "התחל משחק". |
| 21 | L6 | `LocalizationAudit.CatchesHardcoded` | Test harness with one deliberate `m_label.text = "hardcoded";` → the Python audit exits non-zero in strict mode. |
| 22 | L6 | `LocalizationAudit.MissingKeysReport` | en.json has 5 keys, he.json has 3 → editor overlay lists the 2 missing keys. |
| 23 | L6 | `TextRendererPerf.HudPassUnderBudget` | New `tests/test_text_renderer_perf.cpp`: 20-label / 800-glyph workload on the dev rig completes the HUD pass in ≤ 0.30 ms / frame (8 frame median to dodge cold-cache outlier). The benchmark is the gate every earlier slice's perf claim is measured against. |

Test 5 introduces a new `GlyphInfo::operator==`; the addition
is part of L1 since test 5 lands then.

Test 23 is the budget gate. Earlier slices' perf claims (§ 3, § 5.3)
are flagged as estimates; test 23 confirms them in L6. If any
intermediate slice ships a regression that test 23 fails to catch
because the benchmark didn't exist yet, the fix lands in L6 + a
backfill test for the offending slice. This is the standard
"benchmark-after" trade-off when scaffolding budget gates.

---

## 9. Performance budget — estimates with measurement plan

**Caveat.** The *timing* and *atlas-size* figures in this section are
first-cut estimates based on the existing `unordered_map::find` cost
class and microbenchmarks of similar UTF-8 decoders; real numbers
replace them in L6 once `tests/test_text_renderer_perf.cpp` exists. The
*glyph counts* for the in-tree fonts (Latin 95, Greek 127 + 233) are
fc-query-verified (§ 4); only the Hebrew count (~30, Frank Ruhl Libre)
is an estimate pending the in-tree face (pinned by test 6). Per global
`~/.claude/CLAUDE.md` rule 13, the estimated figures are labelled as
such.

Workload: synthetic HUD of 20 labels averaging 40 characters each
(~800 glyphs / frame) at 1920×1080 on the dev rig (Ryzen 5 5600 /
RX 6600 / openSUSE Tumbleweed).

| Stage | Today (ASCII) estimate | L1 (UTF-8) estimate | L2 (stack) estimate | L3 (RTL) estimate |
|---|---|---|---|---|
| UTF-8 decode | 0 µs | ~8 µs | ~8 µs | ~8 µs |
| Codepoint lookup | ~24 µs | ~30 µs | ~30 µs (2-font stack, MRU hits) | ~30 µs |
| Glyph emit | ~120 µs | ~120 µs | ~120 µs | ~120 µs |
| RTL reverse | 0 µs | 0 µs | 0 µs | ~5 µs (only on RTL strings) |
| **Total HUD pass** | ~144 µs | ~158 µs | ~158 µs | ~163 µs |

Budget: 300 µs. Estimated headroom: ~137 µs for the **pure-script /
MRU-hit** case (300 − 163, the L3 column). The genuine worst case is an
*alternating-script* string (2 extra `unordered_map::find` per glyph,
below), which the MRU-hit columns exclude — it is pinned only by the
L6 benchmark (test 23), not by this table's headroom figure.

Worst-case stack pathology: a 2-font stack with a 200-character
all-Latin string (so MRU cache hits 100 %) costs the same per glyph
as a single-font baseline — the MRU cache is what makes the design
tractable.
A 2-font stack with an alternating-script string would double the
per-glyph lookup cost; the L6 benchmark (test 23) includes an
alternating-script workload to pin this case.

The earlier-revision claim "200-char line → 24 000 lookups → 0.7 ms
without caching" is replaced by the explicit MRU-cache model above:
**every render call** maintains the MRU pointer, so the only
cache-miss-per-glyph case is a deliberately-pathological
alternating-script string that does not appear in shipped content.

Cold-cache worst case (first frame after language switch, every
glyph misses the font's `unordered_map`): estimated ~600 µs.
Above the budget but a **one-frame transient**. Acceptable per § 1
("≤ 0.30 ms / frame for the same workload" measured against the
warmed harness — cold cache is explicitly out-of-scope for the gate).

GPU side: zero change. Same atlas binding, same shader, same draw
call count.

### Memory budget (estimated, pinned in L2)

| Atlas | Font | Glyphs | Estimated atlas size |
|---|---|---|---|
| Latin (ASCII range 0x20-0x7E) | Arimo | 95 | ~1 MB (R8; today's strip atlas) |
| Basic Greek (U+0370-U+03FF) | Arimo | 127 | ~0.9 MB |
| Polytonic Greek (U+1F00-U+1FFF) | Arimo | 233 | ~1.5 MB |
| Hebrew (U+0590-U+05FF) | Frank Ruhl Libre | ~30 | ~0.4 MB |
| **Total estimated** | — | ~485 | ~3.8 MB |

The atlases group by `Font` instance — the Arimo instance packs its
Latin + Greek ranges into one shelf-packed atlas (L2), and the Frank
Ruhl Libre instance packs the Hebrew range into its own. The Hebrew
estimate drops vs. the rev-2 number (~0.6 MB → ~0.4 MB) because Frank
Ruhl Libre carries only the ~22 Hebrew letters + finals + a handful of
punctuation, not Arimo's fuller 87-glyph block. The ~3.8 MB total is a
ceiling; the shelf packer may compress better. L2 test 6 pins the
actual sizes.

Headroom against the project's implicit "single GPU pool, < 100 MB
for text" baseline is comfortable. No specific atlas-cap is set in
§ 1 (rev-1's "16 MB" was unsourced — removed).

---

## 10. Backwards compatibility — precise scope

What is guaranteed unchanged:

- **ASCII-only call sites** that don't use `tr()` still render the
  same glyphs from the same atlas with the same code path. § 8
  test 5 (`Font.AsciiBackwardCompat`) pins this against the
  L0 baseline.
- **Existing `std::string` consumers** (`UILabel`, world labels,
  subtitles, toasts) keep their `text` field; literal English
  strings still compile and run.
- **Existing settings.json files** load via `migrate_v2_to_v3` →
  emerge with `localization.language = "en"`, identical UX to
  pre-rev3 (§ 8 test 18).
- **No new GPU shader or buffer layout**; same `screen_quad.frag`
  text shader, same VBO layout.

What is *not* guaranteed:

- **`Font::getGlyph(char)` overload removal in L2.** The shim
  exists across L1 → L2 only. Callers that depend on the `char`
  overload after L2 must migrate to the `uint32_t` form. § 4
  inventory confirms 0 external consumers today (only the
  text-renderer's 4 sites use it, and L1 migrates them).
- **`unordered_map<char, GlyphInfo>` ABI in `Font`.** The map's
  key type changes in L1. No external consumer holds a reference
  to it today (grep `m_glyphs` confirms — only `Font::getGlyph`
  reads it). If a test or tool *did* hold a reference, it would
  fail to compile in L1; the migration is mechanical (`char` →
  `uint32_t`).
- **The atlas image is rebuilt** (L2 swaps the single-shelf row-wrap
  packer for a real 2D shelf/skyline packer). Texture id is stable;
  pixel layout inside the atlas changes. No external consumer reads
  the atlas pixels directly.

---

## 11. Alternatives considered

### Per-script font bundles (a dedicated Hebrew face)

Rev 1 proposed Noto Sans Hebrew + Noto Sans Greek; rev 2 dropped both
when fc-query showed Arimo covers all three scripts. **Rev 3 partially
re-adopts this** for one script only: the reviewer (decision 3) chose a
dedicated biblical-Hebrew serif over Arimo's sans-serif Hebrew, so the
default stack pairs Arimo (Latin + Greek) with **Frank Ruhl Libre**
(Hebrew, OFL — § 7). Greek stays on Arimo (its polytonic coverage is
fine and no aesthetic objection was raised). This is the `FontStack`
abstraction doing exactly what § 5.3 anticipated — a per-script swap
with no call-site re-plumbing.

### Pre-shape all strings offline (`assets/localization/<lang>.bin`)

Run HarfBuzz at build time, ship pre-shaped glyph index runs.
Rejected: would force `tools/localization_compile` into the
build, and the shaping benefit is small for non-cursive scripts
(no Arabic-style joining). Re-evaluate when Arabic ships.

### Drop the `char` shim immediately

Migrate all callers in slice L1. Rejected: the shim is one
inline line, the callers are ~10 sites, and the migration is
mechanical (`'A'` → `static_cast<uint32_t>('A')` is the same
generated code). Bundling the migration into L2 keeps L1 a
pure additive change and gives reviewers a smaller diff.

### Use ICU for everything

Full BiDi, full shaping, full normalization. Rejected for the
narrow four-language scope — full cost rationale (~25 MB, build
time) lives in § 7 "Considered and rejected"; ICU is the right
answer when we ship Arabic, not before.

### Embed translations in source code (`tr("...")` as a literal)

Like `Qt::tr` — strings live at the call site and a tool
extracts them. Rejected: hides the canonical key list, makes
"are we missing translations for the Hebrew language" a
build-tool question rather than a glance at `he.json`. The
dot-key approach is more transparent for a small i18n surface.

---

## 12. Open questions for the reviewer — RESOLVED 2026-06-06

All four answered by the reviewer; decisions are summarised in the
§ Status block and folded into the relevant sections. Recorded here
for the audit trail.

1. **Reference language fallback ordering.** → **active → English →
   key** (silent English fallback), as § 5.6 proposed. The loud
   `‹missing: …›` marker was declined; the editor "missing keys"
   overlay (§ 5.7) plus the once-per-key log warning cover QA
   visibility without surfacing markers to players.
2. **Settings field placement.** → **own top-level `Settings::
   Localization` group** (not folded into Accessibility). § 2 L5 already
   specifies the `Settings::Localization` group; now locked.
3. **Hebrew font aesthetic for v1.** → ship a **dedicated biblical-
   Hebrew serif now** (rather than the rev-2 "defer to Arimo"
   recommendation), but **Frank Ruhl Libre (OFL)** substituted for the
   originally-named Frank Ruehl CLM after
   the latter was found to be plain GPLv2 with no embedding exception
   (§ 7 "Considered and rejected", which records the licence
   correction). Default stack is now 2-font (§ 2 L2, § 5.3, § 9).
4. **Localization audit in CI.** → **strict from day one**, scoped in
   § 5.7: hardcoded-literal + reference-language missing-key checks
   fail the build; secondary-language coverage is report-only (English
   fallback makes a not-yet-translated secondary string a non-bug). The
   secondary-language-report-only scoping is the locked interpretation
   (§ Status decision 4); tightenable later without redesign.

---

## 13. Implementation order (suggested)

The slices can land in any dependency-respecting order, but the
recommended sequence is:

1. **L1** — pure additive (UTF-8 decoder + `uint32_t` Font API +
   the text-renderer migration of the 4 loops). Lowest risk for
   the new API surface; behaviour change is bounded to non-ASCII
   inputs which today produce garbage. Lands first; cold-eyes
   review can verify the additive claim by diffing the ASCII
   behaviour test (test 5) before/after.
2. **L2** — `FontStack`, multi-range Arimo load (Latin + Greek) +
   the bundled Frank Ruhl Libre Hebrew face, shelf packer. Bigger
   change but still no consumer-side migration — the existing
   single-font path is routed through the 2-font stack, and the MRU
   cache keeps pure-script strings at baseline cost (§ 5.3).
3. **L3** — RTL reverse, plumbed into the text renderer.
   Latin-only callers see no behaviour change.
4. **L4** — String table + `LocalizationService`. The first
   `tr()` call site migrations land in this slice as a proof of
   integration.
5. **L5** — Settings field + menu picker + migration v2 → v3.
   End-to-end visible user flow. The migration test (#18) is the
   most important gate here.
6. **L6** — Editor overlay + CI audit + HUD-pass benchmark
   (test 23). Polish + the gate that retro-validates the perf
   claims of L1–L5.

Each slice ships with its own CHANGELOG line and the verify-step
pass criteria from § 8 satisfied.

---

## 14. References

### Specifications

- [UnicodeUAX9] Unicode Standard Annex #9, "Unicode Bidirectional
  Algorithm" — the full algorithm that § 6 deferral 1 declines to
  implement. <https://www.unicode.org/reports/tr9/>
- [UnicodeTR36] Unicode Technical Report #36, "Unicode Security
  Considerations" — Section 3.5 ("UTF-8 Exploit") establishes the
  "emit U+FFFD and advance 1 byte" convention used by UTF8-INV-4.
  <https://www.unicode.org/reports/tr36/>
- [BCP47] IETF BCP 47, "Tags for Identifying Languages" — the
  short-tag convention used in `LocalizationService::languageCode()`.
  <https://www.rfc-editor.org/info/bcp47>

### Implementations referenced

- [Hoehrmann] Björn Höhrmann, "Flexible and Economical UTF-8
  Decoder" — the DFA-style decoder design referenced in § 3 for the
  per-codepoint cost estimate. <https://bjoern.hoehrmann.de/utf-8/decoder/dfa/>
- [Liberation] Croscore / Liberation typeface family documentation
  — explains the Arimo / Liberation Sans coverage tables used in
  § 4 inventory and § 9 budget. <https://github.com/liberationfonts/liberation-fonts>
- [HarfBuzz] HarfBuzz project — the shaper that § 7 considers and
  defers. <https://harfbuzz.github.io/>

### Project precedent

- `docs/phases/phase_10_audio_music_player_design.md` — the W8
  design doc this one follows in convention (status block, CPU/GPU
  placement per project rule 7, verify-step test plan, references), though
  the section layout is not a one-to-one mirror.
- `docs/phases/phase_10_settings_design.md` — the slice-13 design
  that established the schema-migration pattern this design hooks
  into.
- `docs/phases/phase_10_fog_design.md` — the slice-11 design that
  established the "estimates + benchmark pin in last slice" pattern
  this design follows.

### Fonts

- [FrankRuhlLibre] Frank Ruhl Libre, by Yanek Iontef (Fontef) —
  OFL-1.1 open-source revival of the classic Frank Rühl Hebrew serif.
  Bundled for the Hebrew range (§ 7, reviewer decision 3).
  <https://fonts.google.com/specimen/Frank+Ruhl+Libre> ·
  upstream <https://github.com/google/fonts/tree/main/ofl/frankruhllibre>
- [Arimo] Arimo (Liberation Sans-compatible, SIL OFL 1.1 per the
  project's `ASSET_LICENSES.md:49`) — bundled for the Latin + Greek
  ranges. Already in tree.
- [CulmusLicence] Culmus project LICENSE — verified 2026-06-06 that the
  Frank-Ruehl / Maxim Iorsh families are plain GPLv2 with **no** font
  exception (the exception attaches only to the Yoram Gnat families),
  which is why Frank Ruehl CLM was rejected in favour of Frank Ruhl
  Libre (§ 7). <https://github.com/deepin-community/culmus> (LICENSE).

### Tools used during design

- `fc-query` (fontconfig) — used 2026-05-18 to determine Arimo's
  Hebrew / Greek coverage (see § 4 inventory table).
- `ttx -t cmap` (fontTools) — used 2026-05-18 to count exact
  codepoint coverage in the cmap table (Hebrew: 87, basic Greek:
  127, polytonic: 233).
