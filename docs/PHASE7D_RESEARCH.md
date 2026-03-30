# Phase 7D Research: Inverse Kinematics

## Scope

1. **Two-bone IK solver** — analytic solver for 3-joint chains (arm, leg)
2. **Look-at IK** — orient a joint chain (head/spine) to face a target
3. **Foot IK** — plant feet on uneven ground using raycasts + two-bone IK
4. **Hand IK** — reach for world targets (door handles, weapons)
5. **IK blending** — weight-based blend between animation pose and IK solution

## Two-Bone IK Algorithm

Source: [The Orange Duck — Simple Two Joint IK](https://theorangeduck.com/page/simple-two-joint)

The algorithm decomposes into two steps:

1. **Adjust interior triangle angles** using law of cosines:
   - Given bone lengths `lab`, `lcb` and clamped target distance `lat`
   - Compute desired angles: `acos((lcb²-lab²-lat²)/(-2·lab·lat))`
   - Delta from current angles gives rotation amounts

2. **Swing chain toward target**:
   - Rotate around `cross(current_effector_dir, target_dir)`

**Pole vector** resolves the elbow/knee ambiguity — defines which plane the chain lies in. Uses `mid_joint.forward` as stable reference when chain is nearly extended.

**Edge cases**: Target clamped to `[eps, totalLength - eps]` to avoid degenerate full extension/collapse. ozz-animation adds a `soften` parameter for smooth asymptotic approach.

## Look-At IK

Single-joint aim solver: compute rotation from joint's current forward to the target direction, optionally constrained to a maximum angle. For multi-joint (spine + head), distribute the rotation across joints with decreasing weight.

## Foot IK

Pattern from [ozz-animation foot_ik sample](http://guillaumeblanc.github.io/ozz-animation/samples/foot_ik/):
1. Raycast down from hip to find ground height at each foot
2. Compute pelvis offset (lower the whole skeleton so lowest foot touches ground)
3. Apply two-bone IK to each leg chain (hip→knee→ankle) with new ankle target
4. Apply aim IK to ankle to align foot flat with ground normal

## IK Blending

Standard approach from [ozz](https://guillaumeblanc.github.io/ozz-animation/documentation/ik/):
- Each IK solver outputs correction quaternions per joint
- Blend via NLerp between identity and correction, controlled by `weight ∈ [0,1]`
- Per-joint weight masks allow selective IK (e.g., only upper body)

## Sources

- [Simple Two Joint IK — The Orange Duck](https://theorangeduck.com/page/simple-two-joint)
- [ozz-animation IK Documentation](https://guillaumeblanc.github.io/ozz-animation/documentation/ik/)
- [ozz Two Bone IK Sample](https://guillaumeblanc.github.io/ozz-animation/samples/two_bone_ik/)
- [ozz Foot IK Sample](http://guillaumeblanc.github.io/ozz-animation/samples/foot_ik/)
- [Foot IK and Terrain Adaptation](https://palospublishing.com/foot-ik-and-terrain-adaptation/)
- [Animation Blending: Achieving IK and More](https://www.gamedeveloper.com/programming/animation-blending-achieving-inverse-kinematics-and-more)
