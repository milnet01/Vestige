# Subsystem Specification — `engine/ui`

## Header

| Field | Value |
|-------|-------|
| Subsystem | `engine/ui` |
| Status | `shipped` |
| Spec version | `1.0` |
| Last reviewed | `2026-04-28` (initial draft — pending cold-eyes review) |
| Owners | `milnet01` |
| Engine version range | `v0.5.0+` (Phase 5 HUD foundation; expanded through Phase 10.7 / 10.9) |

---

## 1. Purpose

`engine/ui` is the in-game User Interface (UI) layer — the screen-space overlay every shipped game presents on top of the 3D scene. It owns the sprite batch renderer that draws 2D quads, the `UICanvas` element hierarchy, the widget catalogue (label / panel / image / button / slider / checkbox / dropdown / keybind row / progress bar / crosshair / Frames-Per-Second (FPS) counter / interaction prompt / world-anchored label / notification toast), the screen state machine (`GameScreen` — MainMenu / Playing / Paused / Settings / Loading / Exiting), the menu prefab factories that populate the canvas for each screen, the engine-wide `UITheme` palette + sizes, the closed-caption / subtitle pipeline (queue → layout → renderer + `CaptionMap` for clip-path-keyed lookup), the Web Accessibility Initiative — Accessible Rich Internet Applications (WAI-ARIA)-style accessibility metadata layer (`UIAccessibleInfo`), and the world-to-screen projection helper that anchors floating Heads-Up Display (HUD) labels to 3D positions. It exists as its own subsystem because the in-game runtime UI is deliberately separate from the editor's Dear ImGui — runtime UI ships in the binary every player sees, must respect a strict accessibility / theming contract, and is rendered through the engine's own batched 2D pipeline rather than an immediate-mode debug toolkit. For the engine's primary use case — first-person architectural walkthroughs of biblical structures — `engine/ui` is what draws the main menu, the pause menu, the crosshair, the "Press [E] to enter" interaction prompt, the autosave toast, and the dialogue caption when the narrator speaks.

## 2. Scope

| In scope | Out of scope |
|----------|--------------|
| `UICanvas` — flat root list of `std::unique_ptr<UIElement>` plus traversal / hit-test / accessibility-walk | The `UISystem` `ISystem` host — `engine/systems/ui_system.h` (the scope split is deliberate; the widget tree is reusable, the system is the wiring) |
| `UIElement` base + 14 concrete widgets (label, panel, image, button, slider, checkbox, dropdown, keybind row, progress bar, crosshair, FPS counter, world-label, interaction prompt, notification toast) | Editor panels + immediate-mode debug overlays — `engine/editor/` (Dear ImGui-driven; never reach the player) |
| `UITheme` + scale presets (`UIScalePreset` 1.0× / 1.25× / 1.5× / 2.0×) + Vellum / Plumbline registers + `withScale` / `withHighContrast` / `withReducedMotion` transforms | Concrete settings persistence + apply-sink machinery — `engine/core/settings*.h` (the sinks *call into* `UISystem::applyAccessibilityBatch` etc.; the storage and editor live in core) |
| `ui_contrast::relativeLuminance` / `contrastRatio` / `compositeOver` — Web Content Accessibility Guidelines (WCAG) 2.2 palette-correctness math used by tests + audits | Font atlas / text rasterisation — `engine/renderer/text_renderer.h` (ui only consumes `TextRenderer::renderText2D` / `measureTextWidth`) |
| `SpriteBatchRenderer` — batched 2D quad pipeline (Vertex Array Object / Vertex Buffer Object / Element Buffer Object — VAO / VBO / EBO, max 1000 quads per batch, ortho projection) | 3D scene rendering passes (geometry / lighting / post) — `engine/renderer/` |
| `GameScreen` enum + `GameScreenIntent` enum + `applyGameScreenIntent` pure transition function + label helpers + `isWorldSimulationSuspended` / `suppressesWorldInput` predicates | Modal stack + per-game integration of the screens (which screen is the cold-start root, which save slot to load) — `UISystem` and the game's `main` |
| `menu_prefabs::buildMainMenu` / `buildPauseMenu` / `buildSettingsMenu` / `buildDefaultHud` factories | Per-game settings categories beyond chrome (which sliders / dropdowns each game exposes) — game project code |
| `SubtitleQueue` — Fixed-capacity First-In First-Out (FIFO) caption queue, push-newest / drop-oldest, per-tick countdown, `setEnabled` consumer-visible flag, narrator style selector, size preset | Audio playback — `engine/audio/`. Captions are populated by an audio-event handler that calls `CaptionMap::enqueueFor` on clip start. |
| `CaptionMap` — JavaScript Object Notation (JSON)-loaded clip-path → caption template lookup, with fallback duration | The wider Settings → audio → caption wiring chain — `engine/audio/audio_system.h` is the producer; `engine/ui/` is the consumer / renderer |
| `SubtitleLayoutParams` + `computeSubtitleLayout` (pure-function layout) + `renderSubtitles` (sprite-batch + text-renderer dispatch) + `wrapSubtitleText` (greedy word-wrap with ellipsis tail) | Localisation / translation pipeline — strings are passed through verbatim; localisation is per-game |
| `UIAccessibleRole` + `UIAccessibleInfo` + `UIAccessibilitySnapshot` — flat ARIA-style metadata every widget carries; `UICanvas::collectAccessible` walk | The Text-To-Speech (TTS) bridge that consumes the snapshot — not yet shipped (`triage` open question §15) |
| `NotificationQueue` — capacity-3 FIFO toast queue with fade-in / fade-out envelope; `notificationAlphaAt` pure helper for tests | Game-event → notification routing (which gameplay events spawn toasts) — game project code |
| `projectWorldToScreen` — pure world-to-screen helper used by `UIWorldLabel` and `UIInteractionPrompt` | Camera math — `engine/renderer/camera.h` (UI consumes `Camera::getViewMatrix` etc.) |
| Keyboard-focus model on `UIElement::focused` (Phase 10.9 Slice 3 S4) — set by `UISystem::handleKey` Tab / arrow walk | The keyboard event source — `engine/core/event_bus.h` `KeyPressedEvent` (UI subscribes through `UISystem`) |

## 3. Architecture

```
                  ┌────────────────────────────────┐
                  │          UISystem              │
                  │  (engine/systems/ui_system.h)  │
                  │   ISystem, Render phase        │
                  └────────────┬───────────────────┘
                               │ owns / drives
   ┌──────────┬───────────┬────┴────┬───────────┬─────────────┬──────────────┐
   ▼          ▼           ▼         ▼           ▼             ▼              ▼
┌──────┐  ┌────────┐  ┌────────┐  ┌──────┐  ┌──────────┐  ┌──────────┐  ┌─────────┐
│Sprite│  │UICanvas│  │UICanvas│  │UITheme│  │GameScreen│  │Subtitle  │  │Notif.   │
│Batch │  │ root   │  │ modal  │  │+ base │  │+ stack   │  │Queue +   │  │Queue    │
│Rndr  │  │        │  │        │  │       │  │  state   │  │CaptionMap│  │ (toasts)│
└──┬───┘  └───┬────┘  └───┬────┘  └───┬───┘  └────┬─────┘  └────┬─────┘  └────┬────┘
   │ GL       │           │           │           │              │             │
   │ VAO/VBO  │ flat list │ flat list │ palette / │ pure intent  │ tick + pure │ tick + alpha
   ▼          ▼           ▼           │ sizes /   │ transition   │ layout      │ envelope
draw quads   tree of     same shape   │ motion    │ table        │ + draw      │
            UIElement    (modals)     ▼           ▼              ▼             ▼
            subclasses           withScale /                renderSubtitles   UINotification
                                 withHighContrast /        (sprite + text)   Toast widgets
                                 withReducedMotion
```

Key abstractions:

