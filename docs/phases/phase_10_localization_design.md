# Phase 10 Localization — i18n + RTL + Multi-script Text Rendering Design

## Status

Design doc, **awaiting blocking review** (per project `CLAUDE.md`
rule 1: research → design → review → code). Revision 2 — folded in
cold-eyes review findings 2026-05-18. No code lands until this is
reviewed.

Closes four roadmap bullets in the Phase 10 → Localization section
of `ROADMAP.md`:

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
- HUD-pass budget: today's pass is estimated at ~0.15 ms / frame
  for a typical workload (no benchmark exists today — see §9 for
  the harness scaffolding plan in slice L6); the i18n addition
  must keep within **≤ 0.30 ms / frame** for the same workload
  *measured against the same harness*. The budget gate ships in
  L6 alongside the benchmark itself, so earlier slices can't
  "regress" what doesn't exist yet — they each ship a
  microbenchmark for the work they add (§ 8 tests 8, 20).
- Atlas-memory budget: see § 9 for verified numbers. The single
  font (Arimo) already on disk covers all three scripts, so the
  multi-script atlas grows from 1 MB (today, 95 glyphs) to an
  estimated 4 MB at full coverage (~450 glyphs across the three
  scripts at 48 px pixel-size; see § 9 measurement plan).
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
| **L1** | UTF-8 decoder + codepoint Font API | M | `engine/utils/utf8.{h,cpp}` (pure-function decoder), Font glyph map keyed by `uint32_t`, `text_renderer.cpp` glyph loops at lines 249 / 354 / 478 / 549 switched to UTF-8 walk. Plus a HUD perf microbench (§ 8 test 20 scaffold) so the budget gate has a baseline. Backwards compatible for ASCII inputs: identical output, see §10 for precise scope. |
| **L2** | Font fallback chain + glyph-range expansion | M | `FontStack` class — ordered list of Font instances. `getGlyph(codepoint)` walks the list until a font claims the codepoint. Default stack contains a single Arimo Font loaded with Latin + Hebrew + Greek ranges (fc-query + ttx confirm coverage; see § 4 and § 14 Tools); the *stack abstraction* exists so the design can later add per-script fonts without re-plumbing call sites. |
| **L3** | Right-to-left logical→visual reorder | S | `engine/utils/rtl.{h,cpp}` — per-run reversal for pure-Hebrew strings (no BiDi algorithm). `TextRenderer` calls the reorder before glyph emission. |
| **L4** | String table + Localization service | M | `engine/localization/string_table.{h,cpp}` (JSON loader, key→value, miss reporting), `LocalizationService` ISystem wrapper, `assets/localization/{en,he,el,la}.json` initial files, plus the SystemRegistry registration + `LanguageChangedEvent` on the existing event bus. |
| **L5** | Settings integration + language dropdown | S | Bump `kCurrentSchemaVersion` 2 → 3 in `settings.h`; add `Settings::Localization { language: "en" }`; add `migrate_v2_to_v3(json&)` in `settings_migration.{h,cpp}`; add the language-picker UI to the existing Settings menu Accessibility tab. **Owns the v2 → v3 schema bump (locked 2026-06-01, CE4):** the v1 → v2 bump (onboarding) already shipped via Phase 10.5; this slice is the next bump in sequence and fills the commented `// case 2:` placeholder in `settings_migration.cpp`. |
| **L6** | Editor / debug tooling + HUD-pass benchmark | M | Editor "missing keys" overlay; CMake-driven `tools/localization_audit.py` to catch hardcoded user-visible strings in CI; **`tests/test_text_renderer_perf.cpp`** — a 20-label / 800-glyph HUD-pass benchmark that pins the §1 budget as a ctest target. The benchmark is the gate every prior slice's perf claim is measured against. |

Each slice is independently mergeable and tested. The shape mirrors
the per-slice cadence used by Phase 10 Audio (10.1 – 10.10) and Fog
(11.1 – 11.10). The reference language always loaded, so partial
deployment (only `en.json` shipped) never produces visible blanks.

---

## 3. CPU / GPU placement (CLAUDE.md Rule 7)

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
documented model. Per CLAUDE.md rule 6.

---

## 4. Inventory — what exists, what's missing

Surveyed against the current source on 2026-05-18:

### Existing (verified by grep + fc-query)

