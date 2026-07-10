# Phase 10 — AX12: Editor Audio Spectrum / Waveform Viewer (design)

**Roadmap:** AX12 (Audio System section) — *"Real-time spectrum / waveform viewer
in editor."* **This doc supersedes the ROADMAP AX12 line's implementation sketch**
("pocketfft", "waveform of the master bus"): it reuses the in-repo FFT (no pocketfft)
and analyses a **per-bus, producer-push signal**, not a summed master bus (see §1.1, §2).
**Status:** design — awaiting cold-eyes convergence before implementation.
**Author:** in-session 2026-07-10.
**Depends on:** shipped audio bundles (AX1–AX4, reverb, occlusion, procedural),
`AudioEngine` / `AudioMixer`, `AudioMusicPlayer`, `AudioAnalyzer` FFT, editor
`AudioPanel` Debug tab, vendored ImPlot.

## Section index

- §1 Goal & scope (+ non-goals)
- §2 Background — why a per-bus producer-push tap
- §3 Architecture (shared FFT, monitor, producer hooks, solo, UI)
- §4 CPU / GPU placement
- §5 Performance (60 FPS gate)
- §6 Accessibility
- §7 Testing plan
- §8 Invariants
- §9 Implementation order
- §10 Sources

---

## 1. Goal & scope

Give the editor's **Window → Audio → Debug** tab a live "mini audio-analyzer" so a
scene author can answer *"why does my mix sound muddy / too bright / too quiet?"*
without a separate DAW. Three user-visible features (full AX12 scope, confirmed
2026-07-10):

1. **Frequency bars** — a live magnitude spectrum (bass → treble) of the selected bus.
2. **Scrolling waveform** — the selected bus's time-domain signal over the last ~1–2 s.
3. **Per-bus solo / select** — pick a channel (Music / Voice / Sfx / Ambient / Ui) to
   view *and* audition in isolation (mutes the others on the live output).

### 1.1 The signal: per-bus, producer-push

**Chosen approach (user decision 2026-07-10): analyse only CPU-generated audio, one bus
at a time, tapped where it is produced.** OpenAL Soft hides both the final speaker mix
*and* each voice's playing samples (§2). Rather than reconstruct voices from OpenAL, the
audio **producers that already hold their samples on the CPU** — the streaming music
player and the procedural synth — **push a copy** of what they generate into an
`AudioMixMonitor`, tagged with their bus and sample rate. The monitor keeps a short
rolling buffer **per bus**; the viewer transforms and draws the **selected** bus (default
`Music`).

Why per-bus and not one summed "master mix": the producers are asynchronous and
heterogeneous — the music player decodes **ahead** of playback and a file's rate may
differ from the synth's — so summing them into one time-aligned master signal is
incoherent (it would misalign timelines and conflate sample rates). Analysing each bus on
its own timeline and rate is coherent, makes solo trivial and eviction-safe (§3.4), and
still answers the muddiness question (you inspect each contributor). A summed-master view
is explicitly deferred (§1.2).

This per-bus signal is the **analysis signal** throughout (one term, used consistently) —
it is *not* the speaker output. It reflects the frequency balance and levels of a bus's
CPU-generated content but excludes HRTF panning, distance attenuation, and reverb tails
OpenAL applies last. Because the music producer decodes ahead of playback (its keep-ahead
window is ~0.30–0.60 s, `audio_music_stream.h`), the **Music** bus's analysis *leads* the
speakers by that window — acceptable for spectral-balance debugging (frequency content,
not phase), and noted in the tab.

### 1.2 Non-goals (explicit)

- **No summed "master mix" spectrum in v1.** Only one selected bus is analysed/displayed
  at a time (switchable via the solo/select row). A cross-bus summed view is deferred (it
  needs rate/timeline reconciliation the per-bus model deliberately avoids).