| Abstraction | Type | Purpose |
|-------------|------|---------|
| `UIElement` | abstract class | Base for every widget; owns position / size / anchor / visibility / interactivity / `focused` flag / accessibility metadata / child list. `engine/ui/ui_element.h:40` |
| `Anchor` | enum | 9-position anchor (TOP_LEFT … BOTTOM_RIGHT) used by `computeAbsolutePosition`. `engine/ui/ui_element.h:23` |
| `UICanvas` | class | Flat root list of `std::unique_ptr<UIElement>`; render traversal, hit test, accessibility walk. `engine/ui/ui_canvas.h:23` |
| `SpriteBatchRenderer` | class | Batched 2D quad renderer (max 1000 quads / batch); ortho projection; depth-test off; texture-grouped flushes. `engine/ui/sprite_batch_renderer.h:32` |
| `UITheme` | struct | Palette, sizes, type sizes, motion timing, font-family logical names; Vellum (default) + Plumbline registers; `withScale` / `withHighContrast` / `withReducedMotion` transforms. `engine/ui/ui_theme.h:31` |
| `UIScalePreset` | enum | 1.0× / 1.25× / 1.5× / 2.0× scaling presets. `engine/ui/ui_theme.h:149` |
| `ui_contrast::contrastRatio` | free function | WCAG 2.2 contrast-ratio math; symmetric, returns `[1, 21]`. `engine/ui/ui_theme.h:188` |
| `UIAccessibleRole` | enum | ARIA-style role tag (Button / Slider / Checkbox / …). `engine/ui/ui_accessible.h:37` |
| `UIAccessibleInfo` | struct | Role + label + description + hint + value — every `UIElement` carries one. `engine/ui/ui_accessible.h:62` |
| `UIAccessibilitySnapshot` | struct | One entry in the flattened accessibility walk. `engine/ui/ui_accessible.h:77` |
| `Signal<Args...>` | template class | Lightweight slot list used by widget callbacks (`onClick`, `onHover`). `engine/ui/ui_signal.h:23` |
| `GameScreen` | enum | Top-level shipped-game screen (None / MainMenu / Loading / Playing / Paused / Settings / Exiting). `engine/ui/game_screen.h:60` |
| `GameScreenIntent` | enum | Caller-fired transition request; pure function maps `(screen, intent) → screen`. `engine/ui/game_screen.h:77` |
| `applyGameScreenIntent` | free function | Total, pure, side-effect-free transition table. `engine/ui/game_screen.h:102` |
| `menu_prefabs::buildMainMenu` etc. | free functions | Canvas-population factories matching the Claude Design hand-off. `engine/ui/menu_prefabs.h:35` |
| `SubtitleQueue` | class | FIFO of active captions with per-tick countdown, size preset, narrator-style preset, `setEnabled` consumer-visible flag. `engine/ui/subtitle.h:165` |
| `Subtitle` / `ActiveSubtitle` | struct | Authored caption + queue-side countdown view. `engine/ui/subtitle.h:125` / `:153` |
| `wrapSubtitleText` | free function | Greedy word-wrap to `SUBTITLE_SOFT_WRAP_CHARS` (40) with `SUBTITLE_MAX_LINES` (2) cap and Unicode ellipsis tail. `engine/ui/subtitle.h:119` |
| `CaptionMap` | class | JSON-loaded `clipPath → Subtitle` lookup; `enqueueFor(clip, queue)`. `engine/ui/caption_map.h:49` |
| `SubtitleLayoutParams` / `SubtitleLineLayout` | struct | Pure layout-pass inputs / outputs. `engine/ui/subtitle_renderer.h:63` / `:78` |
| `computeSubtitleLayout` | free function | Pure layout — no GL — testable with a stub width-measurer. `engine/ui/subtitle_renderer.h:113` |
| `renderSubtitles` | free function | Plate quads + text glyphs through `SpriteBatchRenderer` + `TextRenderer`. `engine/ui/subtitle_renderer.h:132` |
| `NotificationQueue` | class | Capacity-3 FIFO toast queue; `advance(dt, fadeSeconds)`; alpha envelope per entry. `engine/ui/ui_notification_toast.h:87` |
| `notificationAlphaAt` | free function | Pure fade-envelope math; reduced-motion = step function. `engine/ui/ui_notification_toast.h:152` |
| `UINotificationToast` | class | Widget that renders one `ActiveNotification` as panel + accent strip + title + body. `engine/ui/ui_notification_toast.h:168` |
| `projectWorldToScreen` | free function | Pure world-to-screen with frustum cull; testable headlessly. `engine/ui/ui_world_projection.h:35` |
| Concrete widgets | class | `UILabel`, `UIPanel`, `UIImage`, `UIButton`, `UISlider`, `UICheckbox`, `UIDropdown`, `UIKeybindRow`, `UIProgressBar`, `UICrosshair`, `UIFpsCounter`, `UIWorldLabel`, `UIInteractionPrompt`. Each `engine/ui/ui_*.h`. |

## 4. Public API

The subsystem has 8+ public headers (every `ui_*.h` in `engine/ui/` plus the `subtitle*` / `caption_map` / `menu_prefabs` / `sprite_batch_renderer` family). The facade is grouped per header below.

```cpp
// engine/ui/ui_element.h — base class + Anchor + child tree.
enum class Anchor { TOP_LEFT, …, BOTTOM_RIGHT };  // 9 anchor positions.
class UIElement {
public:
    virtual void render(SpriteBatchRenderer&, const glm::vec2& parentOffset,
                        int screenWidth, int screenHeight) = 0;
    virtual bool hitTest(const glm::vec2& point, const glm::vec2& parentOffset,
                         int screenWidth, int screenHeight) const;
    glm::vec2  computeAbsolutePosition(const glm::vec2& parentOffset, int w, int h) const;
    void       addChild(std::unique_ptr<UIElement>);
    UIElement* getChildAt(size_t index);            // S4: tab-order walk
    UIAccessibleInfo&       accessible();           // public field; S4 keyboard-focus
    virtual void collectAccessible(std::vector<UIAccessibilitySnapshot>& out) const;
    glm::vec2 position, size; Anchor anchor; bool visible, interactive, focused;
    Signal<> onClick, onHover;
};
```

```cpp
// engine/ui/ui_canvas.h — root container.
class UICanvas {
public:
    void      addElement(std::unique_ptr<UIElement>);
    void      clear();
    size_t    getElementCount() const;
    void      render(SpriteBatchRenderer&, int w, int h);
    bool      hitTest(const glm::vec2& point, int w, int h) const;
    UIElement*       getElementAt(size_t index);   // editor-panel inspector
    std::vector<UIAccessibilitySnapshot> collectAccessible() const;
};
```

```cpp
// engine/ui/ui_theme.h — palette + sizes + accessibility transforms + WCAG math.
struct UITheme {
    // palette: bg / panel / stroke / text / accent / hud (~25 fields)
    // sizes: button / slider / checkbox / dropdown / keybind / row (~20 fields)
    // type: typeDisplay / H1 / H2 / body / button / caption / micro
    // motion: transitionDuration, focusRingThickness, focusRingOffset
    static UITheme defaultTheme();        // Vellum register (warm parchment on walnut).
    static UITheme plumbline();           // alternative monastic register.
    UITheme withScale(float factor) const;          // sizes only.
    UITheme withHighContrast() const;     // palette only — pure black/white + saturated accent.
    UITheme withReducedMotion() const;    // zeroes transitionDuration.
};
enum class UIScalePreset { X1_0, X1_25, X1_5, X2_0 };
float scaleFactorOf(UIScalePreset);
namespace ui_contrast {
    float     relativeLuminance(const glm::vec3& srgb);
    float     contrastRatio(const glm::vec3& a, const glm::vec3& b);
    glm::vec3 compositeOver(const glm::vec4& fg, const glm::vec3& bg);
}
```