| Area | Location | State |
|---|---|---|
| **FreeType integration** | `engine/renderer/font.cpp::loadFromFile` lines 85-265 | Complete. `FT_Init_FreeType` / `FT_New_Face` / `FT_Set_Pixel_Sizes`. Pinned via `external/CMakeLists.txt:140-152` (`FetchContent_Declare(freetype GIT_TAG VER-2-13-3)`). |
| **Glyph atlas** | `engine/renderer/font.cpp` second pass (line 248) | Complete. Single-channel `GL_R8`, immutable storage via DSA. Atlas-packing is a simple horizontal strip — fine for the ~95 ASCII glyphs but will need a 2D shelf packer for the ~450 multi-script glyphs (L2 covers this). |
| **Text rendering** | `engine/renderer/text_renderer.cpp` lines 249, 354, 478, 549 | 4 glyph loops, all `for (char c : text)` over `std::string`. Calls `m_font.getGlyph(c)`. L1 must edit all 4 sites to walk UTF-8 codepoints instead. Batched (`beginBatch2D` / `endBatch2D`) — Phase 10.9 Pe1. Italic-oblique shear — Phase 10.9 P6. |
| **`Font::getGlyph`** | `engine/renderer/font.h:50` | `const GlyphInfo& getGlyph(char codepoint) const;` — `char` overload only; can't address codepoints beyond U+00FF. |
| **`Font` glyph map** | `engine/renderer/font.h:80` | `std::unordered_map<char, GlyphInfo>`. L1 swaps the key type to `uint32_t`. |
| **UI text consumers** | `engine/ui/{ui_label,ui_world_label,ui_keybind_row}.h`, subtitle renderer, FPS counter, toast notifications | All consume `std::string text` directly. Today's strings come from C++ literals at call sites. |
| **`ISystem` contract** | `engine/core/i_system.h:88-93` | `virtual const std::string& getSystemName() const = 0;` and `virtual bool initialize(Engine& engine) = 0;`. `LocalizationService` must match these exact signatures (see § 5.6). |
| **System lookup pattern** | `engine/core/system_registry.h` (`SystemRegistry::getSystem<T>()`) | The project does not use a free-function service locator. The free-function `tr()` in §5.6 forwards via the registry, not a separate locator file. |
| **Bundled fonts (fc-query 2026-05-18)** | `assets/fonts/` | `arimo.ttf` — Liberation-Sans-compatible, ships with Latin + 87/88 Hebrew (U+0590..U+05FF) + 127/144 basic Greek (U+0370..U+03FF) + 233/256 polytonic Greek (U+1F00..U+1FFF). `cormorant_garamond.ttf` — Latin only (no Greek, no Hebrew, despite the typeface's reputation). `inter_tight.ttf` — Latin + basic Greek. `jetbrains_mono.ttf` — Latin + basic Greek. |
| **Settings system** | `engine/core/settings.h:64` (`kCurrentSchemaVersion = 2`), `settings_migration.h:64` (`migrate_v1_to_v2`) | Complete (Phase 10 Settings — slice 13.5e). JSON-backed, atomic-write, runtime live-apply. Schema versioning exists; new fields land via `migrate_vN_to_vN+1`. |
| **JSON loading** | `engine/utils/json_size_cap.h` (`JsonSizeCap::loadJsonWithSizeCap`) | Canonical helper. Localization files are small (estimated ≤ 32 KB even at 1000 keys × 32-byte values) so the default cap is fine. **Estimate** — pinned by the slice-L4 acceptance test (test #13) once real content exists. |
| **Path sandbox** | `engine/utils/path_sandbox.h` | Localization JSON loaded from `assets/localization/`; the sandbox roots already cover this path. No change needed. |
| **Event bus** | `engine/events/event_bus.h` | Existing publish/subscribe. `LanguageChangedEvent` is one new event type on it (no new bus needed). |

### Missing (the work this design covers)

1. **UTF-8 decoder.** Today `for (char c : text)` truncates
   multi-byte sequences into garbage. Every text consumer is
   broken for any non-ASCII codepoint.
2. **Codepoint-keyed glyph map.** `Font::getGlyph(char)` can't
   address codepoints beyond U+00FF.
3. **Font fallback / multi-range glyph loading.** Arimo covers
   all three scripts, but `loadFromFile` hard-loops ASCII 32-126
   (see `font.cpp:115`). Loading additional ranges is configurable
   per call but not yet implemented.
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
8. **Atlas packing for ~450 glyphs.** The horizontal-strip packer
   is fine for 95; for 450 it needs a shelf packer to fit a square
   power-of-two texture without wasting > 50 % space.
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
    /// holds one Arimo Font loaded with Latin + Greek + Hebrew ranges,
    /// because the bundled Arimo covers all three. The class exists
    /// so future per-script swaps (e.g. swap in Frank Ruehl for the
    /// biblical-Hebrew aesthetic) work without re-plumbing call sites.
    void addFont(std::shared_ptr<Font> font);

    /// Locate the (font, glyph) pair for a codepoint. Returns a
    /// raw Font* whose lifetime is "until the next addFont/clear
    /// call on this stack" — same guarantee as a vector::front()
    /// pointer survives until the next vector mutation.
    struct Hit { Font* font; const GlyphInfo* glyph; };

    /// First font that covers `codepoint` wins. If nothing covers it,
    /// returns the first font's fallback glyph (the standard "?" mark
    /// from `font.cpp:242`).
    Hit lookup(uint32_t codepoint) const;

    /// True iff any loaded font claims this codepoint.
    bool covers(uint32_t codepoint) const;

private:
    std::vector<std::shared_ptr<Font>> m_fonts;
};

} // namespace Vestige
```

**Performance contract.** The naïve "walk the stack on every glyph"
cost is O(stacks × stringLen) per render — for the default 1-font
stack this is O(stringLen), identical to the slice-L0 baseline. When
future per-script swaps grow the stack to 2-3 entries, the per-glyph
hop adds an `unordered_map::find` per missed font. The text-renderer
side maintains a one-element MRU cache (`m_lastHitFont`) — when the
same font claims two adjacent glyphs (~99 % of cases for natural
text, where script boundaries are at most a handful per line), the
cache skips the stack walk.

The cost numbers below are *estimates* pending the L6 benchmark:

- **Default 1-font stack:** zero overhead vs. baseline (same single
  `unordered_map::find` per glyph).
- **2-font stack, all-Latin string:** zero overhead with MRU cache
  (every glyph hits the cached font); 1 extra `unordered_map::find`
  per glyph without it.
- **2-font stack, alternating-script string** (worst case): 2 extra
  `unordered_map::find` per glyph — pathological for documents that
  alternate every codepoint, not realistic for plaque text.

L6 microbenchmark pins these (§ 8 test 8). If the worst case is
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

    /// Lookup. Missing keys return the key itself (when built with
    /// the `VESTIGE_LOCALIZATION_WARN_MISSING` CMake option — debug
    /// builds set this by default — the key is also logged via
    /// `Logger::warning` exactly once per session per key, no
    /// per-frame spam). Release builds omit the warning path
    /// entirely to keep the hot path branchless.
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

**`std::string_view` lifetime.** `get` returns a view into
`m_map`'s value storage; the view is valid until the next
`loadFromFile()` call (which `LocalizationService::setLanguage`
triggers). Callers that store the result across a possible
language change must materialise to `std::string` (and reissue
lookup on `LanguageChangedEvent` — see § 5.6).

### 5.6 `engine/localization/localization_service.h` (slice L4)

```cpp
namespace Vestige
{

/// Engine-wide language state. Wired as an ISystem so it
/// participates in the same lifecycle as audio / physics / etc.
class LocalizationService : public ISystem
{
public:
    // ISystem contract (engine/core/i_system.h:88-93).
    const std::string& getSystemName() const override { return s_name; }
    bool initialize(Engine& engine) override;   // Loads default language "en".
    void shutdown() override;
    void update(float dt) override {}            // No per-frame work.

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
    /// resort. See § 12 question 1 for the fallback-policy
    /// decision the reviewer is asked to confirm.
    std::string_view tr(std::string_view key) const;

private:
    static inline const std::string s_name = "Localization";
    Engine*     m_engine = nullptr;
    std::string m_languageCode;
    StringTable m_active;
    StringTable m_reference;   // English, kept loaded for fallback.
};

/// Free-function convenience. Resolves the singleton instance via
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
# tools/localization_audit.py — invoked from CI
# 1. Walk engine/ + editor/ for any std::string literal passed to
#    ui_label::text or text_renderer methods that is not wrapped
#    in tr().
# 2. Walk every <lang>.json under assets/localization/ and report
#    keys present in en.json but missing in others.
# Exits non-zero on any violation when run with --strict; default
# is lint-mode (warn-only). The strict flag flips on per § 12 Q4.
# Follows the existing tools/audit/ pattern (tools/audit/lint_*.py).
```

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
   as Arimo's fallback glyph — a "?" mark per `font.cpp:242`. This
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

### Bundled fonts (no new bundles needed)

`assets/fonts/arimo.ttf` already covers all three scripts (verified
via `fc-query` + `ttx -t cmap` on 2026-05-18 — see § 4 inventory
table). The bundled font is sufficient for v1 across Latin,
Hebrew, basic Greek, and polytonic Greek. **This is the major
simplification vs. revision 1** — no Noto Sans Hebrew bundle, no
new `THIRD_PARTY_NOTICES.md` entry, no new font license to track.

### Considered and rejected

- **Bundle Noto Sans Hebrew.** Rejected: Arimo's 87/88 Hebrew
  coverage is sufficient, and adding ~280 KB of Hebrew-only fallback
  to ship duplicate glyph coverage is unjustified. Re-evaluate if
  the typographic-quality bar lifts (Arimo Hebrew is a sans-serif
  workmanlike face — Frank Ruehl CLM or SBL Hebrew would be more
  appropriate for biblical-aesthetic content). The `FontStack`
  abstraction supports this swap when the time comes.
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
| 6 | L1 | `Font.LoadsHebrewRangeFromArimo` | `arimo.ttf` loaded with `HEBREW_RANGE` produces ≥ 80 non-fallback glyphs in the Hebrew block (fc-query confirms 87 coverage; allow slop). |
| 7 | L2 | `FontStack.LatinHebrewRoundtrip` | Stack with one Arimo Font loaded with `{ASCII, HEBREW, GREEK_RANGES}` returns same font for both 'A' and U+05D0 (because there's one font). |
| 8 | L2 | `FontStack.MissingCodepointReturnsFallback` | Unmapped codepoint (e.g. 0x4E2D Chinese) returns the first font's fallback glyph (`?`), not a crash. Pins § 6 deferral 3. |
| 9 | L2 | `FontStack.MruCacheHits` | 1000-glyph pure-Latin render: cached-font path resolves without an extra hash lookup (verified by counting `m_fonts[i]->hasGlyph()` calls — must equal the number of script-boundary transitions, which is 0 for a Latin-only string). |
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

Tests 5 and 9 introduce a new `GlyphInfo::operator==`; the addition
is part of L1 since test 5 lands then.

Test 23 is the budget gate. Earlier slices' perf claims (§ 3, § 5.3)
are flagged as estimates; test 23 confirms them in L6. If any
intermediate slice ships a regression that test 23 fails to catch
because the benchmark didn't exist yet, the fix lands in L6 + a
backfill test for the offending slice. This is the standard
"benchmark-after" trade-off when scaffolding budget gates.

---

## 9. Performance budget — estimates with measurement plan

**Caveat.** All numbers in this section are first-cut estimates
based on the existing `unordered_map::find` cost class and
microbenchmarks of similar UTF-8 decoders. Real numbers replace
them in L6 once `tests/test_text_renderer_perf.cpp` exists. Per
CLAUDE.md rule 13, every figure below is labelled as estimate.

Workload: synthetic HUD of 20 labels averaging 40 characters each
(~800 glyphs / frame) at 1920×1080 on the dev rig (Ryzen 5 5600 /
RX 6600 / openSUSE Tumbleweed).

| Stage | Today (ASCII) estimate | L1 (UTF-8) estimate | L2 (stack) estimate | L3 (RTL) estimate |
|---|---|---|---|---|
| UTF-8 decode | 0 µs | ~8 µs | ~8 µs | ~8 µs |
| Codepoint lookup | ~24 µs | ~30 µs | ~30 µs (1-font stack, MRU hits) | ~30 µs |
| Glyph emit | ~120 µs | ~120 µs | ~120 µs | ~120 µs |
| RTL reverse | 0 µs | 0 µs | 0 µs | ~5 µs (only on RTL strings) |
| **Total HUD pass** | ~144 µs | ~158 µs | ~158 µs | ~163 µs |

Budget: 300 µs. Estimated headroom: ~140 µs.

Worst-case stack pathology: a 2-font stack with a 200-character
all-Latin string (so MRU cache hits 100 %) is identical to a 1-font
stack — the MRU cache is what makes the design tractable.
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

| Atlas | Glyphs (counted via fc-query) | Estimated atlas size |
|---|---|---|
| Latin (existing ASCII range 0x20-0x7E) | 95 | 1 MB (512×512 R8) |
| Hebrew (Arimo coverage of U+0590-U+05FF) | 87 | ~0.6 MB |
| Basic Greek (Arimo coverage of U+0370-U+03FF) | 127 | ~0.9 MB |
| Polytonic Greek (Arimo coverage of U+1F00-U+1FFF) | 233 | ~1.5 MB |
| **Total estimated** | 542 | ~4 MB |

The four atlases are independent texture allocations (one per
loaded `Font` range — for the single-font Arimo stack, they share
one atlas built via a shelf packer in L2). The 4-MB total is a
ceiling estimate; the shelf packer may compress better. L2 test 6
pins the actual size.

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
- **The atlas image is rebuilt** (L2 swaps the packer from
  horizontal-strip to shelf). Texture id is stable; pixel
  layout inside the atlas changes. No external consumer reads
  the atlas pixels directly.

---

## 11. Alternatives considered

### Per-script font bundles (Noto Sans Hebrew + Noto Sans Greek)

Rev 1's plan. Rejected when fc-query showed Arimo already covers
all three scripts. Bundling Noto would duplicate ~280 KB of glyph
data and add a license-tracking obligation for zero rendering
benefit. Re-evaluate if typographic quality bar rises.

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

Full BiDi, full shaping, full normalization. Rejected: ~25 MB
binary cost, ~30 s extra build time, and the project ships a
narrow language set where the lightweight approach is correct.
ICU is the right answer when we ship Arabic — not before.

### Embed translations in source code (`tr("...")` as a literal)

Like `Qt::tr` — strings live at the call site and a tool
extracts them. Rejected: hides the canonical key list, makes
"are we missing translations for the Hebrew language" a
build-tool question rather than a glance at `he.json`. The
dot-key approach is more transparent for a small i18n surface.

---

## 12. Open questions for the reviewer

Blocking questions per CLAUDE.md rule 8 — surface before
implementation.

1. **Reference language fallback ordering.** § 5.6 defines
   `tr(key)` as "active → English → key". Is that right, or
   should missing-key behaviour be "render the key in a debug
   marker like `‹missing: ui.menu.X›`" so it's obvious in QA
   which strings are unfinished? The current proposal hides
   missing strings; the alternative makes them loud.
2. **Settings field placement.** Should `Settings::Localization`
   be its own top-level group, or fold into the existing
   `Settings::Accessibility` block? The accessibility menu is the
   natural UX home for "language picker" (it's adjacent to
   screen-reader-related settings), but the data is not strictly
   accessibility-only. (Pre-decided as own-top-level in § 4 inventory
   wording, but the reviewer's call.)
3. **Hebrew font aesthetic for v1.** Arimo Hebrew is a sans-serif
   workmanlike face — visually mismatched with Cormorant Garamond's
   serif aesthetic for biblical content. Three options:
   - (a) Ship Arimo only — simplest, design covers it.
   - (b) Bundle Frank Ruehl CLM (Public Domain, ~190 KB) as the
     biblical Hebrew face; FontStack swaps to it for Hebrew
     codepoints. Re-introduces the bundling overhead that this
     revision avoided.
   - (c) Defer the aesthetic concern — ship Arimo, plan the swap
     as a v2 polish item.
   Recommend (c).
4. **Localization audit in CI.** § 5.7 ships in lint-mode (warn-only)
   by default, with a `--strict` flag for the gate. Should the
   strict flag default to on in CI from day one? Tradeoff: "first
   translator-skipped string fails the build" vs. "we tolerate
   6 months of catching them in PR review".

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
2. **L2** — `FontStack`, multi-range Arimo load, shelf packer.
   Bigger change but still no consumer-side migration — the
   existing single-font path is just routed through a one-font
   stack.
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
  <https://tools.ietf.org/html/bcp47>

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
  design doc this one mirrors in structure (status block, scope
  split, CPU/GPU placement, verify-step plan, alternatives,
  references).
- `docs/phases/phase_10_settings_design.md` — the slice-13 design
  that established the schema-migration pattern this design hooks
  into.
- `docs/phases/phase_10_fog_design.md` — the slice-11 design that
  established the "estimates + benchmark pin in last slice" pattern
  this design follows.

### Tools used during design

- `fc-query` (fontconfig) — used 2026-05-18 to determine Arimo's
  Hebrew / Greek coverage (see § 4 inventory table).
- `ttx -t cmap` (fontTools) — used 2026-05-18 to count exact
  codepoint coverage in the cmap table (Hebrew: 87, basic Greek:
  127, polytonic: 233).
