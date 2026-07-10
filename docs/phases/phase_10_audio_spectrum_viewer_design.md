# Phase 10 — AX12: Editor Audio Spectrum / Waveform Viewer (design)

**Roadmap:** AX12 (Audio System section) — *"Real-time spectrum / waveform viewer
in editor."*
**Status:** design — awaiting cold-eyes convergence before implementation.
**Author:** in-session 2026-07-10.
**Depends on:** shipped audio bundles (AX1–AX4, reverb, occlusion, procedural),
`AudioEngine` / `AudioMixer`, `AudioAnalyzer` FFT, editor `AudioPanel` Debug tab,
vendored ImPlot.

---

## 1. Goal & scope

Give the editor's **Window → Audio → Debug** tab a live "mini audio-analyzer" so a
scene author can answer *"why does my mix sound muddy / too bright / too quiet?"*
without a separate DAW. Three user-visible features (full AX12 scope, confirmed
2026-07-10):

1. **Frequency bars** — a live magnitude spectrum (bass → treble) of the current mix.
2. **Scrolling waveform** — the master mix's time-domain signal over the last ~1–2 s.
3. **Per-bus solo** — click a channel (Music / Voice / Sfx / Ambient / Ui) to
   audition it in isolation (mutes the others on the live output).

### 1.1 Non-goals (explicit)

- **Not** the true post-spatialization speaker output. OpenAL Soft mixes internally
  and streams to a hardware device; the finished buffer never reaches engine CPU
  memory (see §2). The viewer analyses an **approximate content mix** reconstructed
  on the CPU from the currently-playing per-source PCM, weighted by the mixer's
  resolved gains. It therefore reflects **frequency balance and per-bus levels**
  (exactly what "muddy mix" is about) but **excludes** HRTF panning, distance
  attenuation, and reverb tails that OpenAL applies last. The Debug tab states this
  limitation inline so the reading is never mistaken for the speaker signal.
- **No new third-party dependency.** Reuses the in-repo radix-2 FFT and the
  already-vendored ImPlot. (Per DEPENDENCY_STANDARDS.md: nothing to add, so no
  dependency-upgrade cold-eyes gate applies — only the standard doc cold-eyes on
  this design.)
- **No mixer rewrite**, no new audio thread, no lock-free ring buffer.

---

## 2. Background — why an approximate mix

`AudioEngine` opens a **normal hardware playback device**
(`alcOpenDevice(nullptr)` → `alcCreateContext`) — `engine/audio/audio_engine.cpp`.
OpenAL Soft performs all sample mixing on its own internal render thread and streams
straight to the driver; there is **no CPU-visible post-mix buffer** anywhere in the
codebase (verified: zero references to `ALC_SOFT_loopback`,
`alcRenderSamplesSOFT`, `alcCaptureSamples`, or any `mixBuffer` / `outputBuffer`).

Two ways to obtain a signal to analyse:

- **(A) CPU pseudo-mix (chosen).** Sum each active source's currently-playing PCM
  window, scaled by its resolved gain (`master × bus × sourceVolume × duck`), into a
  per-bus accumulator and a master accumulator. Approximate but touches nothing
  fragile.
- **(B) `ALC_SOFT_loopback` (rejected).** Yields the true post-mix buffer but
  requires driving the render manually (`alcRenderSamplesSOFT`), which replaces the
  hardware-device path and is **incompatible with the existing device hot-swap**
  (`ALC_SOFT_reopen_device` / `ALC_SOFT_system_events`) that auto-switches output
  when headphones are plugged in. Disproportionate risk for a debug tool.

User decision 2026-07-10: **(A)**.

---

## 3. Architecture

Three small, independently testable units, plus one localized mixer addition and one
shared-helper extraction. The design deliberately separates **pure maths** (unit-
testable without any audio device) from **OpenAL plumbing** from **UI**.

```
 AudioEngine (owns) ── AudioMixMonitor ──produces──▶ MixSnapshot ──read──▶ AudioPanel::drawDebugTab
      │   (plumbing: reads AL_SAMPLE_OFFSET +                                    │ (ImPlot render)
      │    source PCM, weights via resolveSourceGain)                           │
      │                                                                          │
      ├── accumulateMixWindow()  ◀── pure maths (no OpenAL) ── unit tested       ├── computeMagnitudeSpectrum() ◀ shared FFT helper
      │                                                                          │
      └── updateGains() ×busSoloMultiplier()  ◀── solo takes effect on live output
```