```cpp
// engine/ui/ui_accessible.h — ARIA-flavoured metadata.
enum class UIAccessibleRole { Unknown, Label, Panel, Image, Button, Checkbox,
                              Slider, Dropdown, KeybindRow, ProgressBar, Crosshair };
struct UIAccessibleInfo { UIAccessibleRole role; std::string label, description,
                          hint, value; };
struct UIAccessibilitySnapshot { UIAccessibleInfo info; bool interactive; };
const char* uiAccessibleRoleLabel(UIAccessibleRole);
```

```cpp
// engine/ui/sprite_batch_renderer.h — batched 2D quad pipeline.
class SpriteBatchRenderer {
public:
    static constexpr int MAX_QUADS = 1000;
    bool initialize(const std::string& assetPath);
    void shutdown();
    void begin(int screenWidth, int screenHeight);
    void drawQuad(const glm::vec2& pos, const glm::vec2& size, const glm::vec4& color);
    void drawTexturedQuad(const glm::vec2& pos, const glm::vec2& size,
                          GLuint tex, const glm::vec4& tint = glm::vec4(1));
    void end();
};
```

```cpp
// engine/ui/game_screen.h — pure-function state machine (no GL, no allocations).
enum class GameScreen     { None, MainMenu, Loading, Playing, Paused, Settings, Exiting };
enum class GameScreenIntent { OpenMainMenu, NewWalkthrough, Continue, OpenSettings,
                              CloseSettings, Pause, Resume, QuitToMain, QuitToDesktop,
                              LoadingComplete };
GameScreen   applyGameScreenIntent(GameScreen, GameScreenIntent);  // total, pure.
const char*  gameScreenLabel(GameScreen);
const char*  gameScreenIntentLabel(GameScreenIntent);
bool         isWorldSimulationSuspended(GameScreen);
bool         suppressesWorldInput(GameScreen);
```

```cpp
// engine/ui/menu_prefabs.h — canvas factories.
void buildMainMenu     (UICanvas&, const UITheme&, TextRenderer*);
void buildMainMenu     (UICanvas&, const UITheme&, TextRenderer*, UISystem&);  // wires onClick → applyIntent
void buildPauseMenu    (UICanvas&, const UITheme&, TextRenderer*);
void buildPauseMenu    (UICanvas&, const UITheme&, TextRenderer*, UISystem&);
void buildSettingsMenu (UICanvas&, const UITheme&, TextRenderer*);
void buildSettingsMenu (UICanvas&, const UITheme&, TextRenderer*, UISystem&);
void buildDefaultHud   (UICanvas&, const UITheme&, TextRenderer*, UISystem&);
```

```cpp
// engine/ui/subtitle.h — caption queue.
enum class SubtitleCategory      { Dialogue, Narrator, SoundCue };
enum class SubtitleSizePreset    { Small, Medium, Large, XL };
enum class SubtitleNarratorStyle { Italic, Colour };
inline constexpr std::size_t SUBTITLE_SOFT_WRAP_CHARS = 40;
inline constexpr std::size_t SUBTITLE_MAX_LINES       = 2;
std::vector<std::string> wrapSubtitleText(const std::string&,
                                          std::size_t maxChars = 40,
                                          std::size_t maxLines = 2);
struct Subtitle       { std::string text, speaker; float durationSeconds = 3;
                        SubtitleCategory category; float directionDegrees = -1; };
struct ActiveSubtitle { Subtitle subtitle; float remainingSeconds; };
class SubtitleQueue {
public:
    static constexpr int DEFAULT_MAX_CONCURRENT = 3;
    void enqueue(const Subtitle&);
    void tick(float dt);
    const std::vector<ActiveSubtitle>& activeSubtitles() const;  // empty when disabled
    std::size_t size() const; bool empty() const;
    void setEnabled(bool); bool isEnabled() const;
    void clear();
    int  maxConcurrent() const; void setMaxConcurrent(int);
    SubtitleSizePreset sizePreset() const; void setSizePreset(SubtitleSizePreset);
    SubtitleNarratorStyle narratorStyle() const; void setNarratorStyle(SubtitleNarratorStyle);
};
```

```cpp
// engine/ui/caption_map.h — declarative clip → caption.
constexpr float DEFAULT_CAPTION_DURATION_SECONDS = 3.0f;
class CaptionMap {
public:
    bool             loadFromFile(const std::string& path);
    bool             loadFromString(const std::string& json);
    const Subtitle*  lookup(const std::string& clipPath) const;
    bool             enqueueFor(const std::string& clipPath, SubtitleQueue&) const;
    std::size_t      size() const; bool empty() const; void clear();
};
SubtitleCategory parseSubtitleCategory(const std::string&);
```

```cpp
// engine/ui/subtitle_renderer.h — pure layout + GL dispatch.
struct SubtitleStyle        { glm::vec3 textColor, speakerColor; glm::vec4 plateColor; bool italic; };
struct SubtitleLayoutParams { int screenW, screenH, fontPixelSize; float baseReferencePx,
                              referenceHeight, bottomMarginFrac, platePaddingX, platePaddingY,
                              lineSpacingPx; };
struct SubtitleLineLayout   { std::string fullText; std::vector<std::string> wrappedLines;
                              glm::vec2 platePos, plateSize; glm::vec4 plateColor;
                              glm::vec2 textBaseline; float lineStepPx, textScale;
                              glm::vec3 textColor; SubtitleCategory category; bool italic; };
SubtitleStyle styleFor(SubtitleCategory, SubtitleNarratorStyle = SubtitleNarratorStyle::Colour);
std::vector<SubtitleLineLayout> computeSubtitleLayout(
    const SubtitleQueue&, const SubtitleLayoutParams&,
    const std::function<float(const std::string&)>& measureTextWidthPx);
void renderSubtitles(const std::vector<SubtitleLineLayout>&,
                     SpriteBatchRenderer&, TextRenderer&, int w, int h);
```

```cpp
// engine/ui/ui_notification_toast.h — toast queue + widget.
enum class NotificationSeverity { Info, Success, Warning, Error };
struct Notification       { std::string title, body; NotificationSeverity severity;
                            float durationSeconds = 4; };
struct ActiveNotification { Notification data; float elapsedSeconds, alpha; };
class NotificationQueue {
public:
    static constexpr std::size_t DEFAULT_CAPACITY     = 3;
    static constexpr float       DEFAULT_FADE_SECONDS = 0.14f;
    void push(const Notification&);
    void advance(float dt, float fadeSeconds = DEFAULT_FADE_SECONDS);
    const std::vector<ActiveNotification>& active() const;
    std::size_t size() const;
    std::size_t capacity() const;
    void        setCapacity(std::size_t);
};
float     notificationAlphaAt(float elapsed, float duration, float fade);  // pure
glm::vec4 notificationSeverityColor(NotificationSeverity, const UITheme&);
class UINotificationToast : public UIElement { /* render + updateFromNotification + alpha */ };
```

```cpp
// engine/ui/ui_world_projection.h — pure helper for world-anchored UI.
struct WorldToScreenResult { bool visible; glm::vec2 screenPos; float ndcDepth; };
WorldToScreenResult projectWorldToScreen(const glm::vec3& worldPos,
                                         const glm::mat4& viewProj,
                                         int screenWidth, int screenHeight);
```

**Widget headers** — each is a `UIElement` subclass with public data members; see headers for fields.

| Header | Role |
|--------|------|
| `ui_label.h` | text label — TextRenderer driven |
| `ui_panel.h` | solid-colour rectangle |
| `ui_image.h` | textured quad |
| `ui_button.h` | `.btn` — DEFAULT / PRIMARY / GHOST / DANGER + small + shortcut keycap |
| `ui_slider.h` | track + fill + thumb + value readout; external drag handling |
| `ui_checkbox.h` | 20 × 20 with stroke + checkmark + inline label |
| `ui_dropdown.h` | 40-px combo with popup; selectedIndex external mutation |
| `ui_keybind_row.h` | label + keycap + CLEAR; "PRESS ANY KEY…" listening state |
| `ui_progress_bar.h` | horizontal value / maxValue fill — clamps at render time |
| `ui_crosshair.h` | centred reticle (length / thickness / centreGap) |
| `ui_fps_counter.h` | smoothed-FPS readout via `tick(dt)` |
| `ui_world_label.h` | floating world-anchored text via `projectWorldToScreen` |
| `ui_interaction_prompt.h` | `UIWorldLabel` subclass: "Press [keyLabel] to actionVerb" with distance fade |

