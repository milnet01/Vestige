// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_motion_vectors_mrt.cpp
/// @brief Parity tests for the Slice R1 motion-vector math (CPU spec in
///        engine/renderer/motion_vector_math.h), GL-free.
///
/// Pins the geometry-pass motion output and the motion-combine selector against
/// the documented math, and proves the deliberate R1 invariants:
///   - static object under static camera → zero motion;
///   - static object under camera motion → equals pure camera reprojection
///     (so the combine's object-motion and camera-fallback branches agree on a
///      shared static surface — no seam);
///   - the coverage flag (not a magnitude sentinel) selects the combine branch;
///   - missing prev-matrix (prev == current) yields no spurious object motion;
///   - skinned/morph meshes use the BASE position → rigid-body parity with the
///     deleted overlay (R2 is what will add animated-pose motion).
#include "renderer/motion_vector_math.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>

#include <gtest/gtest.h>

using namespace Vestige;

namespace
{
constexpr float kTol = 1e-5f;

// Independent oracle: the overlay's documented math, re-implemented step-by-step
// (perspective divide → NDC → UV → difference) so it is not the same code path as
// computeMotionVectorUV even though it encodes the same formula.
glm::vec2 overlayOracle(const glm::mat4& model, const glm::mat4& prevModel,
                        const glm::mat4& vp, const glm::mat4& prevVp,
                        const glm::vec3& objPos)
{
    auto toUV = [](const glm::vec4& clip) -> glm::vec2
    {
        glm::vec2 ndc(0.0f);
        if (std::abs(clip.w) > 1e-6f)
        {
            ndc = glm::vec2(clip.x, clip.y) / clip.w;
        }
        return ndc * 0.5f + 0.5f;
    };
    const glm::vec4 cur = vp * model * glm::vec4(objPos, 1.0f);
    const glm::vec4 prev = prevVp * prevModel * glm::vec4(objPos, 1.0f);
    return toUV(cur) - toUV(prev);
}

glm::mat4 testProjection()
{
    return glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
}
}  // namespace

// Test 1 — the CPU mirror reproduces the overlay's currUV-prevUV over a battery
// of transforms (incl. the 1e-6 safeClipDivide guard).
TEST(MotionVectorMath, MotionUVMatchesOverlayMath)
{
    const glm::mat4 proj = testProjection();
    const glm::mat4 view = glm::lookAt(glm::vec3(0, 2, 5), glm::vec3(0), glm::vec3(0, 1, 0));
    const glm::mat4 vp = proj * view;
    const glm::mat4 prevView = glm::lookAt(glm::vec3(0.3f, 2, 5), glm::vec3(0.1f, 0, 0), glm::vec3(0, 1, 0));
    const glm::mat4 prevVp = proj * prevView;

    const std::array<glm::mat4, 3> models = {
        glm::translate(glm::mat4(1.0f), glm::vec3(1, 0, -2)),
        glm::rotate(glm::mat4(1.0f), glm::radians(30.0f), glm::vec3(0, 1, 0)),
        glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(-1, 1, 0)), glm::vec3(2.0f)),
    };
    const std::array<glm::vec3, 4> points = {
        glm::vec3(0, 0, 0), glm::vec3(0.5f, -0.5f, 0.25f),
        glm::vec3(-1, 2, -1), glm::vec3(3, 0, -4),
    };

    for (const glm::mat4& model : models)
    {
        const glm::mat4 prevModel = glm::translate(model, glm::vec3(0.05f, 0.0f, 0.02f));
        for (const glm::vec3& p : points)
        {
            const glm::vec2 got = computeMotionVectorUV(model, prevModel, vp, prevVp, p);
            const glm::vec2 want = overlayOracle(model, prevModel, vp, prevVp, p);
            EXPECT_NEAR(got.x, want.x, kTol);
            EXPECT_NEAR(got.y, want.y, kTol);
        }
    }
}

// Test 2 — static object, static camera → exactly zero motion.
TEST(MotionVectorMath, StaticObjectMotionIsZeroUnderStaticCamera)
{
    const glm::mat4 vp = testProjection() *
        glm::lookAt(glm::vec3(0, 1, 4), glm::vec3(0), glm::vec3(0, 1, 0));
    const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0, -1));

    const glm::vec2 motion = computeMotionVectorUV(model, model, vp, vp, glm::vec3(0.2f, 0.1f, 0));
    EXPECT_NEAR(motion.x, 0.0f, kTol);
    EXPECT_NEAR(motion.y, 0.0f, kTol);
}

