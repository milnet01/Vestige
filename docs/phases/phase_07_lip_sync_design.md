# Phase 7B: Audio-Driven Lip Sync Design

## Overview

Lip sync system with two modes: (a) pre-baked phoneme tracks from Rhubarb Lip Sync, and (b) real-time amplitude-based fallback. Integrates with the existing FacialAnimator API (`setLipSyncWeight()`, `clearLipSync()`, `setLipSyncAlpha()`).

## Research Summary

### Viseme Standards Evaluated
| Standard | Count | Use Case |
|----------|-------|----------|
| Preston Blair / Rhubarb | 9 (A-H, X) | 2D animation, indie games |
| MPEG-4 FAP | 15 | Academic standard |
| OVR (Meta) | 15 | VR, AAA games |
| Microsoft Azure | 22 | Cloud TTS |

**Decision:** Use Rhubarb's 9-shape set (A-H, X) as primary. Maps naturally to the Preston Blair tradition used in animation for 60+ years. Expandable to 15 OVR visemes later if needed.

### Coarticulation / Blending
- Exponential smoothing on viseme weights prevents jittering
- Lookahead from pre-baked data enables anticipatory lip rounding
- Bilabial consonants (P, B, M = shape A) need priority override for full lip closure

### Sources
- Rhubarb Lip Sync: https://github.com/DanielSWolf/rhubarb-lip-sync
- Preston Blair phoneme chart: Gary C. Martin (garycmartin.com)
- MPEG-4 FAP: ISO/IEC 14496-2
- OVR Lipsync: Meta developer docs
- JALI Model: Edwards et al., SIGGRAPH 2016
- uLipSync: https://github.com/hecomi/uLipSync

## Architecture

```
VisemeMap (static: Viseme -> ARKit blend shape weights)
       ^
       |
LipSyncPlayer (Component)
  ├── Mode A: LipSyncTrack (loaded from JSON, timed mouth cues)
  │   └── Interpolates between visemes with coarticulation smoothing
  ├── Mode B: AudioAnalyzer (real-time RMS + spectral centroid)
  │   └── Maps audio features to estimated viseme weights
  └── Output -> FacialAnimator::setLipSyncWeight()
```

## Viseme-to-ARKit Mapping

Each Rhubarb shape maps to a sparse set of ARKit blend shape weights:

| Rhubarb | Phonemes | ARKit Shapes |
|---------|----------|-------------|
| X (rest) | silence | All zero (neutral mouth) |
| A (closed) | P, B, M | mouthClose=0.8, mouthPressLeft/Right=0.4 |
| B (teeth) | K, S, T, EE | jawOpen=0.1, mouthStretchLeft/Right=0.3, mouthClose=0.3 |
| C (open) | EH, AE | jawOpen=0.35, mouthLowerDownLeft/Right=0.3 |
| D (wide) | AA | jawOpen=0.7, mouthLowerDownLeft/Right=0.5 |
| E (rounded) | AO, ER | jawOpen=0.3, mouthFunnel=0.4, mouthPucker=0.2 |
| F (pucker) | UW, OW, W | mouthPucker=0.7, mouthFunnel=0.5, jawOpen=0.15 |
| G (F/V) | F, V | mouthUpperUpLeft/Right=0.3, jawOpen=0.1 |
| H (L) | L | jawOpen=0.25, tongueOut=0.3, mouthLowerDownLeft/Right=0.2 |

## File Layout

### `animation/viseme_map.h/.cpp`
- `enum class Viseme : uint8_t { X, A, B, C, D, E, F, G, H, COUNT }`
- `struct VisemeEntry { const char* shapeName; float weight; }`
- `struct VisemeShape { Viseme viseme; const char* name; vector<VisemeEntry> entries; }`
- `class VisemeMap` — static access: `get(Viseme)`, `fromRhubarbChar(char)`, `blendWeights()`

### `animation/lip_sync.h/.cpp`
- `struct LipSyncCue { float start; float end; Viseme viseme; }`
- `struct LipSyncTrack { vector<LipSyncCue> cues; float duration; }`
- `class LipSyncPlayer : public Component`
  - `loadTrack(const string& jsonPath)` — parses Rhubarb JSON
  - `loadTrackFromString(const string& json)` — for testing / embedded data
  - `play()`, `pause()`, `stop()`, `setTime(float)`
  - `setSmoothing(float)` — exponential smoothing factor [0,1], default 0.15
  - `setFacialAnimator(FacialAnimator*)`
  - `update(float deltaTime)` — finds current cue, blends weights, feeds FacialAnimator
  - `enableAmplitudeMode()` — switches to real-time audio fallback
  - `feedAudioSamples(const float* samples, size_t count, int sampleRate)` — for amplitude mode

### `animation/audio_analyzer.h/.cpp`
- `class AudioAnalyzer`
  - `feedSamples(const float* samples, size_t count, int sampleRate)` — accepts normalized float PCM
  - `getRMS() const` — current amplitude [0,1]
  - `getSpectralCentroid() const` — brightness [0,1] (requires internal FFT)
  - `getEstimatedViseme() const` — maps RMS + centroid to Viseme enum
  - `getJawOpenWeight() const` — simple jaw weight from amplitude
  - Internal: Cooley-Tukey radix-2 FFT (512-point, ~40 lines)
  - Internal: WAV file reader for offline analysis

## Interpolation & Smoothing

**Track mode:** At each frame:
1. Find current cue at playback time
2. Find next cue for lookahead
3. Compute transition progress within overlap window (last 20% of current cue)
4. Blend current + next viseme weights
5. Apply exponential smoothing: `w = lerp(prev_w, target_w, smoothing)`

**Amplitude mode:**
1. Compute RMS from latest audio buffer
2. Map RMS to jaw opening: `jawOpen = smoothstep(0.01, 0.3, rms)`
3. If FFT available, use spectral centroid to pick vowel shape
4. Apply same exponential smoothing

## Integration with FacialAnimator

```cpp
// Per-frame in LipSyncPlayer::update():
for (auto& [shapeName, weight] : blendedWeights) {
    m_facialAnimator->setLipSyncWeight(shapeName, weight);
}
```

FacialAnimator::mergeAndApply() handles the rest — lip sync weights override emotion for mouth/jaw/tongue shapes at `lipSyncAlpha` blend ratio.

## Test Plan
- VisemeMap: all shapes have entries, roundtrip char conversion, blending
- LipSyncTrack: JSON parsing, cue ordering, time lookup
- LipSyncPlayer: playback state machine, time advancement, loop/stop
- AudioAnalyzer: RMS computation, FFT output, viseme estimation
- Integration: LipSyncPlayer + FacialAnimator weight application

## Performance
- Track mode: O(log N) binary search for current cue, ~20 multiply-adds for weight blending
- Amplitude mode: O(N log N) FFT on 512 samples = trivial at 60 fps
- No allocations per frame (pre-sized weight buffers)
- Zero external dependencies (pure C++17)