**Non-obvious contract details:**

- `UIElement` is **non-copyable** (deleted copy ctor / assign). Children are owned via `unique_ptr`.
- `UIElement::accessible()` is a **public field accessor** rather than getter / setter pair; callsites assign `btn->accessible().label = "Play Game"`. Widgets set `role` in their constructors.
- `UICanvas::collectAccessible()` **skips entire invisible subtrees** and **skips the element itself** when it has neither a role nor a label — the snapshot is the announce-able subset.
- `SpriteBatchRenderer::begin` / `end` are required around every batch — calling `drawQuad` outside the active range is undefined. The renderer **does not own GL state** between frames; consumers must turn depth-test off and set src-alpha / one-minus-src-alpha blend before calling `begin` (UISystem does this).
- `UITheme::withScale` touches **size fields only** — palette, motion timing, and font-family names are unchanged. `withReducedMotion` zeroes `transitionDuration` only — palette and sizes unchanged. The three transforms compose in any order; `UISystem::applyAccessibilityBatch` applies all three then rebuilds in one pass.
- `UIScalePreset` and `SubtitleSizePreset` share the same 1.0× / 1.25× / 1.5× / 2.0× ladder **deliberately** — users tune both with one mental model. The two are independent settings: a partially-sighted user can pick XL captions with a 1.0× UI scale or Small captions with a 2.0× UI scale.
- `SubtitleQueue::enqueue` is **push-newest, drop-oldest** when at `maxConcurrent` — current dialogue wins over stale lines.
- `SubtitleQueue::setEnabled(false)` hides every entry from `activeSubtitles()` / `size()` / `empty()` **without clearing internal storage** — re-enabling restores any unexpired lines (Phase 10.9 P5 closed the gap where the apply-sink wrote a flag nothing read).
- `CaptionMap::loadFromFile` treats **missing file as success** with an empty map — games that ship without captions don't get warning spam. Malformed JSON logs a warning and leaves the map empty.
- `wrapSubtitleText` **hard-breaks** overlong tokens at the limit rather than pushing whole on a new line (would otherwise guarantee plate overflow). Truncated tail is suffixed with U+2026 ellipsis so the user sees content was cut, not silently lost.
- `applyGameScreenIntent` is **total** — every (screen, intent) pair returns a value. Invalid combinations return the input screen unchanged. Callers can fire intents defensively without branching on current state.
- `UIInteractionPrompt::computeFadeAlpha` is **piecewise linear** — 1.0 at or below `fadeNear`, 0.0 at or above `fadeFar`, linear interpolation between. Render is skipped when alpha is 0.
- `NotificationQueue::advance` with `fadeSeconds <= 0` (reduced motion) collapses the envelope to a step function: alpha is 1.0 for the full duration, then 0.0.
- `notificationAlphaAt` is **clamped to [0, 1]**; safe to call with negative `elapsed` or `elapsed > duration`.

**Stability:** the facade is semver-frozen for `v0.x`. Three known evolution points: (a) the TTS bridge that consumes `collectAccessible` is unbuilt — adding it is additive (§15 Q1); (b) italic narrator caption rendering depends on font-atlas oblique support not yet present — `SubtitleNarratorStyle::Italic` works through vertex-shear today and may switch to a true italic atlas later (§15 Q2); (c) the modal-stack is currently a flat `std::vector<GameScreen>` on `UISystem` — multi-modal compositing (e.g. a confirm dialog over the Settings overlay) is unprototyped (§15 Q3).

## 5. Data Flow

**Steady-state per-frame (`UISystem::update` then `UISystem::renderUI`, both main-thread):**