- **File-decoded one-shot SFX are not graphed.** `AudioEngine::loadBuffer` decodes a clip,
  uploads it to OpenAL via `alBufferData`, and **discards its CPU copy** (`m_bufferCache`
  maps `path → ALuint` only; a live voice's `SourceMix` stores
  `{bus, sourceVolume, priority, startTime}` — no path/PCM handle,
  `engine/audio/audio_engine.h`). Analysing them would need retaining every decoded clip
  in RAM and threading a PCM handle through the source system — out of scope. The Sfx bus
  therefore shows only *procedural* audio (footsteps/impacts); the tab states file
  one-shots are not shown. **Per-bus solo still mutes them on the live output** (§3.4).
- **Not the true post-spatialization speaker output** (would need `ALC_SOFT_loopback`,
  which replaces the hardware-device path and breaks device hot-swap — §2).
- **No new third-party dependency.** Reuses the in-repo FFT and already-vendored ImPlot.
- **No mixer rewrite, no new audio thread, no lock-free ring buffer** (producers and the
  panel both run on the main/update thread — §3.2).

---

## 2. Background — why a per-bus producer-push tap

`AudioEngine` opens a **normal hardware playback device**
(`alcOpenDevice(nullptr)` at `engine/audio/audio_engine.cpp:64`). OpenAL Soft mixes on its
own internal render thread and streams straight to the driver; there is **no CPU-visible
post-mix buffer in engine code** (verified: zero references to `ALC_SOFT_loopback`,
`alcRenderSamplesSOFT`, `alcCaptureSamples` under `engine/`). OpenAL also exposes no
read-back of a buffer's samples, so a *playing* voice's PCM is not recoverable engine-side.

Two rejected alternatives:

- **`ALC_SOFT_loopback`** — true post-mix, but requires driving the render manually,
  replacing the hardware-device path; incompatible with the existing device hot-swap
  (`ALC_SOFT_reopen_device` / `ALC_SOFT_system_events`). Disproportionate for a debug tool.
- **Retain all decoded PCM + reconstruct per voice via `AL_SAMPLE_OFFSET`** — faithful to
  every source but doubles audio memory and needs a source→PCM handle threaded through
  `SourceMix`. Rejected as over-built for a debug viewer.

The producer-push tap reads samples **only where they already exist on the CPU**, at zero
extra memory when the tab is closed.

---

## 3. Architecture

Four small, independently testable units, plus one localized mixer addition and one
shared-helper extraction. Pure maths (unit-testable without any audio device) is kept
separate from OpenAL plumbing and from UI.

```
 CPU producers ──submit(bus, samples, gain, rate)──▶ AudioMixMonitor ──snapshot()──▶ AudioPanel::drawDebugTab
 (AudioMusicPlayer decode,                            │ per-bus frame-sum → per-bus       │ (ImPlot: selected bus)
  procedural synth: m_synthScratch)                   │ rolling ring; no-op inactive      │
                                                       └── accumulateBusFrame (pure) ── unit tested   ├── computeMagnitudeSpectrum() ◀ shared FFT helper
 AudioMixer.soloBus ──busSoloMultiplier()──▶ post-multiply at AL_GAIN OUTPUT sites only ──▶ audible isolation
                                              (resolveSourceGain / eviction / occlusion left untouched)
```

### 3.1 Shared spectrum helper (reuse, not rewrite)

`AudioAnalyzer::computeFFT` is today a **private static** radix-2 Cooley-Tukey FFT inside
`engine/experimental/animation/audio_analyzer.{h,cpp}` (`FFT_SIZE = 512`, Hann-windowed
for the lip-sync spectral centroid). Per project reuse rule 3(b), **extract only the raw
FFT** into a shared pure module:

```
engine/audio/audio_spectrum.h / .cpp   (new, pure — no OpenAL, no ImGui)

  // In-place radix-2 Cooley-Tukey FFT (moved verbatim from AudioAnalyzer::computeFFT;
  // `n` must be a power of two).
  void computeFFT(std::vector<float>& real, std::vector<float>& imag);

  // Hann-window `in` (n samples, n a power of two), FFT, write n/2 linear magnitude
  // bins to `outMag`. NEW code (the analyzer fuses magnitude into its centroid loop and
  // never exposed a standalone helper). Pure/deterministic. Hann divisor is (n-1) to
  // match the analyzer's existing window exactly. binHz(i) = i * sampleRate / n.
  void computeMagnitudeSpectrum(const float* in, std::size_t n,
                                std::vector<float>& outMag);
```

`AudioAnalyzer` is refactored to call the shared `computeFFT` (its magnitude/centroid code
is untouched); behaviour is unchanged and pinned by the existing
`tests/test_audio_analyzer.cpp`. Only `computeFFT` is shared; `computeMagnitudeSpectrum` is
new.

### 3.2 `AudioMixMonitor` (the plumbing) + pure per-bus accumulator

`engine/audio/audio_mix_monitor.{h,cpp}` — owned by `AudioEngine`.

- **Active only when the Debug tab is open.** `setActive(bool)`; while inactive,
  `submit(...)` is an immediate no-op and producers skip the copy entirely (§5). The panel
  calls `AudioEngine::setMixMonitorActive(m_debugTabVisible)` each frame.
- **Sample domain:** producers submit **normalized-float mono** samples. Both real
  producers hold **int16** PCM (music: `stb_vorbis_get_samples_short_interleaved`,
  `AL_FORMAT_*16`; procedural: `m_synthScratch` is `std::vector<std::int16_t>`,
  `audio_engine.h:916`), so each producer converts int16→float by `s / 32768.0f` and
  down-mixes stereo→mono (channel average) **before** `submit`. All spectrum/waveform math
  and the clip test operate in this normalized-float domain.
- **Producer contract (per-bus, per-frame summation):**
  ```
  void beginFrame();                                             // clears per-bus frame accumulators
  void submit(AudioBus bus, const float* mono, std::size_t n,
              float gain, int sampleRate);                       // adds gain*mono into bus's frame block
  void endFrame();                                              // pushes each bus's summed frame block onto its ring
  ```
  `AudioSystem::update` brackets the audio update with `beginFrame()` / `endFrame()` (only
  when active), so all producer submits for a frame land between them. Within a frame,
  multiple submits to the **same bus** (e.g. several music layers, or overlapping
  procedural hits) are **summed** aligned at index 0 (short blocks zero-pad) into that
  bus's frame block — correct because a single bus's producers share one playback timeline.
  Blocks on **different buses are never summed together.** The pure summation —
  "given a frame's `{samples, gain}` submits for one bus, produce that bus's frame block of
  length = the frame's max submitted length" — is factored as a free function
  `accumulateBusFrame(...)` and unit-tested without any audio object.
- **Per-bus rolling rings.** `endFrame()` appends each bus's frame block to that bus's
  rolling ring (capacity ≥ `WINDOW` + waveform history). The ring holds the bus's recent
  normalized-float signal; the spectrum FFTs the **last `WINDOW = 2048` samples of the
  selected bus's ring** (a rolling STFT window — ≈43 ms at 48 kHz), and the waveform reads
  a decimated tail of the same ring (~1–2 s). An idle bus's ring decays to zeros as empty
  frames push zero-length blocks (nothing appended), so its window reads silence.
- **Heterogeneous rates handled by construction:** each bus stores the sample rate of its
  producer's submits; `binHz` for the displayed spectrum uses **that bus's** rate. No
  cross-rate summation occurs (buses are never mixed), so rates need not match across buses.
  (If two producers ever share a bus at different rates, the monitor keeps the first rate
  seen that frame and logs a one-time warning — an accepted debug-tool limitation.)
- `MixSnapshot snapshot()` returns `{ std::array<std::vector<float>, AudioBusCount> ring;
  std::array<int, AudioBusCount> rateHz; }` for the current frame. `ring[Master]` /
  `rateHz[Master]` are **unused** (Master is the aggregate concept, never a producer bus);
  indexing stays by `AudioBus` for simplicity. Same-thread (main/update) production and
  consumption ⇒ **no locking.**

