# Phase 7A: Facial Blend Shapes + Eye Animation Design

## Overview
Adds a facial animation system on top of the existing morph target pipeline. Characters with ARKit-compatible blend shapes get emotion presets with smooth transitions, procedural blink, look-at gaze, and a lip sync merge layer (populated by Batch 7).

## Research Summary

### ARKit 52 Blend Shape Standard
The de facto industry standard (used by MetaHuman, Ready Player Me, VRChat). 52 named blend shapes mapped to FACS Action Units covering eyes (14), jaw (4), mouth (22), brows (5), cheeks/nose/tongue (6). Each shape is a [0,1] weight.

Sources: Apple ARKit BlendShapeLocation docs, ARKit-to-FACS cheat sheet (Melinda Ozel), Py-Feat AU reference.

### Emotion Presets (Ekman 6 + Pain)
Based on FACS research (Ekman, EMFACS), each basic emotion maps to specific AU combinations:
- **Happy**: AU6+AU12 (cheek raise + lip corner pull) = Duchenne smile
- **Sad**: AU1+AU4+AU15 (inner brow raise + brow lower + lip corner depress)
- **Angry**: AU4+AU5+AU23+AU24 (brow lower + upper lid raise + lip press)
- **Surprised**: AU1+AU2+AU5+AU26 (brow raise + lid raise + jaw drop)
- **Fear**: AU1+AU2+AU4+AU5+AU20+AU26 (brow raise+lower + lid raise + lip stretch)
- **Disgust**: AU9+AU15+AU17 (nose wrinkle + lip corner depress + chin raise)
- **Pain**: AU4+AU6+AU7+AU9+AU43 (brow lower + cheek raise + lid tightener + nose wrinkle + eye closure)

### Eye Animation
- **Blink**: ~150ms total (80ms close, 70ms open), asymmetric smoothstep curve, random interval 2-6s, 15% double-blink chance.
- **Gaze**: Direction decomposed into 8 ARKit look shapes (In/Out/Up/Down per eye). Smooth interpolation toward target with configurable speed and anatomical limits (35 deg horizontal, 25 deg vertical).
- **Saccade**: Small random eye movements (0.5-2 deg) every 100-300ms for realism.

Sources: ozz-animation look-at sample, Whizzy Studios eye movement guide, Valve facial expressions primer.

### Layer Architecture
Three additive layers with regional masking:
1. **Emotion (base)**: Full face expression preset
2. **Eye (additive)**: Blink + gaze weights added on top
3. **Lip sync (regional override)**: Overrides mouth/jaw shapes with configurable alpha blend

Source: facial-expressions-unity (mochi-neko), DigitalRune morph target docs.

## Architecture

### Components
```
FacialAnimator (Component)
  |-- Emotion blending (crossfade between presets)
  |-- EyeController (helper class, owned)
  |     |-- Procedural blink
  |     |-- Look-at gaze
  |     |-- Saccade noise
  |-- Lip sync layer (populated by Batch 7)
  |-- Merge pass -> writes to SkeletonAnimator::setMorphWeight()
```

### Data Flow
```
EmotionPreset (sparse name->weight)
  -> resolvePresetWeights() maps to per-index array
  -> smoothstep crossfade between source/target arrays
  -> merge with EyeController weights (additive)
  -> merge with lip sync weights (mouth region override)
  -> clamp [0,1]
  -> SkeletonAnimator::setMorphWeight(index, weight)
  -> existing GPU morph pipeline (SSBO + vertex shader)
```

### Key Classes
- **FacialPresets**: Static data for 8 emotions (NEUTRAL + 7), ARKit shape name constants
- **EyeController**: Procedural blink/gaze/saccade utility, outputs blend shape weights
- **FacialAnimator**: Component that orchestrates emotion + eye + lip sync layers

### Name Resolution
At setup time, `mapBlendShapes(MorphTargetData)` builds a `string->int` map from the model's morph target names. Presets reference shapes by ARKit name string; unresolved names are silently skipped (the model may not have all 52 shapes).

## Performance Considerations
- Emotion weights stored as per-index arrays (not maps) for O(1) per-shape blending
- Name resolution done once at setup, not per frame
- Merge pass iterates only mapped shapes (typically <= 52)
- No allocations in the per-frame update path
- EyeController saccade uses lightweight PRNG (mt19937)

## Files
| File | Type | Description |
|------|------|-------------|
| `animation/facial_presets.h` | New | Emotion enum, EmotionPreset, ARKit 52 constants |
| `animation/facial_presets.cpp` | New | Preset weight data for 8 emotions |
| `animation/eye_controller.h` | New | Blink, gaze, saccade utility |
| `animation/eye_controller.cpp` | New | Eye animation implementation |
| `animation/facial_animation.h` | New | FacialAnimator component |
| `animation/facial_animation.cpp` | New | Layer merge + emotion transition |
| `tests/test_facial_animation.cpp` | New | Unit tests |
