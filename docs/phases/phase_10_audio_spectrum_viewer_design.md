# Phase 10 — AX12: Editor Audio Spectrum / Waveform Viewer (design)

**Roadmap:** AX12 (Audio System section) — *"Real-time spectrum / waveform viewer
in editor."*
**Status:** design — awaiting cold-eyes convergence before implementation.
**Author:** in-session 2026-07-10.
**Depends on:** shipped audio bundles (AX1–AX4, reverb, occlusion, procedural),
`AudioEngine` / `AudioMixer`, `AudioMusicPlayer`, `AudioAnalyzer` FFT, editor
`AudioPanel` Debug tab, vendored ImPlot.

## Section index

- §1 Goal & scope (+ non-goals)
- §2 Background — why a producer-push CPU tap
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

1. **Frequency bars** — a live magnitude spectrum (bass → treble) of the analysis mix.
2. **Scrolling waveform** — the analysis mix's time-domain signal over the last ~1–2 s.
3. **Per-bus solo** — click a channel (Music / Voice / Sfx / Ambient / Ui) to audition
   it in isolation (mutes the others on the live output; the graph follows the solo).

### 1.1 The signal: a producer-push "analysis mix"

**Chosen approach (user decision 2026-07-10): analyse only CPU-generated audio, tapped
at the point it is produced.** OpenAL Soft hides both the final speaker mix *and* each
voice's playing samples (see §2). Rather than reconstruct voices from OpenAL, the audio
**producers that already hold their samples on the CPU** — the streaming music player
(which decodes chunks before queueing them) and the procedural synth (AX4, which
synthesises samples on the CPU) — **push a copy** of what they generate into an
`AudioMixMonitor`, already scaled by the source's resolved gain. The monitor sums these
per bus into the **analysis mix** that the viewer transforms and draws.

This is called the **analysis mix** throughout (one term, used consistently) — it is
*not* the speaker output. It reflects the frequency balance and levels of the
CPU-generated content, which is where "muddy mix" lives, but excludes HRTF panning,
distance attenuation, and reverb tails OpenAL applies last.

### 1.2 Non-goals (explicit)

- **File-decoded one-shot SFX are not graphed.** `AudioEngine::loadBuffer` decodes a
  clip, uploads it to OpenAL via `alBufferData`, and **discards its CPU copy**
  (`m_bufferCache` maps `path → ALuint` only; a live voice's `SourceMix` stores
  `{bus, sourceVolume, priority, startTime}` with no path/PCM handle —
  `engine/audio/audio_engine.h`). Analysing them would require retaining every decoded
  clip in RAM and threading a PCM handle through the source system — deliberately out of
  scope. The Debug tab states inline that file one-shots are not shown. **Per-bus solo
  still mutes them on the live output** (§3.4), it just doesn't graph them.
- **Not the true post-spatialization speaker output** (would need `ALC_SOFT_loopback`,
  which replaces the hardware-device path and breaks device hot-swap — §2).
- **No new third-party dependency.** Reuses the in-repo FFT and already-vendored ImPlot.
- **No mixer rewrite, no new audio thread, no lock-free ring buffer** (producers and the
  panel both run on the main/update thread — §3.3).

---

## 2. Background — why a producer-push CPU tap

`AudioEngine` opens a **normal hardware playback device**
(`alcOpenDevice(nullptr)` at `engine/audio/audio_engine.cpp`). OpenAL Soft mixes on its
own internal render thread and streams straight to the driver; there is **no CPU-visible
post-mix buffer** (verified: zero references to `ALC_SOFT_loopback`,
`alcRenderSamplesSOFT`, `alcCaptureSamples`). OpenAL also exposes no read-back of a
buffer's samples, so a *playing* voice's PCM is not recoverable from the engine side.

Two rejected alternatives:

- **`ALC_SOFT_loopback`** — true post-mix, but requires driving the render manually,
  replacing the hardware-device path; incompatible with the existing device hot-swap
  (`ALC_SOFT_reopen_device` / `ALC_SOFT_system_events`). Disproportionate for a debug
  tool.
- **Retain all decoded PCM + reconstruct per voice via `AL_SAMPLE_OFFSET`** — faithful
  to every source but doubles audio memory (a CPU copy of every clip alongside OpenAL's)
  and needs a source→PCM handle threaded through `SourceMix`. Rejected as over-built for
  a debug viewer.

The producer-push tap avoids both: it reads samples **only where they already exist on
the CPU**, at zero extra memory when the tab is closed.

---

## 3. Architecture

Four small, independently testable units, plus one localized mixer addition and one
shared-helper extraction. Pure maths (unit-testable without any audio device) is kept
separate from OpenAL plumbing and from UI.

```
 CPU producers ──submit(bus, samples, gain)──▶ AudioMixMonitor ──snapshot()──▶ AudioPanel::drawDebugTab
 (AudioMusicPlayer decode,                       │  (sums per bus → master,           │ (ImPlot render)
  procedural synth)                              │   rolling history; no-op inactive)  │
                                                 └── accumulate (pure) ── unit tested  ├── computeMagnitudeSpectrum() ◀ shared FFT helper
 AudioMixer.soloBus ──busSoloMultiplier()──▶ folded into the mixer gain path ──▶ every AL_GAIN site + the submitted gain
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
  // bins to `outMag`. NEW code (the analyzer never had a standalone magnitude helper —
  // it fuses magnitude into its centroid loop). Pure/deterministic. Hann divisor is
  // (n-1) to match the analyzer's existing window exactly. binHz(i) = i * sampleRate / n.
  void computeMagnitudeSpectrum(const float* in, std::size_t n,
                                std::vector<float>& outMag);
```

`AudioAnalyzer` is refactored to call the shared `computeFFT` (its magnitude/centroid
code is untouched); behaviour is unchanged and pinned by the existing
`tests/test_audio_analyzer.cpp`. `computeMagnitudeSpectrum` is new — only `computeFFT` is
shared between analyzer and viewer.

### 3.2 Pure accumulator + `AudioMixMonitor` (the plumbing)

`engine/audio/audio_mix_monitor.{h,cpp}` — owned by `AudioEngine`.

- **Active only when the Debug tab is open.** `setActive(bool)`; while inactive,
  `submit(...)` is an immediate no-op and producers skip the copy entirely (§5). The
  panel calls `AudioEngine::setMixMonitorActive(m_debugTabVisible)` each frame.
- **Producer contract (pure summation core):**
  ```
  void beginFrame();                       // snapshot the per-bus write base for this frame
  void submit(AudioBus bus, const float* mono, std::size_t n, float gain);
  void endFrame();                         // advance the rolling cursor by this frame's span
  ```
  `submit` mixes (adds) `gain * mono[i]` into the bus's rolling buffer starting at the
  frame base, so multiple producers in one frame **sum** (not concatenate). Stereo
  producer PCM is down-mixed to mono (channel average) before submit. The pure summation
  — "given a set of `{bus, samples, gain}` for a frame, produce the per-bus and master
  windows" — is factored as a free function `accumulateFrame(...)` and unit-tested
  without any audio object.
- **Cross-producer sample alignment is approximate** (each producer's block is placed at
  the frame base, not sample-accurately interleaved). This is an accepted debug-meter
  approximation, consistent with §1.1; it is *not* claimed to be sample-exact.
- `MixSnapshot snapshot()` returns, for the current frame, `{ std::vector<float> master;
  std::array<std::vector<float>, AudioBusCount> perBus; int sampleRate; }`. `master` is
  the summed analysis mix; `perBus` is kept for a future per-bus view and to keep solo
  self-consistent. Same-thread (main/update) production and consumption ⇒ **no locking.**
- **FFT window vs. waveform history:** the spectrum FFTs the **master** window of
  `WINDOW = 2048` samples (one FFT/frame — see §5). The scrolling waveform's ~1–2 s
  history is a separate UI-side `ScrollingBuffer` ring (§3.5) fed a decimated tail of
  each frame's master block; history length is independent of `WINDOW`.

### 3.3 Producer hooks

The two CPU producers submit at their existing production points, guarded by the
monitor's active flag:

- **Streaming music** — `AudioMusicPlayer` decodes chunks on the CPU before
  `alSourceQueueBuffers` (it owns the PCM ring and per-layer gain,
  `engine/audio/audio_music_player.cpp`). Each decoded chunk is submitted on the `Music`
  bus, scaled by that layer's effective gain (`AudioMusicPlayer::update` already computes
  it, `audio_music_player.cpp:350`). *(`engine/audio/audio_music_stream.h` is only a
  decode-frame-counter state machine and holds no PCM — the hook lives in
  `AudioMusicPlayer`, the PCM/AL-queue owner.)*