### 3.3 Producer hooks

Both CPU producers submit at their existing production points, guarded by the active flag:

- **Streaming music** — `AudioMusicPlayer` decodes int16 chunks on the CPU before
  `alSourceQueueBuffers` and computes each layer's effective gain
  (`resolveSourceGain(*mixer, AudioBus::Music, layer.gain.currentGain, duckGain)`,
  `audio_music_player.cpp:347`; uploaded at `:350`). Each decoded chunk is normalized to
  float, down-mixed to mono, and submitted on the `Music` bus scaled by that layer's gain,
  at the file's decode rate. *(`audio_music_stream.h` is a decode-frame counter holding no
  PCM — the hook lives in `AudioMusicPlayer`, the PCM/AL-queue owner.)*
- **Procedural audio** (AX4) — `AudioEngine::playSynth` synthesises int16 into
  `m_synthScratch` via `m_synthBank.synthesize(..., m_synthScratch)` (`audio_engine.cpp:922`)
  and resolves gain at `~:972`. The synth block is normalized, down-mixed, and submitted on
  its assigned bus (typically `Sfx`) at the synth's rate, tapped right after `synthesize`.

Both hooks run on the main/update thread. When `setActive(false)`, neither producer copies
anything.

### 3.4 Per-bus solo (localized mixer addition, output-only)

Single-solo model (radio-style — one bus at a time; clicking the active solo clears it),
which doubles as the viewer's **bus selector** (the soloed bus is the displayed bus; with
no solo, the panel shows a default of `Music`):

```
// In AudioMixer (plain data — keeps the "no OpenAL" invariant):
int soloBus = -1;  // -1 = no solo; otherwise a non-Master AudioBus index

// Pure: 1.0 if no bus is soloed OR `bus` is the soloed bus; else 0.0. Master is never muted.
float busSoloMultiplier(const AudioMixer& mixer, AudioBus bus);
```

**Solo is applied ONLY at the final `AL_GAIN` output-upload sites, as a post-multiply on
the already-resolved gain — never inside `resolveSourceGain` / `effectiveBusGain`.** Those
shared functions are also called for **decisions that must stay solo-agnostic**:
voice-eviction scoring (`AudioEngine::acquireSource`, `audio_engine.cpp:670`) and occlusion
ray-gating (`AudioOcclusionSystem::update`, `audio_occlusion_system.cpp:411`). Folding solo
into them would make soloing a bus evict/kill non-soloed voices and stall their occlusion —
so it must not be done.

The `alSourcef(src, AL_GAIN, g)` output sites to post-multiply are (grep
`alSourcef(*, AL_GAIN, *)` to confirm the full set at implementation): the `play*` initial
uploads (`audio_engine.cpp:757/802/848/897`), `playSynth` initial (`~:972`→upload),
`applySourceState` (`:1318`, the spatial per-source `state.gain` from
`composeAudioSourceAlState`), `updateGains` (`:1507`), and `AudioMusicPlayer::update`
(`:350`). At each, upload `g * busSoloMultiplier(mixer, bus)`.

Consequences for the analysis path: submitted (graphed) samples are scaled by the **content
gain only** (no solo), so each bus's ring always holds that bus's real content and the user
can switch the displayed bus freely; solo affects **only what you hear** and **which bus is
shown**. Solo state is **transient authoring state — not persisted** to settings.json.

The full resolved *output* gain per source is therefore
`master × bus × sourceVolume × effectiveDuck(bus) × busSoloMultiplier(bus)`
— the first four via the existing `resolveSourceGain` (`audio_mixer.cpp:82,96`) +
`effectiveDuck` (`audio_engine.cpp:1506`), and `busSoloMultiplier` layered on at upload.

### 3.5 UI — AudioPanel Debug tab

