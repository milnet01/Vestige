# `engine/experimental/animation/` — relocated zombie cluster

This directory holds animation subsystems that **compile and pass their unit tests but have no production caller** as of Phase 10.9 (commit `c91c5ed` and earlier). They were relocated here from `engine/animation/` by Phase 10.9 Slice 8 W12 to make the "no caller" status visible at the path level rather than buried in audit notes.

## What's here

| Subsystem | Files | Tests | Original Phase |
|---|---|---|---|
| Motion matching | `motion_matcher.{h,cpp}`, `motion_database.{h,cpp}`, `motion_preprocessor.{h,cpp}`, `trajectory_predictor.{h,cpp}`, `feature_vector.{h,cpp}`, `kd_tree.{h,cpp}`, `mirror_generator.{h,cpp}`, `inertialization.{h,cpp}` | `tests/test_motion_matching.cpp` | Phase 7 |
| Lip sync | `lip_sync.{h,cpp}`, `audio_analyzer.{h,cpp}`, `viseme_map.{h,cpp}` | `tests/test_lip_sync.cpp` | Phase 7 |
| Facial animation orchestrator | `facial_animation.{h,cpp}`, `facial_presets.{h,cpp}`, `eye_controller.{h,cpp}` | `tests/test_facial_animation.cpp` | Phase 7 |

The morph-target SSBO upload + vertex-shader skinning that `FacialAnimator` was supposed to drive **are live** at the renderer / mesh layer — only the orchestrator class is dead. Same for the skeleton + bone-matrix path: `Skeleton`, `SkeletonAnimator`, `AnimationClip`, `AnimationSampler`, `AnimationStateMachine`, `IKSolver`, `MorphTarget` all live in `engine/animation/` and are consumed by production code.

## Why "experimental" instead of deleted

- The code is ~2,400 LoC of working features with passing unit tests; deleting throws that away.
- Wiring any of these subsystems into production requires a multi-week feature push: motion-matching needs a character controller that queries it, lip-sync needs an authored phoneme pipeline, facial animation needs a rigged head with morph targets and an audio source.
- Open-source target: future contributors may want to activate these without re-deriving the math.
- T0 audit (Phase 10.9 Slice 0) confirmed the zero-caller status — this isn't speculative.

## How to activate

To bring a subsystem back to production:

1. Build a real caller (an `engine/character/` system, an editor demo scene, or a game-side hookup) that drives the subsystem each frame.
2. Move the consumed files back from `engine/experimental/animation/` to `engine/animation/`.
3. Update `engine/CMakeLists.txt` to remove the `experimental/animation/` prefix on the moved files.
4. Update `#include` paths in the cluster's own files + their tests.
5. Update `engine/experimental/animation/README.md` to remove the activated subsystem from the table.
6. Write at least one integration test that exercises the production call path, not just the math.

The reverse of these steps is the relocation procedure used by W12.

## Constraints

- **Don't add new code here unless it's deliberately a scratch / not-yet-wired prototype.** The directory is for relocated zombies, not a permanent home for incomplete work.
- **Don't add transitive dependencies from `engine/animation/` (or any production module) to `engine/experimental/animation/`.** That would re-establish the zombie's status as live, which defeats the relocation. Production code in `engine/animation/` must not `#include "experimental/animation/..."`.
- **Tests in `tests/test_*.cpp` may include from here** — the cluster's existing tests do, and they remain in the test suite as regression pins for the math. Future activation work that adds production callers should also add integration tests at the production layer, not replace these unit tests.

## Audit reference

- ROADMAP Phase 10.9 Slice 0 T0: zombie audit confirming no production caller.
- ROADMAP Phase 10.9 Slice 8 W12: this relocation.
- ROADMAP Phase 7: original "shipped" state of the subsystems (with the post-T0 caveat block noting the zombie status).