- **Procedural audio** (AX4) — the synth produces samples on the CPU at emission time;
  it submits its synthesised block on its assigned bus (typically `Sfx`), scaled by the
  same resolved gain it applies to playback. *(Implementation note: wire the submit at
  the procedural synth's CPU production point; confirm the exact call site against the
  AX4 synth path.)*

Both hooks run on the main/update thread. When `setActive(false)`, neither producer
copies anything.

### 3.4 Per-bus solo (localized mixer addition)

Single-solo model (radio-style — one bus at a time; clicking the active solo clears it),
which avoids a dead `Master` slot:

```
// In AudioMixer (plain data — keeps the "no OpenAL" invariant):
int soloBus = -1;  // -1 = no solo; otherwise a non-Master AudioBus index

// Pure: 1.0 if no bus is soloed OR `bus` is the soloed bus; else 0.0. Master is never muted.
float busSoloMultiplier(const AudioMixer& mixer, AudioBus bus);
```

Solo must reach **both** the live output and the graphed analysis mix, and there are
multiple gain-upload sites — one-shots in `AudioEngine::updateGains` (iterates
`m_livePlaybacks`), music in `AudioMusicPlayer::update`, and procedural. To cover them
all without a per-site edit that could miss one, **fold `busSoloMultiplier` into the
shared mixer gain path** that every site already routes through
(`effectiveBusGain` / `resolveSourceGain`), so:

- every live `AL_GAIN` upload inherits the solo mute (audible isolation), and
- the gain each producer passes to `submit` inherits it too — so **the graph follows the
  solo automatically** (non-soloed buses contribute ~0 to the analysis mix).

*Implementation must verify all three gain sites resolve through the shared path; add the
multiply to any that don't.* The full resolved gain is therefore
`master × bus × sourceVolume × effectiveDuck(bus) × busSoloMultiplier(bus)`
(the `effectiveDuck` router term already exists in `updateGains`,
`audio_engine.h:860-864`). Solo state is **transient authoring state — not persisted** to
settings.json.

### 3.5 UI — AudioPanel Debug tab

`drawDebugTab` (already exists, `engine/editor/panels/audio_panel.cpp:508`) gains a
collapsible "Spectrum / Waveform" region above the existing text status.

**ImPlot API note (version currency — global rule 5).** The pinned ImPlot commit
(`1351ab2c`, header self-reports `IMPLOT_VERSION "1.1 WIP"`, chosen for ImGui 1.92.x) is
on the **post-1.0 `ImPlotSpec` API**: the old trailing `flags / offset / stride`
positional args are removed from the `PlotX` signatures and now live on a single
`ImPlotSpec` passed last; v0.16-era plot calls will not compile. The log axis is
`SetupAxisScale(ImAxis_X1, ImPlotScale_Log10)` — the old `ImPlotAxisFlags_LogScale` no
longer exists. All plot code below uses that idiom.