`drawDebugTab` (already exists, `engine/editor/panels/audio_panel.cpp:508`) gains a
collapsible "Spectrum / Waveform" region above the existing text status.

**ImPlot API note (version currency — global rule 5).** The pinned ImPlot commit
(`1351ab2c`, header self-reports `IMPLOT_VERSION "1.1 WIP"`, chosen for ImGui 1.92.x) is on
the **post-1.0 `ImPlotSpec` API**: the old trailing `flags / offset / stride` positional
args are removed from the `PlotX` signatures and now live on a single `ImPlotSpec` passed
last; v0.16-era plot calls will not compile. The log axis is
`SetupAxisScale(ImAxis_X1, ImPlotScale_Log10)` — the old `ImPlotAxisFlags_LogScale` no
longer exists. Use ImPlot-native condition enums (`ImPlotCond_Always`, not `ImGuiCond_*`).

- **Frequency bars (spectrum) — selected bus:** UI-layer only — `computeMagnitudeSpectrum`
  returns **linear** magnitudes (deterministic/testable, INV-1); the panel converts to dB
  and applies ballistics:
  - **Log-frequency X axis** 20 Hz–20 kHz via `SetupAxisScale(ImAxis_X1, ImPlotScale_Log10)`;
    bin→Hz uses the selected bus's `rateHz`.
  - **dB Y axis:** `dB = 20·log10(max(mag, 1e-9))` (`1e-9` floor guards `log10(0)`), clamped
    to a **−80 dB display floor**, `SetupAxisLimits(ImAxis_Y1, -80, 0)`.
  - Rendered with **`ImPlot::PlotShaded`** (fill from each bin's dB to the −80 dB floor), not
    `PlotBars`: on a log axis a fixed `bar_size` is in data units so bars widen unevenly
    toward the low end; a shaded envelope reads clean. Optional `PlotLine` overlay for a
    crisp top edge.
  - **Ballistics (anti-strobe):** per-bin fast-attack / slow-release smoothing of the
    displayed dB height — `if (v > s) s = v; else s = release·s + (1-release)·v;`
    (`release ≈ 0.9` at 60 FPS) — held as **UI-side panel state**, never fed back into the
    analysed signal (INV-8).
  - **Bass resolution:** `WINDOW = 2048` gives ≈ 23 Hz bins at 48 kHz (≈ 21.5 Hz at
    44.1 kHz), so the low-mid region where muddiness lives has usable resolution; this
    trades ~43 ms of time-window latency, acceptable for a meter.
  - A numeric **peak-frequency + peak-dB readout** sits beside the plot (does not rely on
    colour alone — accessibility, §6).
- **Scrolling waveform — selected bus:** `ImPlot::PlotLine` over the selected bus's ring
  tail, fed through a hand-copied replica of ImPlot's demo `ScrollingBuffer` (an
  `ImVector<ImVec2>` ring; it lives in `implot_demo.cpp`, which is **not** compiled into
  `implot_lib`, so it is replicated in the panel, not linked). The X window is pinned with
  `SetupAxisLimits(ImAxis_X1, t - history, t, ImPlotCond_Always)` for the left-scroll. A
  clip indicator lights when any normalized sample `|s| > 1.0` (possible because AX9
  loudness makeup can push gain > 1.0; the ring is unclamped, INV-3). Compact plot via
  `ImVec2(-1, <px>)` + `ImPlotFlags_NoLegend` / `ImPlotFlags_CanvasOnly`.
- **No double-windowing:** `computeMagnitudeSpectrum` Hann-windows once before the FFT; the
  panel must not window again (a Hann² widens the main lobe and corrupts the dB read).
- **Bus select / solo row:** radio-style toggles for Music / Voice / Sfx / Ambient / Ui
  (clicking the active one clears solo → falls back to viewing `Music`); the active toggle
  is the displayed bus, wired to `AudioMixer::soloBus` via an `AudioEngine` setter. A
  one-line caption notes the Sfx bus shows procedural audio only (file one-shots excluded,
  §1.2) and that the Music view leads the speakers by the decode-ahead window (§1.1).