### 3.1 Shared spectrum helper (reuse, not rewrite)

`AudioAnalyzer::computeFFT` is today a **private static** radix-2 Cooley-Tukey FFT
inside `engine/experimental/animation/audio_analyzer.{h,cpp}` (Hann-windowed,
magnitude bins; `FFT_SIZE = 512`). Per project reuse rule 3(b), **extract** the FFT +
Hann window + magnitude computation into a shared pure module:

```
engine/audio/audio_spectrum.h / .cpp   (new, pure — no OpenAL, no ImGui)

  // In-place radix-2 Cooley-Tukey FFT (moved verbatim from AudioAnalyzer).
  void computeFFT(std::vector<float>& real, std::vector<float>& imag);

  // Hann-window `in` (n samples), FFT, and write n/2 magnitude bins to `outMag`.
  // Pure: identical output for identical input; the CPU spec for the GPU-free
  // spectrum path. `binHz(i, sampleRate, n) = i * sampleRate / n`.
  void computeMagnitudeSpectrum(const float* in, std::size_t n,
                                std::vector<float>& outMag);
```

`AudioAnalyzer` is refactored to call `computeFFT` from the shared module (behaviour
unchanged — its existing `tests/test_audio_analyzer.cpp` pins that it still produces
the same spectral centroid). No duplicated FFT.

### 3.2 Pure mix accumulator (the "mix rebuilder")

```
// Pure — no OpenAL. Input: one contribution per active source.
struct MixContribution
{
    const float* samples;   // mono PCM window for this source, length = frameCount
    std::size_t  frameCount;
    AudioBus     bus;        // source's assigned (non-Master) bus
    float        gain;       // resolveSourceGain(mixer,bus,vol,duck) — already includes master
};

// Accumulate all contributions into master[frameCount] and
// perBus[AudioBusCount][frameCount] (Master slot = summed = master).
// Sums are NOT clamped (a debug meter must show clipping, not hide it);
// the UI flags |sample| > 1.0 as clip. Deterministic.
void accumulateMixWindow(const std::vector<MixContribution>& contribs,
                         std::size_t frameCount,
                         std::vector<float>& outMaster,
                         std::array<std::vector<float>, AudioBusCount>& outPerBus);
```

This is the byte-for-byte-testable core: fake contributions + known gains → exact
expected sums.

### 3.3 AudioMixMonitor (the plumbing)

`engine/audio/audio_mix_monitor.{h,cpp}` — owned by `AudioEngine`.

- **Active only when the Debug tab is open.** `setActive(bool)`; when inactive it does
  **zero work** (protects the 60 FPS budget — see §5). The panel calls
  `AudioEngine::setMixMonitorActive(m_debugTabVisible)` each frame.
- `const MixSnapshot& capture()` — called at most once per frame while active
  (pull-based, driven from the panel draw). For each active source in the engine's
  existing source pool (the same pool `updateGains` iterates):
  1. `alGetSourcei(id, AL_SAMPLE_OFFSET, &off)` → current playback frame position.
  2. Read a fixed **analysis block** of `WINDOW` mono samples from the source's PCM
     starting at `off` — `WINDOW` is a power of two (design default **1024**, ≈ 23 ms
     at 44.1 kHz) so it feeds the radix-2 FFT directly. Stereo source PCM is
     down-mixed to mono (channel average), indexing by `frame × channels`. Reads past
     the buffer end zero-pad (never OOB).
  3. Compute `gain = resolveSourceGain(mixer, bus, sourceVolume, duck)` — reuse the
     existing pure gain math (no re-derivation).
  4. Emit a `MixContribution`.
  Then call `accumulateMixWindow(...)` → fill `MixSnapshot`.
- `MixSnapshot` = `{ std::vector<float> master; std::array<std::vector<float>,6> perBus;
  int sampleRate; bool anyStreamingMissing; }` — each window is `WINDOW` (1024)
  samples. Same-thread (main/update) production and consumption ⇒ **no locking**.
- **Spectrum vs. waveform history:** the FFT consumes one `WINDOW`-sample block per
  frame (fine-grained, instantaneous). The **scrolling waveform's** ~1–2 s history is
  *not* one huge window — the panel's `ScrollingBuffer` ring appends a decimated tail
  of each frame's master block, so history length is a UI concern independent of
  `WINDOW`.