- **Frequency bars (spectrum):** display path is UI-layer only — `computeMagnitudeSpectrum`
  returns **linear** magnitudes (deterministic/testable, INV-1); the panel converts to dB
  and applies ballistics:
  - **Log-frequency X axis** 20 Hz–20 kHz via `SetupAxisScale(ImAxis_X1, ImPlotScale_Log10)`.
  - **dB Y axis:** `dB = 20·log10(max(mag, 1e-9))` (`1e-9` floor guards `log10(0)`),
    clamped to a **−80 dB display floor**, `SetupAxisLimits(ImAxis_Y1, -80, 0)`.
  - Rendered with **`ImPlot::PlotShaded`** (fill from each bin's dB to the −80 dB floor),
    not `PlotBars`: on a log axis a fixed `bar_size` is in data units so bars widen
    unevenly toward the low end; a shaded envelope reads clean. Optional `PlotLine`
    overlay for a crisp top edge.
  - **Ballistics (anti-strobe):** per-bin fast-attack / slow-release smoothing of the
    displayed dB height — `if (v > s) s = v; else s = release·s + (1-release)·v;`
    (`release ≈ 0.9` at 60 FPS) — held as **UI-side panel state**, never fed back into the
    analysed signal (so the pure helper and its tests are unaffected, INV-8).
  - **Bass resolution:** `WINDOW = 2048` gives ≈ 23 Hz bins at 48 kHz (≈ 21.5 Hz at
    44.1 kHz), so the low-mid region where muddiness lives has usable resolution; this
    trades ~43 ms of time-window latency, acceptable for a meter. (`WINDOW = 1024` would
    halve latency but coarsen bass to ≈ 47 Hz bins.)
  - A numeric **peak-frequency + peak-dB readout** sits beside the plot (does not rely on
    colour alone — accessibility, §6).
- **Scrolling waveform:** `ImPlot::PlotLine` over the master window, fed through a
  hand-copied replica of ImPlot's demo `ScrollingBuffer` (an `ImVector<ImVec2>` ring with
  a wrapping `Offset`; it lives in `implot_demo.cpp`, which is **not** compiled into
  `implot_lib`, so it is replicated in the panel, not linked). The X window is pinned with
  `SetupAxisLimits(ImAxis_X1, t - history, t, ImGuiCond_Always)` for the left-scroll. A
  clip indicator lights when any `|sample| > 1.0` (the accumulator is unclamped, INV-3).
  Compact plot via `ImVec2(-1, <px>)` + `ImPlotFlags_NoLegend` / `ImPlotFlags_CanvasOnly`.
- **No double-windowing:** `computeMagnitudeSpectrum` Hann-windows once before the FFT;
  the panel must not window again (a Hann² widens the main lobe and corrupts the dB read).
- **"File one-shots not shown" note:** a one-line static caption states the graph covers
  CPU-generated audio (music + procedural) only, so a blank Sfx contribution is not read
  as a bug.
- **Solo row:** radio-style toggles for Music / Voice / Sfx / Ambient / Ui (clicking the
  active one clears solo); wired to `AudioMixer::soloBus` via an `AudioEngine` setter.
- Requires **linking `implot_lib` into `vestige_engine`** (the monolithic engine+editor
  STATIC lib, `engine/CMakeLists.txt`; there is no separate editor target). One line —
  `imgui_lib` is already linked so the incremental is `implot_lib` alone. Note this makes
  `implot_lib` a transitive dep of `app/vestige` and every test binary that links
  `vestige_engine`; static linkage keeps object pull-in lazy (no runtime cost where
  unused).

---

## 4. CPU / GPU placement (project rule 7)