- Requires **linking `implot_lib` into `vestige_engine`** (the monolithic engine+editor
  STATIC lib, `engine/CMakeLists.txt:372`; there is no separate editor target). One line —
  `imgui_lib` is already linked (`:442`) so the incremental is `implot_lib` alone (link it
  `PUBLIC`). This makes `implot_lib` a transitive dep of `app/vestige` and every test binary
  linking `vestige_engine` (static linkage keeps object pull-in lazy); the existing explicit
  `implot_lib` link in `tools/CMakeLists.txt` (`formula_workbench`) becomes redundant but
  harmless.

---

## 4. CPU / GPU placement (project rule 7)

| Work | Placement | Reason |
|------|-----------|--------|
| Per-bus accumulation (producer submit / frame-sum / ring append) | **CPU** | Samples already exist on the CPU at the producer; summing a few short blocks is trivial and branchy. A GPU round-trip would add latency for no gain. Matches the "branching / sparse / I/O → CPU" heuristic. |
| FFT (one 2048-pt FFT of the selected bus, per frame) | **CPU** | Microsecond-scale at this size; must interoperate with ImGui/ImPlot which are CPU-side. |
| Solo gain multiply | **CPU** | Part of the existing per-frame gain-upload path. |
| Bar / waveform rasterization | **GPU (via ImGui backend)** | ImPlot emits vertex data; the ImGui GL backend draws it — standard, no custom GPU code. |

No dual CPU/GPU implementation is needed (no per-pixel / per-vertex runtime hot path), so
no parity test is required.

---

## 5. Performance (60 FPS hard gate)

- **Tab closed (default):** monitor inactive → producers skip the sample copy and `submit`
  is a no-op → **zero** added cost. The dominant case; keeps the gate safe for normal use.
  Observable via INV-6 (a submit-call counter asserts 0 while inactive).
- **Tab open:** per frame — the active producers' int16→float copies (a few short blocks) +
  per-bus frame-sum + ring appends + **one** 2048-pt FFT of the *selected* bus + dB/ballistics
  over ≤ 1024 bins + ImPlot draw. Only the selected bus is FFT'd (non-selected buses keep
  cheap rings for instant switching but are not transformed). Order tens of microseconds on
  the RX 6600 target, negligible against the 16.6 ms budget.
- Verify with the frame-time HUD: open the Debug tab in a busy scene (many music layers +
  procedural emitters) and confirm frame time stays < 16.6 ms.

---

## 6. Accessibility

- **Reduce-motion:** the waveform's left-scroll is the strongest animation. Add a new
  per-feature flag `reduceMotionAudioViewer` to `PostProcessAccessibilitySettings`
  (mirroring the existing `reduceMotionFog` / `reduceMotionGi` — there is **no** generic
  `reduceMotion` field), **and** extend that struct's `operator==` and `safeDefaults()`
  accordingly (`post_process_accessibility.h`), or settings equality / dirty-detection
  silently breaks. When set, the waveform freezes to a periodic static snapshot. The
  spectrum bars remain animated (an instantaneous readout with ballistics is still motion,
  but far less than a scrolling trace — kept live by design). This is a small persisted
  accessibility pref (one bool + one checkbox + the `operator==`/`safeDefaults` update) —
  distinct from the transient, non-persisted solo state (INV-7).
- **Not colour-only:** peak frequency and peak level are shown as **text** beside the bars;
  the spectrum uses a perceptually-ordered colormap, not red/green.
- The panel is opt-in (drawn only when the tab is open) and keyboard-reachable through the
  existing ImGui tab navigation.

---

## 7. Testing plan

Pure-function unit tests (no audio device — the reason for the maths/plumbing split):