**Streaming-music sources** (long tracks decoded in chunks that are queued then
unqueued from the AL source) have no retained full-buffer PCM to index by
`AL_SAMPLE_OFFSET`. Handling: the music-stream decoder (`audio_music_stream`) hands
the monitor a **copy of each decoded chunk as it is queued**, into a small per-source
retained ring (a few chunks). The monitor reads the currently-playing chunk from that
ring (selected via `AL_BUFFERS_PROCESSED` / buffer-in-flight bookkeeping the stream
already tracks). This is the one moderate-complexity hook; it is contained to the
stream→monitor boundary and gated behind `setActive` so it costs nothing when the tab
is closed.

### 3.4 Per-bus solo (localized mixer addition)

`AudioMixer` gains solo state as plain data (keeps the "pure data / no OpenAL" invariant):

```
std::array<bool, AudioBusCount> busSolo{};   // default all false = no solo

// Pure: 1.0 if no bus is soloed OR `bus` is itself soloed; else 0.0.
// Master is never solo-muted.
float busSoloMultiplier(const AudioMixer& mixer, AudioBus bus);
```

`resolveSourceGain` is **left untouched** (it is called from many sites; widening it
is unnecessary blast radius). Instead `AudioEngine::updateGains` multiplies its final
per-source gain by `busSoloMultiplier(mixer, sourceBus)`. Because `updateGains`
uploads `AL_GAIN` every frame, toggling solo is audible on the **next frame** with no
manual dirty-flag bookkeeping. Solo is a transient debug/authoring control — it is
**not** persisted to settings.json.

### 3.5 UI — AudioPanel Debug tab

`drawDebugTab` (already exists, `engine/editor/panels/audio_panel.cpp`) gains a
collapsible "Spectrum / Waveform" region above the existing text status.

**ImPlot API note (version currency — global rule 5).** The pinned ImPlot commit
(`1351ab2c`, chosen for ImGui 1.92.x) is on the **v1.0 `ImPlotSpec` API track, well
past v0.16**: the old trailing `flags / offset / stride` positional args are removed
from the `PlotX` signatures and now live on a single `ImPlotSpec` struct passed last.
v0.16-era plot calls will not compile. The log axis is
`SetupAxisScale(ImAxis_X1, ImPlotScale_Log10)` — the old `ImPlotAxisFlags_LogScale`
flag no longer exists at this commit. All plot code below must use the v1.0 idiom.

- **Frequency bars (spectrum):** display path is UI-layer only — the pure
  `computeMagnitudeSpectrum` returns **linear** magnitudes (stays deterministic /
  testable, INV-1); the panel converts to dB and applies ballistics:
  - **Log-frequency X axis** 20 Hz–20 kHz via
    `SetupAxisScale(ImAxis_X1, ImPlotScale_Log10)` — human pitch is ~logarithmic, so
    each octave gets equal width.
  - **dB Y axis:** `dB = 20·log10(max(mag, 1e-9))` (the `1e-9` floor guards
    `log10(0)`), clamped to a **−80 dB display floor**, `SetupAxisLimits(Y1, -80, 0)`.
  - Rendered with **`ImPlot::PlotShaded`** (fill from each bin's dB down to the −80 dB
    floor), not `PlotBars`: on a log axis a fixed `bar_size` is in data units so bars
    would widen unevenly toward the low end; a shaded envelope reads as a clean
    spectrum. Optional `PlotLine` overlay for a crisp top edge.
  - **Ballistics (anti-strobe):** per-bin fast-attack / slow-release smoothing of the
    displayed dB height — `if (v > s) s = v; else s = release·s + (1-release)·v;`
    (`release ≈ 0.9` at 60 FPS) — kept as **UI-side state in the panel**, never fed
    back into the analysed signal, so it does not perturb the pure helper or its tests.
  - A numeric **peak-frequency + peak-dB readout** sits beside the plot (does not rely
    on colour alone — accessibility, §6).
- **Scrolling waveform:** `ImPlot::PlotLine` over the master window, fed through the
  ImPlot-demo `ScrollingBuffer` ring (an `ImVector<ImVec2>` with a wrapping `Offset`;
  interleaved stride set on `ImPlotSpec::Stride`), with the X window pinned by
  `SetupAxisLimits(ImAxis_X1, t - history, t, ImGuiCond_Always)` to get the left-
  scroll. A clip indicator lights when any `|sample| > 1.0` (the accumulator is
  unclamped, INV-3). Compact plot via `ImVec2(-1, <px>)` + `ImPlotFlags_NoLegend` /
  `ImPlotFlags_CanvasOnly`.