| Work | Placement | Reason |
|------|-----------|--------|
| Per-bus accumulation (producer submit) | **CPU** | Samples already exist on the CPU at the producer; summing a few short blocks is trivial and branchy. A GPU round-trip would add latency for no gain. Matches the "branching / sparse / I/O → CPU" heuristic. |
| FFT (one 2048-pt master FFT/frame) | **CPU** | Microsecond-scale at this size; must interoperate with ImGui/ImPlot which are CPU-side. |
| Solo gain multiply | **CPU** | Part of the existing per-frame gain path. |
| Bar / waveform rasterization | **GPU (via ImGui backend)** | ImPlot emits vertex data; the ImGui GL backend draws it — standard, no custom GPU code. |

No dual CPU/GPU implementation is needed (no per-pixel / per-vertex runtime hot path), so
no parity test is required.

---

## 5. Performance (60 FPS hard gate)

- **Tab closed (default):** monitor inactive → producers skip the sample copy and
  `submit` is a no-op → **zero** added cost. The dominant case; keeps the gate safe for
  normal use. Enforced/observable via INV-6 (a submit-call counter asserts 0 while
  inactive).
- **Tab open:** per frame — the producers' sample copies (a few short blocks) + **one**
  2048-pt master FFT + dB/ballistics over ≤ 1024 bins + ImPlot draw. Order tens of
  microseconds on the RX 6600 target, negligible against the 16.6 ms budget. Only the
  master is transformed (1 FFT/frame) — per-bus windows are kept but not FFT'd unless a
  future per-bus view is added.
- Verify with the frame-time HUD: open the Debug tab in a busy scene (many music layers +
  procedural emitters) and confirm frame time stays < 16.6 ms.

---

## 6. Accessibility

- **Reduce-motion:** the waveform's left-scroll is the only animation. Add a new
  per-feature flag `reduceMotionAudioViewer` to `PostProcessAccessibilitySettings`
  (mirroring the existing `reduceMotionFog` / `reduceMotionGi` — there is **no** generic
  `reduceMotion` field); when set, the waveform freezes to a periodic static snapshot
  while the spectrum bars (an instantaneous readout) stay live. This is a small persisted
  accessibility pref (one bool + one checkbox in the existing accessibility settings) —
  distinct from the transient, non-persisted solo state (INV-7).
- **Not colour-only:** peak frequency and peak level are shown as **text** beside the
  bars; the spectrum uses a perceptually-ordered colormap, not red/green.
- The panel is opt-in (drawn only when the tab is open) and keyboard-reachable through the
  existing ImGui tab navigation.

---

## 7. Testing plan

Pure-function unit tests (no audio device — the reason for the maths/plumbing split):

- `test_audio_spectrum` — `computeMagnitudeSpectrum` of a synthesized sine at frequency
  *f* (rate *sr*, size *n* power-of-two) peaks in bin `round(f·n/sr)`; DC input peaks in
  bin 0; silence → all-zero magnitudes; asserts identical output on repeat (purity).
- `test_audio_mix_monitor` — `accumulateFrame` with two fake producers on different buses
  and known gains yields `master[i] == g0·s0[i] + g1·s1[i]` and the correct per-bus split;
  a producer block shorter than `WINDOW` zero-pads (never OOB); empty frame → all-zero
  windows of length `WINDOW`; over-unity sums are **not** clamped (INV-3).
- Solo: `busSoloMultiplier` returns 1.0 for every bus when `soloBus == -1`; when `Sfx` is
  soloed returns 1.0 for `Sfx` (and Master) and 0.0 for the rest.
- INV-5 regression: existing `test_audio_analyzer` passes unchanged after the FFT
  extraction (proves the refactor is behaviour-preserving).
- INV-6: with the monitor inactive, a submit-call/copy counter is asserted `== 0` after a
  simulated producer frame (automated, not just code-review).
- INV-7: a settings.json round-trip asserts no solo key is written/read.

Manual/visual: open Debug tab, play a known 1 kHz music layer → bar peak at 1 kHz; solo
Music → only music audible **and** graphed; solo Sfx → file one-shots muted on output
(graph shows the "not shown" note); clip indicator fires on an intentionally over-unity
sum.

---

