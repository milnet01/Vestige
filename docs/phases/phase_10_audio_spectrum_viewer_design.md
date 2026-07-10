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
- §5 Performance & memory (60 FPS gate)
- §6 Accessibility
- §7 Testing plan
- §8 Invariants
- §9 Implementation order
- §10 Sources
- §11 Cold-eyes loop log

**Named constants** (used throughout): `WINDOW = 2048` (FFT block), `HISTORY_SECONDS = 2.0`
(waveform history), `WAVE_POINTS = 512` (decimated waveform plot points).

---

## 1. Goal & scope

Give the editor's **Window → Audio → Debug** tab a live "mini audio-analyzer" so a
scene author can answer *"why does my mix sound muddy / too bright / too quiet?"*
without a separate DAW. Three user-visible features (full AX12 scope, confirmed
2026-07-10):

1. **Frequency bars** — a live magnitude spectrum (bass → treble) of the selected bus.
2. **Scrolling waveform** — the selected bus's time-domain signal over the last
   `HISTORY_SECONDS`.
3. **Per-bus solo / select** — pick a channel (Music / Sfx have a signal; see §1.2) to
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

Per-bus (not one summed "master mix") because the producers are asynchronous and
heterogeneous: the music player decodes *ahead* of playback and a file's rate may differ
from the synth's, so summing them into one time-aligned signal is incoherent. Analysing
each bus on its own timeline and rate is coherent, makes solo trivial and eviction-safe
(§3.4), and still answers the muddiness question. A summed-master view is deferred (§1.2).

This per-bus signal is the **analysis signal** throughout (one term, used consistently) —
it is *not* the speaker output. It reflects the frequency balance and levels of a bus's
CPU-generated content but excludes HRTF panning, distance attenuation, and reverb tails
OpenAL applies last. Because the music producer decodes ahead of playback (keep-ahead
window ~0.30–0.60 s, `audio_music_stream.h:53-54`), the **Music** bus's analysis *leads*
the speakers by that window — acceptable for spectral-balance debugging (frequency
content, not phase), and noted in the tab.

### 1.2 Non-goals (explicit)