// Test 3 — static object under camera motion equals the pure camera reprojection
// of the same world point. Proves the combine's two branches agree on static
// geometry (no seam between object motion and the camera fallback).
TEST(MotionVectorMath, StaticObjectUnderCameraMotionEqualsCameraReprojection)
{
    const glm::mat4 proj = testProjection();
    const glm::mat4 vp = proj * glm::lookAt(glm::vec3(0, 2, 6), glm::vec3(0), glm::vec3(0, 1, 0));
    const glm::mat4 prevVp = proj * glm::lookAt(glm::vec3(1, 2, 6), glm::vec3(0.2f, 0, 0), glm::vec3(0, 1, 0));
    const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(-0.5f, 0.5f, -2));
    const glm::vec3 objPos(0.3f, -0.2f, 0.1f);

    // Object-motion path (model == prevModel → static object).
    const glm::vec2 objectMotion = computeMotionVectorUV(model, model, vp, prevVp, objPos);

    // Independent camera reprojection of the SAME world point P = model * objPos.
    const glm::vec4 worldP = model * glm::vec4(objPos, 1.0f);
    auto toUV = [](const glm::vec4& clip) { return glm::vec2(clip) / clip.w * 0.5f + 0.5f; };
    const glm::vec2 cameraMotion = toUV(vp * worldP) - toUV(prevVp * worldP);

    EXPECT_NEAR(objectMotion.x, cameraMotion.x, kTol);
    EXPECT_NEAR(objectMotion.y, cameraMotion.y, kTol);
}

// Test 4 — the coverage flag (.b > 0.5), not a magnitude sentinel, selects the
// combine branch. Pins §4.4 (robust to arbitrarily large legitimate motion).
TEST(MotionVectorMath, CoverageFlagSelectsBranch)
{
    const glm::vec2 sceneMotion(0.3f, -0.2f);
    const glm::vec2 cameraMotion(-0.05f, 0.05f);

    // b = 1 → object motion wins (even with non-far depth).
    EXPECT_EQ(combineMotion(1.0f, sceneMotion, 0.9f, cameraMotion), sceneMotion);

    // b = 0, depth at far plane (reverse-Z sky) → zero.
    EXPECT_EQ(combineMotion(0.0f, sceneMotion, 0.0f, cameraMotion), glm::vec2(0.0f));

    // b = 0, depth in front of far → camera fallback.
    EXPECT_EQ(combineMotion(0.0f, sceneMotion, 0.5f, cameraMotion), cameraMotion);

    // A large legitimate object motion (fast rotation near the near plane) is still
    // selected by the flag — a magnitude sentinel like "> -1.5" would have misfired.
    const glm::vec2 bigMotion(1.7f, -1.9f);
    EXPECT_EQ(combineMotion(1.0f, bigMotion, 0.8f, cameraMotion), bigMotion);
}

// Test 5 — cloth / transparent passes leave coverage at 0, so those pixels resolve
// to the camera fallback regardless of whatever stale .rg is present (guards the
// H1/H2/M-B coverage fix). The combine must never return scene.rg when b == 0.
TEST(MotionVectorMath, UncoveredPixelsFallBackToCameraMotion)
{
    const glm::vec2 staleSceneMotion(0.42f, 0.42f);  // garbage from a non-opaque pass
    const glm::vec2 cameraMotion(0.01f, -0.02f);

    const glm::vec2 result = combineMotion(0.0f, staleSceneMotion, 0.5f, cameraMotion);
    EXPECT_EQ(result, cameraMotion);
    EXPECT_NE(result, staleSceneMotion);
}

// Test 6 — an entity absent from the prev-world cache uses prev == current, which
// yields no spurious object motion (matches the overlay's first-frame fallback):
// zero under a static camera, pure camera reprojection under a moving camera.
TEST(MotionVectorMath, MissingPrevMatrixYieldsNoObjectMotion)
{
    const glm::mat4 proj = testProjection();
    const glm::mat4 vp = proj * glm::lookAt(glm::vec3(0, 1, 5), glm::vec3(0), glm::vec3(0, 1, 0));
    const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -1));
    const glm::vec3 p(0.1f, 0.2f, 0.0f);

    // prev == current (cache miss), static camera → zero.
    const glm::vec2 staticMotion = computeMotionVectorUV(model, model, vp, vp, p);
    EXPECT_NEAR(glm::length(staticMotion), 0.0f, kTol);

    // prev == current, moving camera → equals camera reprojection (no object delta).
    const glm::mat4 prevVp = proj * glm::lookAt(glm::vec3(0.4f, 1, 5), glm::vec3(0), glm::vec3(0, 1, 0));
    const glm::vec2 movingMotion = computeMotionVectorUV(model, model, vp, prevVp, p);
    const glm::vec4 worldP = model * glm::vec4(p, 1.0f);
    auto toUV = [](const glm::vec4& clip) { return glm::vec2(clip) / clip.w * 0.5f + 0.5f; };
    const glm::vec2 cameraMotion = toUV(vp * worldP) - toUV(prevVp * worldP);
    EXPECT_NEAR(movingMotion.x, cameraMotion.x, kTol);
    EXPECT_NEAR(movingMotion.y, cameraMotion.y, kTol);
}