## 8. Invariants

| ID | Invariant | Test surface |
|----|-----------|--------------|
| INV-1 | `computeMagnitudeSpectrum` is pure: identical input → identical `outMag`; peak bin of a sine at *f* is `round(f·n/sr)`; requires `n` a power of two. | `test_audio_spectrum` |
| INV-2 | `accumulateFrame` output = Σ(gainᵢ · samplesᵢ) per bus and in master; window length always == `WINDOW` (short producer blocks zero-pad, never OOB read). | `test_audio_mix_monitor` |
| INV-3 | The accumulator does **not** clamp — over-unity sums survive so the UI can flag clipping. | `test_audio_mix_monitor` |
| INV-4 | `busSoloMultiplier` = 0 for a bus iff `soloBus != -1` and `bus != soloBus`; Master is never solo-muted; feeds both live gain and the submitted (graphed) gain, so the display follows solo. | `test_audio_mix_monitor` |
| INV-5 | FFT extraction is behaviour-preserving: only `computeFFT` is shared; `AudioAnalyzer` spectral centroid unchanged (Hann divisor `n-1` preserved). | `test_audio_analyzer` (existing) |
| INV-6 | Monitor inactive ⇒ `submit` is a no-op and producers copy nothing (submit-call counter == 0). | `test_audio_mix_monitor` (counter) |
| INV-7 | Solo state is not persisted to settings.json (transient authoring control). Distinct from the persisted `reduceMotionAudioViewer` accessibility flag. | settings round-trip test |
| INV-8 | dB conversion + bar ballistics + peak-hold are **UI-layer only**: `computeMagnitudeSpectrum` returns raw linear magnitudes, unaffected by display smoothing; `log10` input floored at 1e-9 (no `-inf`). | `test_audio_spectrum` + code review |

---

## 9. Implementation order

1. Extract shared FFT → `audio_spectrum.{h,cpp}`; repoint `AudioAnalyzer` to it; confirm
   `test_audio_analyzer` green. Add `computeMagnitudeSpectrum` + `test_audio_spectrum`.
2. `accumulateFrame` + `AudioMixMonitor` (`setActive` / `beginFrame` / `submit` /
   `endFrame` / `snapshot`) + `test_audio_mix_monitor` (pure summation, zero-pad, no-clamp,
   inactive-counter).
3. Solo: `AudioMixer::soloBus` + `busSoloMultiplier` folded into the shared gain path;
   verify all three AL_GAIN sites route through it; engine setter; tests.
4. Producer hooks: `AudioMusicPlayer` chunk submit (Music bus); procedural synth submit
   (assigned bus) — both guarded by the active flag.
5. `reduceMotionAudioViewer` accessibility flag (+ settings checkbox).
6. Debug-tab UI (ImPlot spectrum + waveform + solo radio row + "file one-shots not shown"
   note + clip indicator); link `implot_lib` into `vestige_engine`.
7. 60 FPS gate verification in a busy scene; CHANGELOG + ROADMAP flip.

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
- Real-time analyzer display conventions — log-frequency axis (~20 Hz–20 kHz), dB
  magnitude (`20·log10(mag)`, −60/−80 dB floor), fast-attack/slow-release ballistics +
  peak-hold, single Hann window: Audacity "Plot Spectrum" manual; SIR SpectrumAnalyzer;
  Oxford Wave Research SpectrumView help; techmind audio spectrum-analyser notes.
- In-repo reuse: `engine/experimental/animation/audio_analyzer.{h,cpp}` (FFT),
  `engine/audio/audio_mixer.h` (`resolveSourceGain`, `effectiveBusGain`, buses),
  `engine/audio/audio_music_player.cpp` (music decode + gain), `engine/audio/audio_engine.{h,cpp}`
  (`updateGains`, `effectiveDuck`, `loadBuffer`), `engine/editor/panels/audio_panel.cpp`
  (`drawDebugTab`).
```