- **Only Music and Sfx carry an analysis signal in v1.** The two CPU producers feed the
  `Music` bus (streaming music) and the `Sfx` bus (procedural synth — footsteps/impacts).
  `Voice`, `Ambient`, and `Ui` are file-decoded (see below) and have **no CPU producer**,
  so they read silent; the bus selector greys them out with a tooltip ("file-decoded — not
  graphed"). Solo (output mute) still works on all buses (§3.4).
- **No summed "master mix" spectrum in v1** (needs rate/timeline reconciliation the
  per-bus model deliberately avoids). Deferred.
- **File-decoded one-shot SFX are not graphed.** `AudioEngine::loadBuffer` decodes a clip,
  uploads it to OpenAL via `alBufferData`, and **discards its CPU copy** (`m_bufferCache`
  maps `path → ALuint` only; a live voice's `SourceMix` stores
  `{bus, sourceVolume, priority, startTime}` — no path/PCM handle, `audio_engine.h:831`).
  Analysing them would need retaining every clip in RAM + a source→PCM handle — out of
  scope.
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
  replacing the hardware-device path; incompatible with device hot-swap
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
 (AudioMusicPlayer,                                   │ per-bus frame-sum (pending) →     │ (flushFrame once/frame,
  FootstepSystem, ImpactAudioSystem via playSynth)    │ per-bus content ring; no-op idle  │  then draw selected bus)
                                                       └── accumulateBusFrame (pure) ── unit tested   ├── computeMagnitudeSpectrum() ◀ shared FFT
 AudioMixer.soloBus ──busSoloMultiplier()──▶ post-multiply at AL_GAIN OUTPUT sites only ──▶ audible isolation
                                              (resolveSourceGain / eviction / occlusion left untouched)
```

### 3.1 Shared spectrum helper (reuse, not rewrite)

`AudioAnalyzer::computeFFT` is today a **private static** radix-2 Cooley-Tukey FFT inside
`engine/experimental/animation/audio_analyzer.{h,cpp}` (`FFT_SIZE = 512`, Hann-windowed
with divisor `FFT_SIZE-1` for the lip-sync spectral centroid). Per project reuse rule 3(b),
**extract only the raw FFT** into a shared pure module:

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

- **Active only when the Debug tab is open.** `setActive(bool)` / `isActive()`; producers
  check `isActive()` and skip the int16→float copy when false, and `submit(...)` is itself a
  no-op when inactive (§5). The panel calls `AudioEngine::setMixMonitorActive(tabVisible)`.
- **Sample domain:** producers submit **normalized-float mono** samples. Both real
  producers hold **int16** PCM (music: `stb_vorbis_get_samples_short_interleaved`,
  `AL_FORMAT_*16`; procedural: `m_synthScratch` is `std::vector<std::int16_t>`,
  `audio_engine.h:916`), so each producer converts int16→float by `s / 32768.0f` and (music
  only — the synth path is already `AL_FORMAT_MONO16`, `audio_engine.cpp:956`) down-mixes
  stereo→mono by channel average **before** `submit`. All spectrum/waveform math and the
  clip test operate in this normalized-float domain.
- **Submit-anytime + panel-driven flush (no per-system bracket).** Producers call
  `submit(AudioBus, const float* mono, std::size_t n, float gain, int sampleRate)` at any
  point during the frame from their own systems — there is **no** `beginFrame`/`endFrame`
  bracket around a single system (an earlier design bracketed `AudioSystem::update`, which
  would have missed `playSynth`; the procedural producers update in separate systems, §3.3).
  `submit` sums `gain*mono` into that bus's **pending frame accumulator** (aligned at index
  0; short blocks zero-pad; repeated same-bus submits in a frame **sum** — correct because a
  bus's producers share one timeline), sets a per-bus `hadSubmitThisFrame` flag, and records
  the bus's rate. The **pure** summation ("given a frame's `{samples, gain}` submits for one
  bus, produce that bus's summed block of length = the frame's max submitted length") is the
  free function `accumulateBusFrame(...)`, unit-tested with no audio object.
- **`flushFrame()` — called once per frame by the panel** at the top of `drawDebugTab`,
  which runs during editor draw and is therefore **guaranteed after every producer system
  has updated that frame**. It appends each bus's pending accumulator to that bus's rolling
  **content ring**, then clears the pending accumulators + `hadSubmitThisFrame` flags. This
  single well-defined flush point removes the ordering dependency the bracket design had.
- **Per-bus content rings + idle behaviour.** Each bus's content ring holds its recent
  normalized-float signal (capacity `WINDOW + HISTORY_SECONDS * rate`). The spectrum FFTs
  the **last `WINDOW = 2048` samples** of the selected bus's ring (a rolling STFT window,
  ≈43 ms at 48 kHz; a ring holding < `WINDOW` samples at cold start front-pads with zeros).
  The waveform reads the last `HISTORY_SECONDS` decimated to `WAVE_POINTS`. **When a bus is
  idle (no submit this frame), nothing is appended, so its display holds its last trace
  (freezes) until playback resumes** — the tab notes this; the previous "decays to silence"
  wording was wrong (a zero-length append cannot decay a ring).
- **Rates handled by construction:** each bus stores the rate of its producer's submits;
  `binHz` for the displayed spectrum uses **that bus's** rate. Buses are never summed
  together, so rates need not match across buses. (If two producers ever share a bus at
  differing rates, the monitor keeps the first rate seen and logs a warning **once per bus
  per process** — an accepted debug-tool limitation.)
- `MixSnapshot snapshot()` exposes, per bus, the content-ring tail, its `rateHz`, and
  `hadRecentSignal` (so the panel shows silence for a bus idle this frame). `[Master]` slots
  are unused (Master is the aggregate concept, never a producer bus); indexing stays by
  `AudioBus`. Same-thread (main/update) production + panel consumption ⇒ **no locking.**

### 3.3 Producer hooks

Both CPU producers submit at their existing production points, guarded by `isActive()`:

- **Streaming music** — `AudioMusicPlayer::update` decodes int16 chunks and computes each
  layer's **resolved (clamped ≤ 1.0) gain**
  (`resolveSourceGain(*mixer, AudioBus::Music, layer.gain.currentGain, duckGain)`,
  `audio_music_player.cpp:347`; uploaded at `:350`). Each layer's decoded chunk is
  normalized to float, down-mixed to mono, and submitted on the `Music` bus scaled by that
  layer's resolved gain, at the file's decode rate. *(`audio_music_stream.h` is a
  decode-frame counter holding no PCM — the hook lives in `AudioMusicPlayer`, the PCM/AL
  owner.)*
- **Procedural audio** (AX4) — `AudioEngine::playSynth` synthesises **mono** int16 into
  `m_synthScratch` via `m_synthBank.synthesize(..., m_synthScratch)` (`audio_engine.cpp:922`)
  and resolves gain at `:972`. The synth block is normalized and submitted on its assigned
  bus (typically `Sfx`) at the synth's rate, tapped right after `synthesize`. `playSynth` is
  invoked from `FootstepSystem::update` and `ImpactAudioSystem::update` (not from
  `AudioSystem`) — which is exactly why the flush is panel-driven (§3.2), not bracketed
  around one system.

All hooks run on the main/update thread. When inactive, no producer copies anything.

### 3.4 Per-bus solo (localized mixer addition, output-only)

Single-solo model (radio-style — one bus at a time; clicking the active solo clears it),
which doubles as the viewer's **bus selector** (soloed bus = displayed bus; no solo →
panel shows `Music`):

```
// In AudioMixer (plain data — keeps the "no OpenAL" invariant):
int soloBus = -1;  // -1 = no solo; otherwise a non-Master AudioBus index

// Pure: 1.0 if no bus is soloed OR `bus` is the soloed bus; else 0.0. Master never muted.
float busSoloMultiplier(const AudioMixer& mixer, AudioBus bus);
```

**Solo is applied ONLY at the final `AL_GAIN` output-upload path, as a post-multiply on the
already-resolved gain — never inside `resolveSourceGain` / `effectiveBusGain`.** Those
shared functions are also called for **decisions that must stay solo-agnostic**:
voice-eviction scoring (`AudioEngine::acquireSource`, `audio_engine.cpp:670`) and occlusion
ray-gating (`AudioOcclusionSystem::update`, `audio_occlusion_system.cpp:411`). Folding solo
into them would make soloing a bus evict/kill non-soloed voices and stall their occlusion.

The complete `alSourcef(src, AL_GAIN, g)` upload set is
`audio_engine.cpp:757/802/848/897/977/1318/1507` + `audio_music_player.cpp:350`
(grep-confirmed). Apply `g *= busSoloMultiplier(mixer, bus)` at each, where `bus` is in
scope: the `play*`/`playSynth` params, `updateGains` (`mix.bus`), and music (`AudioBus::Music`).
The one site without a `bus`/`mixer` in scope is `applySourceState` (`:1318`, which uploads
the spatial `state.gain`); apply its solo multiply upstream in **`composeAudioSourceAlState`**
(`audio_source_state.cpp:59`, where `mixer` + `comp.bus` are in scope and `state.gain` is
formed) — still **outside** `resolveSourceGain`, so the solo-agnostic invariant holds.

Consequences for the analysis path: submitted (graphed) samples are scaled by the **content
gain only** (no solo), so each bus's ring always holds that bus's real content and the user
can switch the displayed bus freely; solo affects **only what you hear** and **which bus is
shown**. Solo state is **transient authoring state — not persisted** to settings.json.

Full resolved *output* gain per source is therefore
`master × bus × sourceVolume × effectiveDuck(bus) × busSoloMultiplier(bus)` — the first four
via the existing `resolveSourceGain` (`audio_mixer.cpp:82,96`, output clamped to [0,1]) +
`effectiveDuck` (`audio_engine.h:860`), and `busSoloMultiplier` layered on at upload.

### 3.5 UI — AudioPanel Debug tab

`drawDebugTab` (already exists, `audio_panel.cpp:508`) gains a collapsible "Spectrum /
Waveform" region above the existing text status. It calls `monitor.flushFrame()` once at the
top, then reads `snapshot()`.

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
    bin→Hz uses the selected bus's rate.
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
    44.1 kHz) — usable in the low-mid muddiness region; trades ~43 ms of window latency.
  - A numeric **peak-frequency + peak-dB readout** sits beside the plot (this is the
    not-colour-only affordance — the shaded spectrum is a single fill, so no information is
    encoded in hue).
- **Scrolling waveform — selected bus:** `ImPlot::PlotLine` over the selected bus's content
  ring, last `HISTORY_SECONDS` decimated to `WAVE_POINTS`, via a hand-copied replica of
  ImPlot's demo `ScrollingBuffer` (an `ImVector<ImVec2>` ring; it lives in `implot_demo.cpp`,
  which is **not** compiled into `implot_lib`, so it is replicated in the panel, not linked).
  The X window is pinned with `SetupAxisLimits(ImAxis_X1, t - HISTORY_SECONDS, t,
  ImPlotCond_Always)`. Because producers deliver samples in chunks (music decodes ahead), the
  scroll advances in chunk-sized steps, not perfectly smoothly — an accepted debug-tool
  approximation; an idle bus holds its last trace (§3.2). A **clip indicator** lights when any
  normalized sample `|s| > 1.0` — which on the graphed buses arises from **summing overlapping
  same-bus contributions** (multiple music layers, or overlapping procedural hits); a single
  contribution cannot exceed 1.0 because `resolveSourceGain` clamps to [0,1] and neither
  graphed producer receives AX9 loudness makeup (music: none; synth: `volume = 1.0`, no makeup
  — `audio_engine.cpp:967`). The ring is unclamped (INV-3). Compact plot via `ImVec2(-1, <px>)`
  + `ImPlotFlags_NoLegend` / `ImPlotFlags_CanvasOnly`.
- **No double-windowing:** `computeMagnitudeSpectrum` Hann-windows once before the FFT; the
  panel must not window again (a Hann² widens the main lobe and corrupts the dB read).
- **Bus select / solo row:** radio-style toggles; only `Music` and `Sfx` are enabled (the
  two with producers), `Voice`/`Ambient`/`Ui` greyed with a "file-decoded — not graphed"
  tooltip (§1.2). The active toggle is the displayed bus, wired to `AudioMixer::soloBus` via
  an `AudioEngine` setter; clicking the active one clears solo → falls back to viewing
  `Music`. A caption notes the Music view leads the speakers by the decode-ahead window (§1.1).
  A panel-local **"Freeze waveform"** checkbox pauses the scroll (§6).
- Requires **linking `implot_lib` PUBLIC into `vestige_engine`** (the monolithic
  engine+editor STATIC lib, `engine/CMakeLists.txt:372`; there is no separate editor target).
  One line — `imgui_lib` is already linked (`:442`) so the incremental is `implot_lib` alone.
  This makes `implot_lib` a transitive dep of `app/vestige` and every test binary linking
  `vestige_engine` (static linkage keeps object pull-in lazy); the existing explicit
  `implot_lib` link in `tools/CMakeLists.txt` (`formula_workbench`) becomes redundant but
  harmless.

---

## 4. CPU / GPU placement (project rule 7)

| Work | Placement | Reason |
|------|-----------|--------|
| Per-bus accumulation (submit / frame-sum / ring append) | **CPU** | Samples already exist on the CPU at the producer; summing a few short blocks is trivial and branchy. A GPU round-trip would add latency for no gain. Matches the "branching / sparse / I/O → CPU" heuristic. |
| FFT (one 2048-pt FFT of the selected bus, per frame) | **CPU** | Microsecond-scale at this size; must interoperate with ImGui/ImPlot which are CPU-side. |
| Solo gain multiply | **CPU** | Part of the existing per-frame gain-upload path. |
| Bar / waveform rasterization | **GPU (via ImGui backend)** | ImPlot emits vertex data; the ImGui GL backend draws it — standard, no custom GPU code. |

No dual CPU/GPU implementation is needed (no per-pixel / per-vertex runtime hot path), so
no parity test is required.

---

## 5. Performance & memory (60 FPS hard gate)

- **Tab closed (default):** monitor inactive → producers skip the copy and `submit` is a
  no-op → **zero** added CPU cost and **zero** added memory (rings allocated lazily on first
  activation, or freed on close). Dominant case; keeps the gate safe. Observable via INV-6.
- **Tab open — CPU:** per frame — the active producers' int16→float copies (a few short
  blocks) + per-bus frame-sum + ring appends + **one** 2048-pt FFT of the *selected* bus +
  dB/ballistics over ≤ 1024 bins + ImPlot draw. Only the selected bus is FFT'd. Order tens of
  microseconds on the target CPU (Ryzen 5 5600), negligible against the 16.6 ms budget.
- **Tab open — memory:** each populated bus's content ring is `WINDOW + HISTORY_SECONDS·rate`
  floats ≈ (2048 + 2.0·48000)·4 B ≈ **~390 KB/bus**. Only `Music` + `Sfx` carry a signal in
  v1, so ~0.8 MB typical; ≤ ~2.3 MB if all six slots are allocated. Freed when the tab closes.
- Verify with the frame-time HUD: open the Debug tab in a busy scene (many music layers +
  procedural emitters) and confirm frame time stays < 16.6 ms.

---

## 6. Accessibility

- **Reduce-motion:** the waveform's left-scroll is the strongest animation. It is paused by a
  **panel-local "Freeze waveform" checkbox** (transient UI state — *not* a persisted setting).
  This is deliberately lightweight: the viewer is opt-in developer tooling that only animates
  while its tab is open, so a panel toggle is proportionate and avoids threading a new field
  through the settings wire/JSON layer. (If a global audio reduce-motion preference is added
  later, it can drive this toggle's default — noted as follow-up, not built now.) The spectrum
  bars remain animated (an instantaneous ballistic readout, far less motion than a scrolling
  trace).
- **Not colour-only:** peak frequency and peak level are shown as **text** beside the bars;
  the spectrum fill encodes no information in hue, so nothing depends on colour.
- The panel is opt-in (drawn only when the tab is open) and keyboard-reachable through the
  existing ImGui tab navigation.

---

## 7. Testing plan

Pure-function unit tests (no audio device — the reason for the maths/plumbing split):

- `test_audio_spectrum` — `computeMagnitudeSpectrum` of a synthesized sine at frequency *f*
  (rate *sr*, size *n* power-of-two) peaks in bin `round(f·n/sr)`; DC input peaks in bin 0;
  silence → all-zero magnitudes; identical output on repeat (purity, INV-1); a pre-windowed
  input is not windowed again (INV-8 Hann-once).
- `test_audio_mix_monitor` — `accumulateBusFrame` with two fake same-bus submits and known
  gains yields `frame[i] == g0·s0[i] + g1·s1[i]` and length == the frame's max submitted
  length (short blocks zero-pad, never OOB, INV-2); over-unity sums (two contributions each
  ≤1.0 summing >1.0) are **not** clamped (INV-3); a bus with no submit this frame appends
  nothing, so its ring tail is unchanged (idle-freeze, INV-2); with the monitor inactive, a
  submit-call counter is `== 0` after a simulated producer frame (INV-6); `submit` before any
  `flushFrame` still accumulates into pending and is flushed by the next `flushFrame`
  (submit-anytime contract).
- `test_audio_mixer` — `busSoloMultiplier` returns 1.0 for every bus when `soloBus == -1`;
  with `Sfx` soloed returns 1.0 for `Sfx` (and Master) and 0.0 for the rest. **Set
  `mixer.soloBus` and assert `resolveSourceGain` / `effectiveBusGain` outputs are unchanged**
  (proves the eviction/occlusion exemption, INV-4).
- INV-5 regression: existing `test_audio_analyzer` passes unchanged after the FFT extraction.
- INV-7: a settings.json round-trip asserts solo is **not** persisted (this feature adds no
  persisted settings field at all).

Manual/visual: open Debug tab, play a known 1 kHz music layer → Music-bus bar peak at 1 kHz;
solo Music → only music audible and displayed; solo Sfx → Sfx view shows procedural hits, file
one-shots muted on output; overlap two music layers loud enough that their sum exceeds unity →
clip indicator fires; soloing a bus does not change which voices get evicted under pool
pressure; select Voice/Ambient/Ui → greyed, shows the "not graphed" note.

---

## 8. Invariants

| ID | Invariant | Test surface |
|----|-----------|--------------|
| INV-1 | `computeMagnitudeSpectrum` is pure: identical input → identical `outMag`; peak bin of a sine at *f* is `round(f·n/sr)`; requires `n` a power of two. | `test_audio_spectrum` |
| INV-2 | `accumulateBusFrame` output = Σ(gainᵢ · samplesᵢ) for one bus's frame submits; length == the frame's max submitted length (short blocks zero-pad, never OOB). Different buses are never summed. A bus with no submit appends nothing (idle → display holds last trace). | `test_audio_mix_monitor` |
| INV-3 | The accumulator/ring does **not** clamp — over-unity sums (from summing overlapping same-bus contributions) survive so the UI can flag clipping in the normalized-float domain. | `test_audio_mix_monitor` |
| INV-4 | `busSoloMultiplier` = 0 for a bus iff `soloBus != -1` and `bus != soloBus`; Master never muted. Setting `soloBus` leaves `resolveSourceGain` / `effectiveBusGain` outputs unchanged (eviction + occlusion stay solo-agnostic; solo lives only at AL_GAIN upload). | `test_audio_mixer` |
| INV-5 | FFT extraction is behaviour-preserving: only `computeFFT` is shared; `AudioAnalyzer` spectral centroid unchanged (Hann divisor `n-1` preserved). | `test_audio_analyzer` (existing) |
| INV-6 | Monitor inactive ⇒ `submit` is a no-op and producers copy nothing (submit-call counter == 0). | `test_audio_mix_monitor` |
| INV-7 | The feature persists no settings: solo and the "Freeze waveform" toggle are transient panel/authoring state; settings.json round-trip shows no new key. | settings round-trip test |
| INV-8 | dB conversion + bar ballistics + peak-hold are **UI-layer only**: `computeMagnitudeSpectrum` returns raw linear magnitudes, unaffected by display smoothing; input is Hann-windowed exactly once; `log10` input floored at 1e-9 (no `-inf`). | `test_audio_spectrum` + code review |

---

## 9. Implementation order

1. Extract shared FFT → `audio_spectrum.{h,cpp}`; repoint `AudioAnalyzer`; confirm
   `test_audio_analyzer` green. Add `computeMagnitudeSpectrum` + `test_audio_spectrum`.
2. `accumulateBusFrame` + `AudioMixMonitor` (`setActive`/`isActive`/`submit`/`flushFrame`/
   `snapshot`, per-bus pending + content rings, per-bus rate, idle-freeze) +
   `test_audio_mix_monitor`.
3. Solo: `AudioMixer::soloBus` + `busSoloMultiplier`; post-multiply at every AL_GAIN upload
   site (§3.4, incl. the `composeAudioSourceAlState` placement for `:1318`); engine setter;
   `test_audio_mixer` incl. the resolveSourceGain-invariance check.
4. Producer hooks: `AudioMusicPlayer` per-layer submit (Music); `playSynth` `m_synthScratch`
   submit (Sfx) — both int16→float, guarded by `isActive()`.
5. Debug-tab UI: `flushFrame` + `snapshot` read; ImPlot spectrum + waveform + bus-select/solo
   row (Music/Sfx enabled, others greyed) + captions + clip indicator + "Freeze waveform"
   toggle; link `implot_lib` PUBLIC into `vestige_engine`.
6. 60 FPS + memory gate verification in a busy scene.
7. CHANGELOG; ROADMAP — flip status **and** correct the stale "pocketfft / master bus" text on
   the AX12 bullet to match this design.

---

## 10. Sources

- OpenAL Soft — `ALC_SOFT_loopback` (`alcLoopbackOpenDeviceSOFT`, `alcRenderSamplesSOFT`):
  the only route to a true post-mix buffer, and why it conflicts with hardware-device
  hot-swap. <https://openal-soft.org/> extension docs.
- OpenAL 1.1 spec — no buffer-data read-back, motivating the producer-push tap.
- Hann window / STFT for spectral display — Harris, "On the use of windows for harmonic
  analysis with the discrete Fourier transform," Proc. IEEE 1978.
- ImPlot **post-1.0 `ImPlotSpec` API** (pinned commit `1351ab2c`, header
  `IMPLOT_VERSION "1.1 WIP"`): flags / offset / stride moved onto `ImPlotSpec`;
  `SetupAxisScale(ImAxis_X1, ImPlotScale_Log10)` replaces the removed
  `ImPlotAxisFlags_LogScale`; `PlotShaded` for the log-frequency envelope; the demo
  `ScrollingBuffer` ring (in `implot_demo.cpp`, replicated not linked). `implot.h` /
  `implot_demo.cpp` at that commit; ImPlot Discussion #370 (v1.0), Issue #690 (Spec migration).
- Real-time analyzer display conventions — log-frequency axis (~20 Hz–20 kHz), dB magnitude
  (`20·log10(mag)`, −60/−80 dB floor), fast-attack/slow-release ballistics + peak-hold, single
  Hann window: Audacity "Plot Spectrum" manual; SIR SpectrumAnalyzer; Oxford Wave Research
  SpectrumView help; techmind audio spectrum-analyser notes.
- In-repo reuse: `engine/experimental/animation/audio_analyzer.{h,cpp}` (FFT),
  `engine/audio/audio_mixer.{h,cpp}` (`resolveSourceGain` `:82/:96`, `effectiveBusGain` `:74`,
  buses), `engine/audio/audio_music_player.cpp` (music decode + gain, `:347/:350`),
  `engine/audio/audio_engine.{h,cpp}` (`playSynth` `m_synthScratch` `:922/:972`, `updateGains`
  AL_GAIN `:1507`, `effectiveDuck` def `audio_engine.h:860`, `loadBuffer`, eviction `:670`),
  `engine/audio/audio_source_state.cpp` (`composeAudioSourceAlState` `:59`),
  `engine/systems/audio_occlusion_system.cpp` (occlusion gate `:411`),
  `engine/systems/footstep_system.cpp` / `engine/systems/impact_audio_system.cpp` (playSynth
  callers), `engine/editor/panels/audio_panel.cpp` (`drawDebugTab` `:508`).

---

## 11. Cold-eyes loop log

Per global rule 14 / project rule 9, this design ran through `/cold-eyes` (3 independent cold
reviewers per pass, findings verified against source, every actionable severity fixed).

- **Loop 1** — CRITICAL 0 · HIGH 4 · MEDIUM 7 · LOW ~10. Original AL_SAMPLE_OFFSET-reconstruction
  approach had no PCM source (engine discards decoded PCM); music path targeted the wrong module;
  `reduceMotion` field didn't exist; implot-target mis-described. → user re-scoped to the cheaper
  CPU-native viewer; redesigned to producer-push.
- **Loop 2** — CRITICAL 1 · HIGH 3 · MEDIUM 6 · LOW 8. Folding solo into `resolveSourceGain` would
  corrupt voice-eviction + occlusion (both call it); summed-timeline model was incoherent
  (decode-ahead, mixed rates, chunk cadence); producers are int16 not float. → per-bus model;
  output-only solo; int16→float; pinned procedural hook.
- **Loop 3** — CRITICAL 1 · HIGH 4 · MEDIUM 4 · LOW ~10. `AudioSystem::update` bracket missed
  `playSynth` (fires from Footstep/Impact systems); `applySourceState` solo site lacked bus/mixer;
  idle "decays to silence" was impossible; clip mis-attributed to loudness makeup; settings-wire
  path under-specified. → panel-driven `flushFrame` (no bracket); solo multiply in
  `composeAudioSourceAlState`; idle = freeze; clip = same-bus summation; reduce-motion as a
  panel-local toggle (dropped the persisted-flag/wire plumbing).
- **Loop 4** — pending (this revision).
```