// Test 3 (R2, replaces R1 #7) — skinned meshes now use the ANIMATED pose for motion.
// A vertex skinned with differing prev/current bone palettes yields motion =
// proj(currentSkinned) − proj(prevSkinned), which is non-zero; and when the two
// palettes are equal (static pose) it reduces EXACTLY to the rigid-body value at that
// posed position (computeMotionVectorUV evaluated there). Pins the §9.6 behaviour change.
TEST(MotionVectorMath, SkinnedMotionUsesAnimatedPose)
{
    const glm::mat4 proj = testProjection();
    const glm::mat4 vp = proj * glm::lookAt(glm::vec3(0, 1, 5), glm::vec3(0), glm::vec3(0, 1, 0));
    const glm::mat4 prevVp = vp;  // static camera — isolate the pose contribution
    const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -2));
    const glm::mat4 prevModel = model;

    const glm::vec3 basePos(0.5f, 1.0f, 0.0f);
    const glm::ivec4 boneIds(0, 1, 2, 3);
    const glm::vec4 boneWeights(1.0f, 0.0f, 0.0f, 0.0f);  // fully bound to joint 0
    const std::vector<float> noMorphW;
    const std::vector<glm::vec3> noMorphD;

    // Current palette translates joint 0; previous palette is identity → pose moved.
    std::array<glm::mat4, 4> curPalette = {glm::translate(glm::mat4(1.0f), glm::vec3(0.4f, -0.3f, 0.1f)),
                                           glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f)};
    std::array<glm::mat4, 4> prevPalette = {glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f)};

    const glm::vec3 curSkin = morphAndSkinPosition(basePos, noMorphW, noMorphD,
                                                   curPalette.data(), boneIds, boneWeights, true);
    const glm::vec3 prevSkin = morphAndSkinPosition(basePos, noMorphW, noMorphD,
                                                    prevPalette.data(), boneIds, boneWeights, true);

    const glm::vec2 animated = computeAnimatedMotionVectorUV(model, prevModel, vp, prevVp, curSkin, prevSkin);
    // Pose changed under a static camera ⇒ non-zero motion (R1 would have given zero here).
    EXPECT_GT(glm::length(animated), 1e-3f);

    // Static pose (equal palettes) reduces exactly to the rigid-body value at the posed point.
    const glm::vec3 sameSkin = curSkin;
    const glm::vec2 staticPose = computeAnimatedMotionVectorUV(model, prevModel, vp, prevVp, sameSkin, sameSkin);
    const glm::vec2 rigidAtPosed = computeMotionVectorUV(model, prevModel, vp, prevVp, sameSkin);
    EXPECT_NEAR(staticPose.x, rigidAtPosed.x, kTol);
    EXPECT_NEAR(staticPose.y, rigidAtPosed.y, kTol);
}

// Test 4 (R2) — equal palettes, model == prevModel, vp == prevVp ⇒ zero motion.
TEST(MotionVectorMath, StaticPoseStaticCameraZeroMotion)
{
    const glm::mat4 proj = testProjection();
    const glm::mat4 vp = proj * glm::lookAt(glm::vec3(0, 1, 5), glm::vec3(0), glm::vec3(0, 1, 0));
    const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -2));

    const glm::vec3 basePos(0.5f, 1.0f, 0.0f);
    const glm::ivec4 boneIds(0, 1, 2, 3);
    const glm::vec4 boneWeights(0.6f, 0.4f, 0.0f, 0.0f);
    std::array<glm::mat4, 4> palette = {glm::translate(glm::mat4(1.0f), glm::vec3(0.2f, 0.1f, 0.0f)),
                                        glm::rotate(glm::mat4(1.0f), 0.5f, glm::vec3(0, 1, 0)),
                                        glm::mat4(1.0f), glm::mat4(1.0f)};
    const std::vector<float> noMorphW;
    const std::vector<glm::vec3> noMorphD;

    const glm::vec3 skin = morphAndSkinPosition(basePos, noMorphW, noMorphD,
                                                palette.data(), boneIds, boneWeights, true);
    const glm::vec2 motion = computeAnimatedMotionVectorUV(model, model, vp, vp, skin, skin);
    EXPECT_NEAR(motion.x, 0.0f, kTol);
    EXPECT_NEAR(motion.y, 0.0f, kTol);
}