- `test_audio_spectrum` — `computeMagnitudeSpectrum` of a synthesized sine at frequency *f*
  (rate *sr*, size *n* power-of-two) peaks in bin `round(f·n/sr)`; DC input peaks in bin 0;
  silence → all-zero magnitudes; asserts identical output on repeat (purity, INV-1); asserts
  no double-window (a pre-windowed input is not windowed again — INV-8's Hann-once check).
- `test_audio_mix_monitor` — `accumulateBusFrame` with two fake same-bus submits and known
  gains yields `frame[i] == g0·s0[i] + g1·s1[i]`; a submit block shorter than the frame max
  zero-pads (never OOB); an empty frame produces a zero-length append so the ring decays to
  silence; over-unity sums are **not** clamped (INV-3); the inactive-monitor submit-call
  counter is `== 0` after a simulated producer frame (INV-6).
- `test_audio_mixer` — `busSoloMultiplier` returns 1.0 for every bus when `soloBus == -1`;
  when `Sfx` is soloed returns 1.0 for `Sfx` (and Master) and 0.0 for the rest; **and
  `resolveSourceGain` / `effectiveBusGain` outputs are invariant to `soloBus`** (proves the
  eviction/occlusion exemption, INV-4).
- INV-5 regression: existing `test_audio_analyzer` passes unchanged after the FFT extraction
  (proves the refactor is behaviour-preserving).
- INV-7: a settings.json round-trip asserts solo is **not** persisted, and that the new
  `reduceMotionAudioViewer` flag **is** persisted (round-trips through `operator==`).

Manual/visual: open Debug tab, play a known 1 kHz music layer → Music-bus bar peak at 1 kHz;
solo Music → only music audible and it is the displayed bus; solo Sfx → file one-shots muted
on output, Sfx view shows procedural only; clip indicator fires on an intentionally
over-unity (loudness-boosted) sum; soloing a bus does not change which voices get evicted
under pool pressure.

---

## 8. Invariants

| ID | Invariant | Test surface |
|----|-----------|--------------|
| INV-1 | `computeMagnitudeSpectrum` is pure: identical input → identical `outMag`; peak bin of a sine at *f* is `round(f·n/sr)`; requires `n` a power of two. | `test_audio_spectrum` |
| INV-2 | `accumulateBusFrame` output = Σ(gainᵢ · samplesᵢ) for one bus's frame submits; length == the frame's max submitted length (short blocks zero-pad, never OOB). Different buses are never summed together. | `test_audio_mix_monitor` |
| INV-3 | The accumulator/ring does **not** clamp — over-unity (loudness-boosted) sums survive so the UI can flag clipping in the normalized-float domain. | `test_audio_mix_monitor` |
| INV-4 | `busSoloMultiplier` = 0 for a bus iff `soloBus != -1` and `bus != soloBus`; Master never solo-muted. Solo is applied only at AL_GAIN upload; **`resolveSourceGain`/`effectiveBusGain` outputs are independent of `soloBus`** (eviction + occlusion stay solo-agnostic). | `test_audio_mixer` |
| INV-5 | FFT extraction is behaviour-preserving: only `computeFFT` is shared; `AudioAnalyzer` spectral centroid unchanged (Hann divisor `n-1` preserved). | `test_audio_analyzer` (existing) |
| INV-6 | Monitor inactive ⇒ `submit` is a no-op and producers copy nothing (submit-call counter == 0). | `test_audio_mix_monitor` (counter) |
| INV-7 | Solo state is not persisted to settings.json (transient). The `reduceMotionAudioViewer` accessibility flag **is** persisted and included in `operator==` / `safeDefaults`. | settings round-trip test |
| INV-8 | dB conversion + bar ballistics + peak-hold are **UI-layer only**: `computeMagnitudeSpectrum` returns raw linear magnitudes, unaffected by display smoothing; input is Hann-windowed exactly once; `log10` input floored at 1e-9 (no `-inf`). | `test_audio_spectrum` + code review |

---

## 9. Implementation order

1. Extract shared FFT → `audio_spectrum.{h,cpp}`; repoint `AudioAnalyzer` to it; confirm
   `test_audio_analyzer` green. Add `computeMagnitudeSpectrum` + `test_audio_spectrum`.
2. `accumulateBusFrame` + `AudioMixMonitor` (`setActive` / `beginFrame` / `submit` /
   `endFrame` / `snapshot`, per-bus rings, per-bus rate) + `test_audio_mix_monitor`.
3. Solo: `AudioMixer::soloBus` + `busSoloMultiplier`; post-multiply at every AL_GAIN upload
   site (enumerated §3.4); engine setter; `test_audio_mixer` incl. the resolveSourceGain
   invariance check.
4. Producer hooks: `AudioMusicPlayer` chunk submit (Music); `playSynth` `m_synthScratch`
   submit (assigned bus) — both int16→float, guarded by the active flag. Bracket the audio
   update with `beginFrame`/`endFrame` in `AudioSystem::update`.
5. `reduceMotionAudioViewer` flag (+ `operator==` / `safeDefaults` / settings checkbox +
   round-trip test).
6. Debug-tab UI (ImPlot spectrum + waveform + bus-select/solo radio row + captions + clip
   indicator); link `implot_lib` PUBLIC into `vestige_engine`.
7. 60 FPS gate verification in a busy scene; CHANGELOG; ROADMAP — flip status **and** correct
   the stale "pocketfft / master bus" text on the AX12 bullet to match this design.

---

## 10. Sources

- OpenAL Soft — `ALC_SOFT_loopback` (`alcLoopbackOpenDeviceSOFT`, `alcRenderSamplesSOFT`):
  the only route to a true post-mix buffer, and why it conflicts with hardware-device
  hot-swap. <https://openal-soft.org/> extension docs.
- OpenAL 1.1 spec — no buffer-data read-back, motivating the producer-push tap.
- Hann window / STFT for spectral display — Harris, "On the use of windows for harmonic
  analysis with the discrete Fourier transform," Proc. IEEE 1978 (the in-repo FFT already
  applies Hann).