- **No double-windowing:** `computeMagnitudeSpectrum` already Hann-windows once before
  the FFT; the panel must not apply a second window (a Hann² widens the main lobe and
  corrupts the dB reading).
- **Solo row:** six toggle buttons (Master shown as "clear solo"); the active solo is
  highlighted. Wire to `AudioMixer::busSolo` via an `AudioEngine` setter.
- Requires **linking `implot_lib` into the engine/editor target** (today it is
  tools-only): a one-line `target_link_libraries` addition in `engine/CMakeLists.txt`
  (no new fetch — ImPlot is already vendored at its pinned post-v0.16 commit).

---

## 4. CPU / GPU placement (project rule 7)

| Work | Placement | Reason |
|------|-----------|--------|
| Pseudo-mix accumulation | **CPU** | Small (≤ ~2 K samples × active voices), branchy (per-source offset/bus decisions), and read-back-bound (`alGetSourcei`). Data is tiny; a GPU round-trip would add latency and complexity for no gain. Matches the "branching / sparse / decision / I/O → CPU" heuristic. |
| FFT (512–2048 pt, ≤ 7 windows/frame) | **CPU** | Microsecond-scale on this data size; must interoperate with ImGui/ImPlot which are CPU-side. |
| Solo gain multiply | **CPU** | Part of the existing per-frame `updateGains` CPU path. |
| Bar / waveform rasterization | **GPU (via ImGui backend)** | ImPlot emits vertex data; the ImGui GL backend draws it — standard, no custom GPU code. |

No dual CPU/GPU implementation is needed (nothing here is a per-pixel / per-vertex
runtime hot path), so no parity test is required.

---

## 5. Performance (60 FPS hard gate)

- **Tab closed (default):** monitor inactive → **zero** added cost (no capture, no
  streaming chunk copies, no FFT). This is the dominant case and keeps the gate safe
  for normal use.
- **Tab open:** once-per-frame capture. Cost ≈ (active voices × ~2 K-sample copy +
  gain calc) + ≤ 7 FFTs of ≤ 2048 pts + ImPlot draw. Order tens of microseconds on
  the RX 6600 target — negligible against the 16.6 ms frame budget. Capture is
  pull-based from the panel draw, so it runs **at most once per rendered frame**.
- Verify with the existing frame-time HUD: open the Debug tab in a busy scene (many
  concurrent voices + streaming music) and confirm frame time stays < 16.6 ms.

---

## 6. Accessibility

- **Reduce-motion:** when `PostProcessAccessibilitySettings.reduceMotion` is set, the
  waveform's left-scroll animation is frozen to a periodic static snapshot (the
  spectrum bars, being an instantaneous readout, stay live). Consistent with how the
  fog/particle systems honour reduce-motion.
- **Not colour-only:** peak frequency and peak level are shown as **text** beside the
  bars, so the reading does not depend on bar colour. Spectrum uses a
  perceptually-ordered colormap, not red/green.
- The panel is opt-in (only drawn when the tab is open) and keyboard-reachable through
  the existing ImGui tab navigation.

---

## 7. Testing plan

Pure-function unit tests (no audio device needed — the reason for the maths/plumbing
split):

- `test_audio_spectrum` — `computeMagnitudeSpectrum` of a synthesized sine at
  frequency *f* (sample rate *sr*, size *n*) peaks in bin `round(f·n/sr)`; a DC input
  peaks in bin 0; silence → all-zero magnitudes.
- `test_audio_mix_monitor` (pure part) — `accumulateMixWindow` with two fake sources
  on different buses and known gains yields `master[i] == g0·s0[i] + g1·s1[i]` and the
  correct per-bus split; empty input → all-zero windows of the requested length;
  short source windows zero-pad (never read out of range).
- Solo: `busSoloMultiplier` returns 1.0 for all buses when none soloed; when Sfx is
  soloed returns 1.0 for Sfx + Master and 0.0 for the rest.
- Regression: existing `test_audio_analyzer` must still pass unchanged after the FFT
  extraction (proves the refactor is behaviour-preserving).

Manual/visual: open Debug tab, play a known 1 kHz tone → bar peak at 1 kHz; solo Music
→ only music audible; clip indicator fires on an intentionally over-unity sum.

---

## 8. Invariants