// Test 5 (R2) — morph-only vertex: prevWeights ≠ weights ⇒ motion equals the projected
// delta-driven displacement, independent of bone state (no bones here).
TEST(MotionVectorMath, MorphMotionUsesPrevWeights)
{
    const glm::mat4 proj = testProjection();
    const glm::mat4 vp = proj * glm::lookAt(glm::vec3(0, 1, 5), glm::vec3(0), glm::vec3(0, 1, 0));
    const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -2));

    const glm::vec3 basePos(0.5f, 1.0f, 0.0f);
    const std::vector<glm::vec3> morphDeltas = {glm::vec3(0.3f, 0.0f, 0.0f)};  // one target
    const std::vector<float> curW = {1.0f};   // fully applied this frame
    const std::vector<float> prevW = {0.0f};  // not applied last frame
    const glm::ivec4 boneIds(0);
    const glm::vec4 boneWeights(0.0f);

    const glm::vec3 curPos = morphAndSkinPosition(basePos, curW, morphDeltas,
                                                  nullptr, boneIds, boneWeights, false);
    const glm::vec3 prevPos = morphAndSkinPosition(basePos, prevW, morphDeltas,
                                                   nullptr, boneIds, boneWeights, false);

    const glm::vec2 motion = computeAnimatedMotionVectorUV(model, model, vp, vp, curPos, prevPos);
    // Independent oracle: the morph displaced the vertex by curW*delta this frame only.
    const glm::vec2 expected =
        computeAnimatedMotionVectorUV(model, model, vp, vp,
                                      basePos + glm::vec3(0.3f, 0, 0), basePos);
    EXPECT_NEAR(motion.x, expected.x, kTol);
    EXPECT_NEAR(motion.y, expected.y, kTol);
    EXPECT_GT(glm::length(motion), 1e-3f);
}

// Test 7 (R2) — V_mask = α(1 − n·n'): ndot=1 ⇒ 0; ndot=0 ⇒ clamp(α,0,1); monotonic in
// (1−ndot); and feedback*(1−vMask) is monotonically non-increasing as divergence grows.
TEST(MotionVectorMath, VMaskZeroWhenNormalsAgreeFullWhenOpposed)
{
    const glm::vec3 n(0.0f, 0.0f, 1.0f);
    // Normals agree exactly ⇒ no rejection.
    EXPECT_NEAR(computeDisocclusionVMask(1.0f, n, n), 0.0f, kTol);
    // Orthogonal ⇒ ndot=0 ⇒ vMask = clamp(α,0,1).
    const glm::vec3 ortho(1.0f, 0.0f, 0.0f);
    EXPECT_NEAR(computeDisocclusionVMask(1.0f, n, ortho), 1.0f, kTol);
    EXPECT_NEAR(computeDisocclusionVMask(0.5f, n, ortho), 0.5f, kTol);

    // Monotonic in divergence: rotate the previous normal progressively away from n.
    float prevMask = -1.0f;
    float prevFeedback = 1e9f;
    const float feedback0 = 0.9f;
    for (int deg = 0; deg <= 90; deg += 15)
    {
        const float a = glm::radians(static_cast<float>(deg));
        const glm::vec3 nPrev(std::sin(a), 0.0f, std::cos(a));
        const float mask = computeDisocclusionVMask(1.0f, n, nPrev);
        EXPECT_GE(mask, prevMask - kTol);                 // V_mask non-decreasing
        const float feedback = feedback0 * (1.0f - mask);
        EXPECT_LE(feedback, prevFeedback + kTol);          // feedback non-increasing
        prevMask = mask;
        prevFeedback = feedback;
    }
}

// Test 8 (R2) — zero-length nCur sentinel (cloth/terrain/sky/transparent) ⇒ vMask=0,
// so those pixels keep R1/today's feedback unchanged regardless of nPrev.
TEST(MotionVectorMath, VMaskDisabledWhereNoNormal)
{
    const glm::vec3 sentinel(0.0f);                 // cleared attachment value
    const glm::vec3 anyPrev(1.0f, 0.0f, 0.0f);
    EXPECT_NEAR(computeDisocclusionVMask(1.0f, sentinel, anyPrev), 0.0f, kTol);
    EXPECT_NEAR(computeDisocclusionVMask(1.0f, sentinel, glm::vec3(0.0f)), 0.0f, kTol);
}