- ImPlot **post-1.0 `ImPlotSpec` API** (pinned commit `1351ab2c`, header
  `IMPLOT_VERSION "1.1 WIP"`): flags / offset / stride moved onto `ImPlotSpec`;
  `SetupAxisScale(ImAxis_X1, ImPlotScale_Log10)` replaces the removed
  `ImPlotAxisFlags_LogScale`; `PlotShaded` for the log-frequency envelope; the demo
  `ScrollingBuffer` ring (in `implot_demo.cpp`, replicated not linked) for the waveform.
  `implot.h` / `implot_demo.cpp` at that commit; ImPlot Discussion #370 (v1.0), Issue #690
  (Spec migration).
- Real-time analyzer display conventions — log-frequency axis (~20 Hz–20 kHz), dB magnitude
  (`20·log10(mag)`, −60/−80 dB floor), fast-attack/slow-release ballistics + peak-hold,
  single Hann window: Audacity "Plot Spectrum" manual; SIR SpectrumAnalyzer; Oxford Wave
  Research SpectrumView help; techmind audio spectrum-analyser notes.
- In-repo reuse: `engine/experimental/animation/audio_analyzer.{h,cpp}` (FFT),
  `engine/audio/audio_mixer.{h,cpp}` (`resolveSourceGain`, `effectiveBusGain`, buses),
  `engine/audio/audio_music_player.cpp` (music decode + gain, `:347/:350`),
  `engine/audio/audio_engine.{h,cpp}` (`playSynth` `m_synthScratch` `:922/:972`,
  `updateGains` `:1505`, `effectiveDuck`, `loadBuffer`, eviction `:670`),
  `engine/audio/audio_source_state.cpp` (`composeAudioSourceAlState` `:59`),
  `engine/systems/audio_occlusion_system.cpp` (occlusion gate `:411`),
  `engine/editor/panels/audio_panel.cpp` (`drawDebugTab` `:508`).
```