| ID | Invariant | Test surface |
|----|-----------|--------------|
| INV-1 | `computeMagnitudeSpectrum` is pure: identical input → identical `outMag`; peak bin of a sine at *f* is `round(f·n/sr)`. | `test_audio_spectrum` |
| INV-2 | `accumulateMixWindow` output = Σ(gainᵢ · samplesᵢ) per bus and in master; output length always == requested `frameCount` (short inputs zero-pad, never OOB read). | `test_audio_mix_monitor` |
| INV-3 | Accumulator does **not** clamp — over-unity sums survive so the UI can flag clipping. | `test_audio_mix_monitor` |
| INV-4 | `busSoloMultiplier` = 0 for a non-soloed bus iff ≥1 non-Master bus is soloed; Master is never solo-muted. | `test_audio_mix_monitor` |
| INV-5 | FFT extraction is behaviour-preserving: `AudioAnalyzer` spectral centroid unchanged. | `test_audio_analyzer` (existing) |
| INV-6 | Monitor inactive ⇒ no capture / no FFT / no streaming chunk copy (zero added cost, tab closed). | code review + frame-time check |
| INV-7 | Solo state is not persisted to settings.json (transient authoring control). | code review |
| INV-8 | dB conversion + bar ballistics + peak-hold are **UI-layer only**: `computeMagnitudeSpectrum` returns raw linear magnitudes and its output is unaffected by any display smoothing (keeps INV-1 pure). `log10` input is floored at 1e-9 (no `-inf`). | `test_audio_spectrum` (pure output) + code review |

---

## 9. Implementation order

1. Extract shared FFT → `audio_spectrum.{h,cpp}`; repoint `AudioAnalyzer`; confirm
   `test_audio_analyzer` green. Add `test_audio_spectrum`.
2. `accumulateMixWindow` + `MixSnapshot` + `test_audio_mix_monitor` (pure part).
3. `AudioMixMonitor` plumbing (static/one-shot sources first): `AL_SAMPLE_OFFSET` read
   + `resolveSourceGain` weighting + capture.
4. Streaming-music chunk-copy hook (`audio_music_stream` → monitor ring).
5. Solo: `AudioMixer::busSolo` + `busSoloMultiplier` + `updateGains` multiply + engine
   setter; tests.
6. Debug-tab UI (ImPlot bars + waveform + solo row); link `implot_lib` into engine
   target; reduce-motion + clip indicator.
7. 60 FPS gate verification in a busy scene; CHANGELOG + ROADMAP flip.

---

## 10. Sources

- OpenAL Soft — `ALC_SOFT_loopback` extension (`alcLoopbackOpenDeviceSOFT`,
  `alcRenderSamplesSOFT`): the only route to a true post-mix buffer, and why it
  conflicts with hardware-device hot-swap. <https://openal-soft.org/> extension docs.
- OpenAL 1.1 spec — `AL_SAMPLE_OFFSET` source query (current playback position in
  samples).
- Hann window / short-time Fourier transform for spectral display — Harris, "On the
  use of windows for harmonic analysis with the discrete Fourier transform," Proc.
  IEEE 1978 (standard windowing reference; the in-repo FFT already applies Hann).
- ImPlot **v1.0 `ImPlotSpec` API** (pinned commit `1351ab2c`, post-v0.16): flags /
  offset / stride moved onto `ImPlotSpec`; `SetupAxisScale(ImAxis_X1,
  ImPlotScale_Log10)` replaces the removed `ImPlotAxisFlags_LogScale`; `PlotShaded`
  for the log-frequency spectrum envelope; the demo `ScrollingBuffer` ring for the
  waveform. `implot.h` / `implot_demo.cpp` at that commit; ImPlot Discussion #370
  (v1.0 announcement), Issue #690 (Spec/offset/stride migration).
- Real-time analyzer display conventions — log-frequency axis (~20 Hz–20 kHz), dB
  magnitude (`20·log10(mag)`, −60/−80 dB floor), fast-attack/slow-release ballistics +
  peak-hold, single Hann window: Audacity "Plot Spectrum" manual; SIR
  SpectrumAnalyzer; Oxford Wave Research SpectrumView help; techmind audio
  spectrum-analyser notes; Harris (1978) on windowing.
- In-repo reuse: `engine/experimental/animation/audio_analyzer.{h,cpp}` (FFT),
  `engine/audio/audio_mixer.h` (`resolveSourceGain`, `effectiveBusGain`, buses),
  `engine/editor/panels/audio_panel.cpp` (`drawDebugTab`).
```