1. `EventBus::publish<KeyPressedEvent>` (from `engine/core/InputManager`) → `UISystem::handleKey(key, mods)` → consumes Tab / Shift-Tab / arrows / Enter / Space against the active canvas tab order; sets `UIElement::focused`; fires the focused element's `onClick` on activation.
2. `EventBus::publish<MouseMoveEvent>` → `UISystem::updateMouseHit(cursor, w, h)` → `UICanvas::hitTest` against root + topmost modal canvas → toggles `m_cursorOverInteractive`.
3. `UISystem::wantsCaptureInput()` reads `m_modalCapture || m_cursorOverInteractive` — game-input handlers consult and skip movement / look bindings while it is true.
4. `UISystem::update(dt)` → `m_notifications.advance(dt, theme.transitionDuration)` (per-frame fade math) → tick widget-internal smoothing (`UIFpsCounter::tick(dt)`).
5. Scene render passes execute (3D geometry / lighting / post). `engine/ui` produces nothing during these passes.
6. `UISystem::renderUI(w, h)` (main thread, after the 3D composite):
   a. Set GL state — disable depth test, enable src-alpha / one-minus-src-alpha blend.
   b. `m_spriteBatch.begin(w, h)` → ortho projection from `(0,0)` top-left to `(w,h)` bottom-right.
   c. `m_canvas.render(m_spriteBatch, w, h)` — root walks every `UIElement` in insertion order; widgets emit `drawQuad` / `drawTexturedQuad` calls and recurse into children.
   d. If `!m_modalStack.empty()` → `m_modalCanvas.render(m_spriteBatch, w, h)` — modal draws on top.
   e. Subtitle pass: `computeSubtitleLayout(queue, params, &TextRenderer::measureTextWidth)` → `renderSubtitles(layout, batch, textRenderer, w, h)` — plates batch with everything else, then a flush, then the text-renderer dispatch.
   f. `m_spriteBatch.end()` — flushes any pending draw, restores nothing (caller's job).
7. `engine/core/Window::swapBuffers` (next frame).

**Caption flow (audio event → caption on screen):**

1. Game / audio code resolves a clip path string (e.g. `"audio/dialogue/moses_01.wav"`).
2. `engine/audio/AudioSystem` (or the playback site) calls `Engine::getCaptionMap().enqueueFor(clipPath, Engine::getSubtitleQueue())` — no-op if the clip has no mapped caption.
3. Per-frame: `Engine::update` calls `Engine::getSubtitleQueue().tick(dt)` (driven from the engine loop, not `UISystem`, because the queue is engine-owned per the Phase 10.7 design — `UISystem::renderUI` only reads the queue).
4. `UISystem::renderUI` runs the layout → render path described above. Hidden when `setEnabled(false)`.

**Screen-stack flow (intent → canvas rebuild):**

1. Caller (button onClick / hotkey / engine event) → `UISystem::applyIntent(intent)`.
2. `UISystem` computes the new screen via `applyGameScreenIntent(currentRoot, intent)`.
3. Routing decision:
   - `OpenSettings` from non-modal → `pushModalScreen(Settings)` → modal canvas built via `m_screenBuilders[Settings]` (or default `buildSettingsMenu`); `m_modalCapture = true`; emit `onModalPushed`.
   - `CloseSettings` while modal → `popModalScreen()` → if stack now empty, `m_modalCapture = false`; emit `onModalPopped`.
   - Other transitions → `setRootScreen(next)` → root canvas cleared and rebuilt; emit `onRootScreenChanged`.
4. The new root / modal canvas is rendered next frame.

**Cold start (engine bring-up — `UISystem::initialize(Engine&)`):**

1. `m_spriteBatch.initialize(engine.getAssetPath())` — loads `shaders/sprite.vert` / `sprite.frag`, allocates VAO / VBO / EBO sized for `MAX_QUADS`.
2. Stash `engine.getRenderer().getTextRenderer()` into every prefab path that needs glyph rendering.
3. `m_baseTheme = UITheme::defaultTheme()` (Vellum); `m_theme = m_baseTheme`.
4. Apply-sinks (built by `Engine` from `Settings`) now call `setScalePreset` / `setHighContrastMode` / `setReducedMotion` (or the batched `applyAccessibilityBatch`) — each call invokes `rebuildTheme()`.
5. Game code drives the cold-start screen (`setRootScreen(MainMenu)` for shipped builds; the editor leaves it `None`).

**Shutdown:** `m_spriteBatch.shutdown()` releases GL resources; `m_canvas.clear()` and `m_modalCanvas.clear()` drop every owned widget. `SubtitleQueue` and `NotificationQueue` are clear-on-destroy via std-vector.

**Exception path:** none thrown from `engine/ui` in steady-state. Init failures (shader load) surface as `SpriteBatchRenderer::initialize` returning `false` → `UISystem::initialize` returns `false` → `SystemRegistry` rolls back the prefix.

## 6. CPU / GPU placement

UI rendering touches the GPU via `SpriteBatchRenderer` and `TextRenderer`; UI logic, layout, hit-test, state machine, queue ticking, and accessibility walks are CPU-only.

| Workload | Placement | Reason |
|----------|-----------|--------|
| 2D quad rasterisation (`SpriteBatchRenderer::flush`) | GPU (vertex + fragment shader) | Per-vertex transform + per-pixel shade — exact CODING_STANDARDS §17 default. Batched to ≤ 1000 quads per draw call to amortise state-change cost. |
| Glyph rasterisation (`TextRenderer::renderText2D`) | GPU (per-glyph quad + atlas sample) | Per-pixel atlas lookup — GPU. The glyph atlas itself is built once on the CPU (`engine/renderer/text_renderer.h`). |
| Layout, hit-test, anchor math, child walks | CPU (main thread) | Branching, sparse, decision-heavy — exactly the §17 CPU heuristic. Element counts are small (< 100 per canvas in practice). |
| `UICanvas::render` traversal | CPU (main thread) | Pointer-chase through `unique_ptr` tree — sparse, branching, not data-parallel. Emits the GPU work indirectly via batch enqueue. |
| `SubtitleQueue::tick`, `NotificationQueue::advance`, `UIFpsCounter::tick` | CPU (main thread) | Per-frame countdown / smoothing — trivial scalar math on tiny arrays (≤ 3 entries). GPU dispatch overhead would dominate. |
| `computeSubtitleLayout` (pure layout pass) | CPU (main thread, per-frame) | String measurement + line wrap + plate sizing — branching, I/O-shaped. Pure-function design specifically lets it run headlessly for tests. |
| `wrapSubtitleText` | CPU (main thread, per-caption) | Greedy word-wrap loop over a short string. Pure C++. |
| `applyGameScreenIntent` (state machine) | CPU (main thread, per intent) | Pure switch / table lookup. Total function. |
| `ui_contrast::contrastRatio` | CPU (build / test / audit) | Verifies palette correctness in CI — not on the per-frame path. |
| `projectWorldToScreen` | CPU (main thread, per world-label) | A handful of `mat4 * vec4` per labelled in-world prompt — tiny count, branchy frustum test, not worth a GPU kernel. |
| `UIElement::collectAccessible` walk | CPU (main thread, on demand) | Tree walk, string copies — sparse. Driven by editor panel or future TTS bridge, not per-frame. |

No dual CPU / GPU implementations. The pure layout / intent functions are deliberately CPU-only so tests don't need GL — that is the testability split, not a parity-test split.

## 7. Threading model

Per CODING_STANDARDS §13. UI is **main-thread-only**. There is no worker entry point in `engine/ui`; the audio thread does not touch the subtitle queue (the queue is tick-driven from the main loop, and audio-side caption enqueue happens via the engine update path, not from the audio worker — `engine/audio` posts an event, the main thread consumes it).

| Caller thread | Allowed APIs | Locks held |
|---------------|--------------|------------|
| Main thread | All of `engine/ui` — every widget render path, every queue mutation, the sprite batch, the canvas, the screen stack, the theme transforms. | None — `engine/ui` carries no internal mutex. |
| Worker threads | None. Calls into `engine/ui` from a worker are undefined. | n/a |

**Lock-free / atomic:** none required. The only multi-thread coupling is read-only consumption of theme / settings state by widgets via raw `const UITheme*` pointers — those pointers are written once at init and on settings apply (also main-thread); widgets read them during render (main-thread). No race possible.

**GL context:** the sprite batch holds GL handles — it must be initialised, used, and shut down on the GL context thread (the main thread, per `engine/core` contract).

## 8. Performance budget

60 FPS hard requirement → 16.6 ms per frame. UI is overhead-only — it shares the budget with the renderer, physics, audio, and every domain system. **The Budget column states the engineering targets** (validated qualitatively by visual + smoke testing); the Measured column is uniformly TBD pending the Phase 11 audit (Open Q6) — no fabricated numbers, no capture run yet.

| Path | Budget | Measured (RX 6600, 1080p) |
|------|--------|----------------------------|
| `UISystem::update(dt)` (notification advance + smoothing) | < 0.05 ms | TBD — measure by Phase 11 audit |
| `UISystem::renderUI` overall (HUD only, ≤ 8 elements) | < 0.5 ms | TBD — measure by Phase 11 audit |
| `UISystem::renderUI` overall (full menu canvas, ≈ 30–40 widgets) | < 1.5 ms | TBD — measure by Phase 11 audit |
| `SpriteBatchRenderer` draw-call count (typical HUD) | ≤ 4 draws | TBD — RenderDoc capture pending |
| `SpriteBatchRenderer` draw-call count (full menu) | ≤ 12 draws | TBD — RenderDoc capture pending |
| `computeSubtitleLayout` (3 captions, 2 lines each) | < 0.05 ms | TBD — measure by Phase 11 audit |
| `SubtitleQueue::tick` + `CaptionMap::enqueueFor` | < 0.01 ms | TBD — measure by Phase 11 audit |
| `UICanvas::hitTest` (cursor over canvas of 40 widgets) | < 0.05 ms | TBD — measure by Phase 11 audit |
| `UISystem::handleKey` Tab walk (canvas of 40 widgets) | < 0.05 ms | TBD — measure by Phase 11 audit |
| `applyIntent` + canvas rebuild (root-screen change) | < 5 ms (one-shot, not per-frame) | TBD — measure by Phase 11 audit |

Profiler markers / RenderDoc capture points: `SpriteBatchRenderer::flush` should be wrapped in a `glPushDebugGroup("UI-Sprite-Batch")` (CODING_STANDARDS §29 — not yet added; see §15 Q4). The subtitle render pass should be `glPushDebugGroup("UI-Subtitles")` similarly. Greppable profiler labels: `UI`, `UI-Subtitles`, `UI-Notifications`.

## 9. Memory

| Aspect | Value |
|--------|-------|
| Allocation pattern | Heap (every widget owned via `std::unique_ptr<UIElement>`); per-frame **vertex scratch** in `SpriteBatchRenderer::m_vertices` (reused across frames so the hot path doesn't reallocate). No arena, no per-frame transient allocator. |
| Reusable per-frame buffers | `SpriteBatchRenderer::m_vertices` (capacity = `MAX_VERTICES` = 4000); subtitle layout vector + notification active vector (both cap to 3 entries by default). |
| Peak working set | Negligible. Sprite-batch vertex / index buffers ≈ 96 KB GPU + 96 KB CPU; theme + caption map + queues a few KB; canvases dominate (≈ 2 KB per shipped widget × ≈ 50 widgets per menu = 100 KB). Single-digit MB across every UI structure even with all menus + HUD live. |
| Ownership | `UISystem` owns `m_spriteBatch`, `m_canvas`, `m_modalCanvas`, `m_baseTheme`, `m_theme`, `m_notifications`, `m_screenBuilders`, `m_modalStack`, `m_focusedElement` (raw, non-owning — points into the canvases). `Engine` owns the `SubtitleQueue` + `CaptionMap` (per Phase 10.7 design — they cross UI / audio so they live on the engine). |
| Lifetimes | Engine-lifetime — every `engine/ui` allocation lives from `UISystem::initialize` until `UISystem::shutdown`. Per-canvas widgets recycle on screen change (`m_modalCanvas.clear()` + rebuild). |

No `new` / `delete` outside `unique_ptr` factories (CODING_STANDARDS §12). GL handles are RAII-wrapped by the sprite batch.

## 10. Error handling

Per CODING_STANDARDS §11 — no exceptions in the steady-state path.

| Failure mode | Reported as | Caller's recourse |
|--------------|-------------|-------------------|
| Sprite-batch shader missing / failed to compile in `initialize` | `bool` return false + `Logger::error` | `UISystem::initialize` returns false → `SystemRegistry` rolls back the prefix. UI unavailable; engine logs and may continue without UI (per `engine/core` Open Q1 — current behaviour). |
| Theme asset missing (e.g. font-family logical name has no atlas) | `Logger::warning`, fall back to the renderer's default font; theme strings are logical names, not paths — failures are downstream in `engine/renderer/text_renderer.h` | Game project ships a fallback font in the asset pack; user sees boxed glyphs only on truly missing fonts. |
| `CaptionMap::loadFromFile` — missing file | Returns `false`, leaves map empty; **no warning** (a project without captions is valid) | None — silent success. |
| `CaptionMap::loadFromFile` / `loadFromString` — malformed JSON | Returns `false`, `Logger::warning` with the parse error, leaves map empty | None — game proceeds without captions for affected entries. |
| Modal stack invariant — `popModalScreen()` on empty stack | No-op; safe | None. |
| Modal stack invariant — `pushModalScreen(None)` | No-op; safe (None has no builder) | None. |
| Modal stack invariant — push when `m_screenBuilders[screen]` resolves to empty (e.g. no default for `Loading`) | The modal canvas is cleared and rendered as an empty canvas; modal capture still engages | Game project registers a custom builder via `setScreenBuilder`. |
| Focus ring lost — `setFocusedElement(el)` with `el` that is no longer in either canvas | Behaviour is undefined per `setFocusedElement` doc — caller's responsibility | Best practice: clear focus (`setFocusedElement(nullptr)`) before clearing a canvas. |
| Focus ring lost — element with `focused = true` is hidden (`visible = false`) | Element keeps `focused = true` but renders nothing; `handleKey` Tab walk skips invisible subtrees and will move focus on next Tab | Not strictly a bug; consider clearing focus when toggling visibility on the focused element. |
| Caption queue overflow | `SubtitleQueue::enqueue` evicts the oldest active entry (push-newest, drop-oldest) | None — the eviction is the policy. |
| Notification queue overflow | `NotificationQueue::push` evicts the oldest (same policy) | None — the eviction is the policy. |
| Negative `deltaTime` to `tick` / `advance` | Treated as 0 internally | None. |
| `wrapSubtitleText` with a token longer than `maxCharsPerLine` | Hard-breaks the token at the limit | None — preferable to silent overflow. |
| `applyGameScreenIntent` with an invalid (screen, intent) pair | Returns the input screen unchanged (no-op) | None — the no-op is the policy; `UISystem::applyIntent` skips the canvas rebuild. |
| Programmer error (null `TextRenderer*` on a widget that needs it) | Widget skips its glyph render silently (matches `menu_prefabs` test-tolerance contract) | Wire the text renderer via `UISystem::setTextRenderer` or the widget's own setter. |
| Out of memory | `std::bad_alloc` propagates | App aborts (CODING_STANDARDS §11). |

`Result<T, E>` / `std::expected` not yet used in `engine/ui` — the subsystem predates the policy. Migration tracked on the engine-wide debt list, not §15.

## 11. Testing

| Concern | Test file | Coverage |
|---------|-----------|----------|
| Theme + accessibility transforms + WCAG contrast math | `tests/test_ui_theme_accessibility.cpp` | `withScale` / `withHighContrast` / `withReducedMotion` composition + palette luminance / contrast assertions |
| Hit-test math + nested anchors + invisible-subtree exclusion | `tests/test_ui_hit_test.cpp` | `UICanvas::hitTest` + `UIElement::hitTest` |
| Layout-side widget rendering (no GL) | `tests/test_ui_layout_panel.cpp` | Anchor / size / parent-offset math via stub batch |
| Runtime widgets — design-spec parity (button / slider / checkbox / dropdown / keybind row / label / panel / image / progress bar / FPS / world-label / interaction prompt) | `tests/test_ui_design_widgets.cpp`, `tests/test_ui_widgets.cpp`, `tests/test_ui_runtime_panel.cpp` | Constructor defaults, `effectiveHeight`, `ratio`, `getRatio`, `composedText`, `computeFadeAlpha`, `currentLabel` |
| Accessibility metadata — role assignment + collectAccessible walk skipping invisible / unannouncable | `tests/test_ui_accessible.cpp` | `UIElement::collectAccessible`, `UICanvas::collectAccessible`, snapshot interactivity flag |
| Keyboard-focus tab walk + arrow / Enter / Space activation | `tests/test_ui_focus_navigation.cpp` | `UISystem::handleKey` Tab order, modal trap, invisible skip |
| Game-screen state-machine totality + transition table | `tests/test_game_screen.cpp` | Every (screen, intent) combination |
| Screen stack push / pop / modal capture / canvas rebuild | `tests/test_ui_system_screen_stack.cpp` | `UISystem::applyIntent`, builder override, signals |
| Menu prefabs — element counts, button-onClick wiring, default-HUD composition | `tests/test_menu_prefabs.cpp` | `buildMainMenu` / `buildPauseMenu` / `buildSettingsMenu` / `buildDefaultHud` |
| Subtitle queue — enqueue / tick / overflow / size preset / narrator style / setEnabled | `tests/test_subtitle.cpp` | All `SubtitleQueue` API |
| Subtitle narrator-style branching (italic vs colour) | `tests/test_subtitle_narrator_style.cpp` | `styleFor`, layout `italic` flag |
| Subtitle layout pass + word-wrap + ellipsis + plate sizing | `tests/test_subtitle_renderer.cpp` | `wrapSubtitleText`, `computeSubtitleLayout` |
| Caption map — JSON parse + lookup + enqueueFor | `tests/test_caption_map.cpp` | `loadFromFile`, `loadFromString`, `enqueueFor`, malformed-JSON tolerance |
| Notification queue — push / advance / fade envelope / capacity / reduced-motion | `tests/test_notification_queue.cpp` | `NotificationQueue`, `notificationAlphaAt` step-function path |
| World-to-screen projection + frustum cull | `tests/test_ui_world_projection.cpp` | `projectWorldToScreen` |
| `UISystem` ↔ EventBus / InputManager input plumbing | `tests/test_ui_system_input.cpp` | Engine → InputManager → EventBus → `UISystem` smoke |
| File-menu accelerator integration | `tests/test_file_menu.cpp` | Editor-side; touches UI hotkeys |

**Adding a test for `engine/ui`:** drop a new `tests/test_<thing>.cpp` next to its peers, link against `vestige_engine` + `gtest_main` in `tests/CMakeLists.txt` (auto-discovered by `gtest_discover_tests`). Use widgets / canvases / queues directly without an `Engine` instance — every primitive in this subsystem **except `SpriteBatchRenderer` and `UISystem::initialize` itself** is unit-testable headlessly. The pure-function design (`applyGameScreenIntent`, `wrapSubtitleText`, `computeSubtitleLayout`, `notificationAlphaAt`, `projectWorldToScreen`, `ui_contrast::contrastRatio`) is intentional — the same functions feed CI and the runtime path.

**Coverage gap:** the actual GL pipeline (`SpriteBatchRenderer` shader compile + draw + frame composition) is not unit-tested headlessly because it requires a GL context. Coverage is exercised through the visual-test runner (`engine/testing/visual_test_runner.h`) and by every `tests/test_*` that brings up an `Engine`. The TTS / screen-reader bridge has no tests yet because the bridge does not exist (§15 Q1).

## 12. Accessibility

This is high-stakes for `engine/ui` — UI is the **producer side** of every player-visible accessibility surface. The user is partially sighted (per project memory); the rules below are non-negotiable.

**Producer surfaces — `engine/ui` originates each:**

- **High-contrast palette.** `UITheme::withHighContrast()` swaps the palette to pure black / white + saturated brass accent. Triggered by `Settings::accessibility.uiHighContrast` → `UIAccessibilityApplySink::setHighContrast` → `UISystem::setHighContrastMode(bool)` → `rebuildTheme()`. Sizing and motion stay unchanged so high contrast composes with scale and reduced motion.
- **UI scale presets.** `UIScalePreset::{X1_0, X1_25, X1_5, X2_0}` — minimum 1.5× recommended for partially-sighted users (project memory). Routed via `Settings` → `UIAccessibilityApplySink::setUIScale` → `UISystem::setScalePreset(preset)` → `UITheme::withScale`. Touches sizes only.
- **Reduced motion.** `UITheme::withReducedMotion()` zeroes `transitionDuration`. Toast fade-in / fade-out collapse to step-function via `notificationAlphaAt(elapsed, dur, 0.0)`. Routed via `Settings` → `UIAccessibilityApplySink` (or the photosensitive-safety chain) → `UISystem::setReducedMotion(bool)`.
- **Photosensitive-safe defaults.** `UITheme::transitionDuration` defaults to 0.14 s (well below WCAG 2.3.1's three-flashes-per-second threshold). Notification queue is capacity-3 with 0.14 s fades — never strobing. Crosshair / panel strokes are static.
- **Closed captions / subtitles.** `SubtitleQueue` + `CaptionMap` + `computeSubtitleLayout` + `renderSubtitles`. Caption display follows BBC / Game Accessibility Guidelines — 40-char soft wrap (`SUBTITLE_SOFT_WRAP_CHARS`), 2-line hard cap (`SUBTITLE_MAX_LINES`), Unicode ellipsis tail on truncation. Capacity 3, push-newest / drop-oldest. Size presets (Small / Medium / Large / XL) compose with UI scale. Per-category styling (Dialogue / Narrator / SoundCue) is colour-distinguished AND prefix-distinguished (speaker name for dialogue, `[brackets]` for sound cues, italic-or-amber for narrator) — never colour-only.
- **Caption-on-by-default toggle.** `SubtitleQueue::setEnabled` + `Settings::accessibility.subtitlesEnabled` → `SubtitleApplySink::setSubtitlesEnabled`. Phase 10.9 P5 closed the gap where the apply-sink wrote a flag nothing read.
- **Captioned non-dialogue audio.** `CaptionMap` keys on clip path so `[footsteps behind]`, `[door creaks]`, etc. can be authored declaratively per game project.
- **Speaker identification.** `Subtitle::speaker` is rendered as a coloured prefix for `Dialogue` entries — the FCC 2026 Closed-Captioning Settings rule treats speaker ID as a required readability axis.
- **Narrator style choice.** `SubtitleNarratorStyle::{Italic, Colour}` — defaults to `Colour` (warm amber, upright) per the accessibility-first decision in Phase 10.9 P6. The italic option is shipped via vertex-shear (no italic atlas yet) for users who specifically prefer the original §4.2 styling.
- **Keyboard navigation.** Every interactive widget is reachable via Tab / Shift-Tab / arrow keys; `UISystem::handleKey` (Phase 10.9 Slice 3 S4) wraps tab order, traps focus inside the topmost modal, and fires `onClick` on Enter / Space / KP_Enter. Activation is keyboard-equivalent to mouse click — no time-pressure puzzles.
- **Focus ring.** `UITheme::focusRingThickness = 2 px` and `UITheme::focusRingOffset = 3 px` against the burnished-brass accent (or saturated accent in high-contrast mode) — 3:1 contrast against `bgBase` at the at-rest theme (verified via `ui_contrast::contrastRatio` per Phase 10.9 S9 palette-correctness pass). Never colour-only — the ring is a geometric outline, distinct from interior fill.
- **Per-element accessibility metadata.** `UIAccessibleInfo { role, label, description, hint, value }` carried by every `UIElement`; `UICanvas::collectAccessible` produces a flat snapshot for downstream TTS bridges. Role is set in widget constructors (`UIPanel::Panel`, `UIButton::Button`, `UISlider::Slider`, …); callers set context-specific labels at wire time.
- **Screen-reader-friendly state machine.** `gameScreenLabel(screen)` / `gameScreenIntentLabel(intent)` produce stable strings — never nullptr — so a future TTS bridge can announce screen transitions verbatim.
- **WCAG 2.2 contrast verification.** `ui_contrast::contrastRatio` exposed as a free function so palette-correctness tests run without a GL stack. Phase 10.9 S9 bumped `panelStroke` α 0.22 → 0.48 (clears WCAG 1.4.11 ≥ 3:1 against `bgBase`) and `textDisabled` `#5C5447` → `#8E8570` (clears WCAG 1.4.3 ≥ 4.5:1).
- **Editor-side log colouring is text-backed.** Even the editor's log panel, which consumes `Logger`, must back the LogLevel colour with a `TRACE` / `INFO` / `WARN` text label per the partially-sighted-user constraint — that text label is sourced from the engine, not the UI subsystem, but `engine/ui` widgets that surface `Logger` levels follow the same rule.

**Producer-side rules — every `engine/ui` change must respect:**

- **Never colour-only encoding.** State (selected, disabled, error, severity) is always backed by text, prefix, geometry, or position. The notification severity strip is colour AND positioned as a left-edge accent strip; the dropdown selected option is colour AND prefix.
- **Contrast targets.** Body text must clear WCAG 1.4.3 (≥ 4.5:1) against its background; UI components and graphical objects must clear 1.4.11 (≥ 3:1). Verify via `ui_contrast::contrastRatio` in any palette-touching test.
- **Keyboard parity.** Every interaction available with the mouse must be available via keyboard. A widget that responds only to `onHover` is broken.
- **Gamepad parity.** Same as keyboard — see `engine/core`'s `InputManager` gamepad path; UI consumes the same Tab / arrow / activate semantics from gamepad bindings (handled by `UISystem` from `EventBus` — UI subsystem itself is input-source-agnostic).
- **Reduced-motion respect.** Any new transition / animation must read `theme.transitionDuration` and collapse cleanly when zero (no fixed-duration animations, no hard-coded easing). The toast envelope is the reference pattern.
- **Photosensitive caps.** No element is allowed to flash > 3 times / second; the `Photosensitive` clamp helpers (`engine/accessibility/photosensitive_safety.h`) clamp upstream effects. UI itself produces no flashes by design.
- **Caption defaults.** Subtitles default ON (`SubtitleQueue::isEnabled() == true`) — accessible by default, opt-out rather than opt-in.

## 13. Dependencies

| Dependency | Type | Why |
|------------|------|-----|
| `engine/core/i_system.h` | engine subsystem | `UISystem : public ISystem` (the host lives in `engine/systems/`, not `engine/ui/` — but every `ui_*.h` widget tree is consumed by it). |
| `engine/core/event_bus.h` (consumed via `UISystem`) | engine subsystem | `KeyPressedEvent` / mouse-move events drive focus walk + hit test. |
| `engine/renderer/text_renderer.h` | engine subsystem | Glyph rasterisation + width measurement (used by every text-bearing widget + subtitle layout). |
| `engine/renderer/shader.h` | engine subsystem | `SpriteBatchRenderer::m_shader` — sprite-quad GL program. |
| `engine/renderer/camera.h` | engine subsystem | `UIWorldLabel` / `UIInteractionPrompt` consume `Camera::getViewProjection` for `projectWorldToScreen`. |
| `engine/accessibility/photosensitive_safety.h` | engine subsystem | UI consults the live-updated photosensitive limits when emitting any flashing element (defensive — UI ships none today, but the hook is wired for future overlays). |
| `<glm/glm.hpp>` | external | `vec2` / `vec3` / `vec4` / `mat4` everywhere. |
| `<glad/gl.h>` | external | GL 4.5 entry points (sprite batch only). |
| `<nlohmann/json.hpp>` | external | `CaptionMap::loadFromFile` JSON parse. |
| `<functional>`, `<memory>`, `<string>`, `<unordered_map>`, `<vector>`, `<cstddef>` | std | Slot lists, `unique_ptr`, strings, screen-builder map, queues. |

**Direction:** `engine/ui` is consumed by `engine/systems/ui_system.h` (the host) and by game-project code (which calls `applyIntent`, registers screen builders, populates the canvas). `engine/ui` itself depends on `engine/core` (events, ISystem), `engine/renderer` (text + shader + camera), and `engine/accessibility` (photosensitive limits). It must **not** depend on `engine/scene/`, `engine/physics/`, `engine/audio/` directly — caption flow into `engine/ui` is via the engine-owned `SubtitleQueue` (push from audio side) rather than a direct include path.

## 14. References

External / authoritative sources (≥ 4 within the last year):

- Pope Tech (2026-03-04). *A guide to accessible focus indicators.* WCAG 2.2 SC 1.4.11 contrast targets, two-tone focus indicator patterns. <https://blog.pope.tech/2026/03/04/a-guide-to-accessible-focus-indicators/>
- UXPin (2026). *How to Build Accessible Modals with Focus Traps.* Focus-trap pattern, modal-stack accessibility — informs `UISystem::pushModalScreen` behaviour. <https://www.uxpin.com/studio/blog/how-to-build-accessible-modals-with-focus-traps/>
- W3C WAI-ARIA Authoring Practices Guide. *Developing a Keyboard Interface.* Tab / Shift-Tab / arrow / Enter / Space conventions used by `UISystem::handleKey`. <https://www.w3.org/WAI/ARIA/apg/practices/keyboard-interface/>
- Muz.li (2026). *How to Make Your UI Accessible: A Practical Checklist for 2026.* Current-year checklist for never-colour-only encoding, contrast, motion. <https://muz.li/blog/how-to-make-your-ui-accessible-a-practical-checklist-for-2026/>
- Pope Tech (2025-12-08). *Design accessible animation and movement with code examples.* Reduced-motion implementation patterns informing `UITheme::withReducedMotion` + `notificationAlphaAt` step-function path. <https://blog.pope.tech/2025/12/08/design-accessible-animation-and-movement/>
- TestPros (2026). *How To Meet FCC Closed Captioning Requirements by 2026.* FCC August 17 2026 deadline for caption settings accessibility — informs `Settings` apply-sink design + `SubtitleQueue::setEnabled` semantics. <https://testpros.com/compliance/fcc-closed-captioning-requirements/>
- Game Accessibility Guidelines. *BBC Subtitling Guidelines.* Reading-speed (130–180 wpm), 32–40 chars per line, 2-line cap — direct sources for `SUBTITLE_SOFT_WRAP_CHARS` and `SUBTITLE_MAX_LINES`. <https://gameaccessibilityguidelines.com/resource/bbc-subtitling-guidelines/>
- MDN Web Docs. *Web accessibility for seizures and physical reactions.* WCAG 2.3.1 three-flash threshold; informs `Photosensitive` defaults. <https://developer.mozilla.org/en-US/docs/Web/Accessibility/Guides/Seizure_disorders>
- Open UI. *focusgroup explainer.* Roving-tabindex pattern referenced by the focus-walk design. <https://open-ui.org/components/focusgroup.explainer/>
- ocornut/imgui issue #5891. *How to use Dear ImGui as a GUI system in-game and then using it as editor too in the same app?* Confirms the engine's separation choice — Dear ImGui for editor only, native sprite-batch UI for shipped game. <https://github.com/ocornut/imgui/issues/5891>
- Romero-Fresco (2019). *Accessible Filmmaking* (cited by BBC subtitling guidelines). Source for the 2-line on-screen ceiling.
- WCAG 2.2 Understanding §1.4.3, §1.4.11. Contrast-ratio formula implemented by `ui_contrast::relativeLuminance` / `contrastRatio`.
- Vestige internal: `vestige-ui-hud-inworld` design hand-off (Claude Design, 2026-04-20). Source for Vellum / Plumbline registers, button / slider / checkbox / dropdown / keybind sizing.

Internal cross-references:

- `CODING_STANDARDS.md` §11 (errors), §12 (memory), §13 (threading), §17 (CPU/GPU), §18 (public API), §29 (debug groups).
- `docs/phases/phase_10_7_design.md` — accessibility integration; §4.2 caption layout; B1 / B2 / B3 slice plan.
- `docs/engine/core/spec.md` §12 — accessibility routing (every UI surface flows through `Settings` and the apply-sinks documented there).
- `CLAUDE.md` rule 1 (research → design → review → code), rule 7 (CPU/GPU placement).

## 15. Open questions

| # | Question | Owner | Target |
|---|----------|-------|--------|
| 1 | Build the Text-To-Speech (TTS) / screen-reader bridge that consumes `UICanvas::collectAccessible`. Metadata is ready; consumer is unbuilt. | `milnet01` | post-MIT release (Phase 12) |
| 2 | True italic font atlas vs vertex-shear oblique for `SubtitleNarratorStyle::Italic`. Today the renderer applies an ~11° horizontal shear at vertex-emit time; a licensed italic atlas would be cleaner at small sizes. | `milnet01` | Phase 11 (when font asset budget is decided) |
| 3 | Multi-modal compositing — confirm dialog over Settings overlay. Modal stack is `std::vector<GameScreen>` so the data is ready, but the visual / focus-trap interaction is unprototyped. | `milnet01` | Phase 11 entry |
| 4 | Add `glPushDebugGroup("UI-…")` markers around `SpriteBatchRenderer::flush` and the subtitle pass per CODING_STANDARDS §29. Trivial; noting as debt. | `milnet01` | Phase 10.9 close-out |
| 5 | `m_wantsCaptureInput` field on `UISystem` is kept "for ABI continuity" but is no longer the canonical capture source (the union of `m_modalCapture` and `m_cursorOverInteractive` is). Either delete it or document the ABI guarantee. | `milnet01` | Phase 11 entry (debt sweep) |
| 6 | Performance budgets in §8 are placeholders. Need a one-shot Tracy / RenderDoc capture of HUD-only and full-menu paths to fill in measured numbers. | `milnet01` | Phase 11 audit (concrete: end of Phase 10.9) |
| 7 | Caption-direction-degree visualisation (the `Subtitle::directionDegrees` field is authored + plumbed but never rendered). Spatial-audio caption indicator (arrow glyph) deferred. | `milnet01` | triage (no scheduled phase) |
| 8 | Per-category subtitle styling colour values (`Dialogue` yellow speaker, `Narrator` amber/italic, `SoundCue` cyan-grey) are hard-coded in `styleFor`. Consider making them theme-driven so high-contrast can override them as a unit. | `milnet01` | Phase 11 entry |

## 16. Spec change log

| Date | Spec version | Author | Change |
|------|--------------|--------|--------|
| 2026-04-28 | 1.0 | `milnet01` | Initial spec — `engine/ui` foundation since Phase 5; expanded through Phase 10.7 (subtitles + caption map + accessibility apply-sinks) and Phase 10.9 (caption wrap + narrator style + WCAG palette pass + keyboard focus + modal stack). Pending cold-eyes review. |
