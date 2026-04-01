/// @file test_facial_animation.cpp
/// @brief Unit tests for facial animation: presets, eye controller, facial animator.

#include "animation/facial_animation.h"
#include "animation/facial_presets.h"
#include "animation/eye_controller.h"
#include "animation/skeleton_animator.h"
#include "animation/morph_target.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

using namespace Vestige;

// ============================================================================
// Helpers
// ============================================================================

/// @brief Creates MorphTargetData with named targets (no vertex delta data needed).
static MorphTargetData createTestMorphData(const std::vector<std::string>& names)
{
    MorphTargetData data;
    for (const auto& name : names)
    {
        MorphTarget target;
        target.name = name;
        data.targets.push_back(target);
    }
    return data;
}

/// @brief Returns all 52 ARKit blend shape names.
static std::vector<std::string> getAllARKitNames()
{
    return {
        BlendShape::EYE_BLINK_LEFT,      BlendShape::EYE_LOOK_DOWN_LEFT,
        BlendShape::EYE_LOOK_IN_LEFT,    BlendShape::EYE_LOOK_OUT_LEFT,
        BlendShape::EYE_LOOK_UP_LEFT,    BlendShape::EYE_SQUINT_LEFT,
        BlendShape::EYE_WIDE_LEFT,       BlendShape::EYE_BLINK_RIGHT,
        BlendShape::EYE_LOOK_DOWN_RIGHT, BlendShape::EYE_LOOK_IN_RIGHT,
        BlendShape::EYE_LOOK_OUT_RIGHT,  BlendShape::EYE_LOOK_UP_RIGHT,
        BlendShape::EYE_SQUINT_RIGHT,    BlendShape::EYE_WIDE_RIGHT,
        BlendShape::JAW_FORWARD,         BlendShape::JAW_LEFT,
        BlendShape::JAW_RIGHT,           BlendShape::JAW_OPEN,
        BlendShape::MOUTH_CLOSE,         BlendShape::MOUTH_FUNNEL,
        BlendShape::MOUTH_PUCKER,        BlendShape::MOUTH_LEFT,
        BlendShape::MOUTH_RIGHT,         BlendShape::MOUTH_SMILE_LEFT,
        BlendShape::MOUTH_SMILE_RIGHT,   BlendShape::MOUTH_FROWN_LEFT,
        BlendShape::MOUTH_FROWN_RIGHT,   BlendShape::MOUTH_DIMPLE_LEFT,
        BlendShape::MOUTH_DIMPLE_RIGHT,  BlendShape::MOUTH_STRETCH_LEFT,
        BlendShape::MOUTH_STRETCH_RIGHT, BlendShape::MOUTH_ROLL_LOWER,
        BlendShape::MOUTH_ROLL_UPPER,    BlendShape::MOUTH_SHRUG_LOWER,
        BlendShape::MOUTH_SHRUG_UPPER,   BlendShape::MOUTH_PRESS_LEFT,
        BlendShape::MOUTH_PRESS_RIGHT,   BlendShape::MOUTH_LOWER_DOWN_LEFT,
        BlendShape::MOUTH_LOWER_DOWN_RIGHT, BlendShape::MOUTH_UPPER_UP_LEFT,
        BlendShape::MOUTH_UPPER_UP_RIGHT,   BlendShape::BROW_DOWN_LEFT,
        BlendShape::BROW_DOWN_RIGHT,     BlendShape::BROW_INNER_UP,
        BlendShape::BROW_OUTER_UP_LEFT,  BlendShape::BROW_OUTER_UP_RIGHT,
        BlendShape::CHEEK_PUFF,          BlendShape::CHEEK_SQUINT_LEFT,
        BlendShape::CHEEK_SQUINT_RIGHT,  BlendShape::NOSE_SNEER_LEFT,
        BlendShape::NOSE_SNEER_RIGHT,    BlendShape::TONGUE_OUT,
    };
}

/// @brief Finds the index of a shape name in the ARKit name list.
static int findIndex(const std::vector<std::string>& names, const std::string& name)
{
    for (size_t i = 0; i < names.size(); ++i)
    {
        if (names[i] == name) return static_cast<int>(i);
    }
    return -1;
}

// ============================================================================
// FacialPresets Tests
// ============================================================================

class FacialPresetsTest : public ::testing::Test {};

TEST_F(FacialPresetsTest, AllEmotionsExist)
{
    const auto& presets = FacialPresets::getAll();
    EXPECT_EQ(presets.size(), static_cast<size_t>(Emotion::COUNT));
}

TEST_F(FacialPresetsTest, NeutralHasNoEntries)
{
    const auto& neutral = FacialPresets::get(Emotion::NEUTRAL);
    EXPECT_EQ(neutral.emotion, Emotion::NEUTRAL);
    EXPECT_TRUE(neutral.entries.empty());
}

TEST_F(FacialPresetsTest, HappyHasSmileShapes)
{
    const auto& happy = FacialPresets::get(Emotion::HAPPY);
    EXPECT_EQ(happy.emotion, Emotion::HAPPY);
    EXPECT_FALSE(happy.entries.empty());

    bool hasSmileLeft = false;
    bool hasSmileRight = false;
    for (const auto& entry : happy.entries)
    {
        std::string name(entry.shapeName);
        if (name == BlendShape::MOUTH_SMILE_LEFT)  hasSmileLeft = true;
        if (name == BlendShape::MOUTH_SMILE_RIGHT) hasSmileRight = true;
    }
    EXPECT_TRUE(hasSmileLeft);
    EXPECT_TRUE(hasSmileRight);
}

TEST_F(FacialPresetsTest, SadHasFrownShapes)
{
    const auto& sad = FacialPresets::get(Emotion::SAD);
    EXPECT_EQ(sad.emotion, Emotion::SAD);

    bool hasFrown = false;
    for (const auto& entry : sad.entries)
    {
        if (std::string(entry.shapeName) == BlendShape::MOUTH_FROWN_LEFT) hasFrown = true;
    }
    EXPECT_TRUE(hasFrown);
}

TEST_F(FacialPresetsTest, AngryHasBrowDown)
{
    const auto& angry = FacialPresets::get(Emotion::ANGRY);
    bool hasBrowDown = false;
    for (const auto& entry : angry.entries)
    {
        if (std::string(entry.shapeName) == BlendShape::BROW_DOWN_LEFT) hasBrowDown = true;
    }
    EXPECT_TRUE(hasBrowDown);
}

TEST_F(FacialPresetsTest, SurprisedHasEyeWide)
{
    const auto& surprised = FacialPresets::get(Emotion::SURPRISED);
    bool hasEyeWide = false;
    for (const auto& entry : surprised.entries)
    {
        if (std::string(entry.shapeName) == BlendShape::EYE_WIDE_LEFT) hasEyeWide = true;
    }
    EXPECT_TRUE(hasEyeWide);
}

TEST_F(FacialPresetsTest, AllNonNeutralEmotionsHaveEntries)
{
    for (int i = 1; i < static_cast<int>(Emotion::COUNT); ++i)
    {
        auto emotion = static_cast<Emotion>(i);
        const auto& preset = FacialPresets::get(emotion);
        EXPECT_FALSE(preset.entries.empty()) << "Emotion " << emotionName(emotion) << " has no entries";
    }
}

TEST_F(FacialPresetsTest, AllWeightsInValidRange)
{
    for (const auto& preset : FacialPresets::getAll())
    {
        for (const auto& entry : preset.entries)
        {
            EXPECT_GE(entry.weight, 0.0f)
                << emotionName(preset.emotion) << ": " << entry.shapeName;
            EXPECT_LE(entry.weight, 1.0f)
                << emotionName(preset.emotion) << ": " << entry.shapeName;
        }
    }
}

TEST_F(FacialPresetsTest, AllShapeNamesAreValidARKit)
{
    auto arkitNames = getAllARKitNames();
    std::unordered_set<std::string> validNames(arkitNames.begin(), arkitNames.end());

    for (const auto& preset : FacialPresets::getAll())
    {
        for (const auto& entry : preset.entries)
        {
            EXPECT_TRUE(validNames.count(entry.shapeName) > 0)
                << emotionName(preset.emotion) << " uses unknown shape: " << entry.shapeName;
        }
    }
}

TEST_F(FacialPresetsTest, EmotionNameReturnsString)
{
    EXPECT_STREQ(emotionName(Emotion::NEUTRAL), "Neutral");
    EXPECT_STREQ(emotionName(Emotion::HAPPY), "Happy");
    EXPECT_STREQ(emotionName(Emotion::PAIN), "Pain");
}

TEST_F(FacialPresetsTest, ARKitHas52Constants)
{
    auto names = getAllARKitNames();
    EXPECT_EQ(names.size(), 52u);

    // All names should be unique
    std::unordered_set<std::string> unique(names.begin(), names.end());
    EXPECT_EQ(unique.size(), 52u);
}

// ============================================================================
// EyeController Tests
// ============================================================================

class EyeControllerTest : public ::testing::Test
{
protected:
    EyeController controller;

    void SetUp() override
    {
        // Disable random blink and saccade for deterministic tests
        controller.setBlinkEnabled(false);
        controller.setSaccadeEnabled(false);
    }
};

TEST_F(EyeControllerTest, InitialWeightsEmpty)
{
    controller.update(0.0f);
    EXPECT_TRUE(controller.getWeights().empty());
}

TEST_F(EyeControllerTest, TriggerBlinkProducesWeight)
{
    controller.setBlinkEnabled(true);
    controller.triggerBlink();

    // Advance partway through blink (close phase)
    controller.update(0.05f);  // ~33% of 150ms duration

    float blinkL = controller.getWeight(BlendShape::EYE_BLINK_LEFT);
    float blinkR = controller.getWeight(BlendShape::EYE_BLINK_RIGHT);
    EXPECT_GT(blinkL, 0.0f);
    EXPECT_GT(blinkR, 0.0f);
    EXPECT_FLOAT_EQ(blinkL, blinkR);  // both eyes blink together
}

TEST_F(EyeControllerTest, BlinkReachesFullClosure)
{
    controller.setBlinkEnabled(true);
    controller.triggerBlink();

    // Advance to the close point (~80ms = 0.53 * 150ms)
    controller.update(0.08f);

    float blink = controller.getWeight(BlendShape::EYE_BLINK_LEFT);
    EXPECT_NEAR(blink, 1.0f, 0.05f);  // should be near fully closed
}

TEST_F(EyeControllerTest, BlinkCompletesToZero)
{
    controller.setBlinkEnabled(true);
    controller.triggerBlink();

    // Advance past full blink duration (150ms + margin)
    controller.update(0.16f);

    float blink = controller.getWeight(BlendShape::EYE_BLINK_LEFT);
    EXPECT_FLOAT_EQ(blink, 0.0f);
}

TEST_F(EyeControllerTest, IsBlinkingDuringBlink)
{
    controller.setBlinkEnabled(true);
    EXPECT_FALSE(controller.isBlinking());

    controller.triggerBlink();
    EXPECT_TRUE(controller.isBlinking());

    controller.update(0.16f);  // complete blink
    EXPECT_FALSE(controller.isBlinking());
}

TEST_F(EyeControllerTest, GazeRightProducesCorrectShapes)
{
    // Look right: +X in head-local space
    controller.setGazeTarget(glm::vec3(1.0f, 0.0f, -1.0f));
    controller.update(1.0f);  // large dt for immediate convergence

    // Looking right: left eye looks in (toward nose), right eye looks out
    float lookInL = controller.getWeight(BlendShape::EYE_LOOK_IN_LEFT);
    float lookOutR = controller.getWeight(BlendShape::EYE_LOOK_OUT_RIGHT);
    EXPECT_GT(lookInL, 0.0f);
    EXPECT_GT(lookOutR, 0.0f);
    EXPECT_NEAR(lookInL, lookOutR, 0.01f);

    // Opposite directions should be zero
    EXPECT_FLOAT_EQ(controller.getWeight(BlendShape::EYE_LOOK_OUT_LEFT), 0.0f);
    EXPECT_FLOAT_EQ(controller.getWeight(BlendShape::EYE_LOOK_IN_RIGHT), 0.0f);
}

TEST_F(EyeControllerTest, GazeLeftProducesCorrectShapes)
{
    // Look left: -X in head-local space
    controller.setGazeTarget(glm::vec3(-1.0f, 0.0f, -1.0f));
    controller.update(1.0f);

    float lookOutL = controller.getWeight(BlendShape::EYE_LOOK_OUT_LEFT);
    float lookInR = controller.getWeight(BlendShape::EYE_LOOK_IN_RIGHT);
    EXPECT_GT(lookOutL, 0.0f);
    EXPECT_GT(lookInR, 0.0f);
}

TEST_F(EyeControllerTest, GazeUpProducesCorrectShapes)
{
    controller.setGazeTarget(glm::vec3(0.0f, 1.0f, -1.0f));
    controller.update(1.0f);

    EXPECT_GT(controller.getWeight(BlendShape::EYE_LOOK_UP_LEFT), 0.0f);
    EXPECT_GT(controller.getWeight(BlendShape::EYE_LOOK_UP_RIGHT), 0.0f);
    EXPECT_FLOAT_EQ(controller.getWeight(BlendShape::EYE_LOOK_DOWN_LEFT), 0.0f);
}

TEST_F(EyeControllerTest, GazeDownProducesCorrectShapes)
{
    controller.setGazeTarget(glm::vec3(0.0f, -1.0f, -1.0f));
    controller.update(1.0f);

    EXPECT_GT(controller.getWeight(BlendShape::EYE_LOOK_DOWN_LEFT), 0.0f);
    EXPECT_GT(controller.getWeight(BlendShape::EYE_LOOK_DOWN_RIGHT), 0.0f);
    EXPECT_FLOAT_EQ(controller.getWeight(BlendShape::EYE_LOOK_UP_LEFT), 0.0f);
}

TEST_F(EyeControllerTest, ClearGazeReturnsToCenter)
{
    controller.setGazeTarget(glm::vec3(1.0f, 0.0f, -1.0f));
    controller.update(1.0f);
    EXPECT_GT(controller.getWeight(BlendShape::EYE_LOOK_IN_LEFT), 0.0f);

    controller.clearGazeTarget();
    controller.update(1.0f);  // converge back to center
    EXPECT_NEAR(controller.getWeight(BlendShape::EYE_LOOK_IN_LEFT), 0.0f, 0.01f);
}

TEST_F(EyeControllerTest, GazeRespectsLimits)
{
    controller.setHorizontalLimit(10.0f);

    // Look far right (45 degrees) — should be clamped at 10 degrees → normalized to 1.0
    controller.setGazeTarget(glm::vec3(1.0f, 0.0f, -1.0f));
    controller.update(1.0f);

    float lookInL = controller.getWeight(BlendShape::EYE_LOOK_IN_LEFT);
    EXPECT_NEAR(lookInL, 1.0f, 0.02f);
}

// ============================================================================
// FacialAnimator Tests
// ============================================================================

class FacialAnimatorTest : public ::testing::Test
{
protected:
    SkeletonAnimator skeletonAnimator;
    FacialAnimator facialAnimator;
    std::vector<std::string> shapeNames;

    void SetUp() override
    {
        shapeNames = getAllARKitNames();
        skeletonAnimator.setMorphTargetCount(static_cast<int>(shapeNames.size()));

        facialAnimator.setAnimator(&skeletonAnimator);
        facialAnimator.mapBlendShapes(shapeNames);

        // Disable eye animation for emotion-focused tests
        facialAnimator.getEyeController().setBlinkEnabled(false);
        facialAnimator.getEyeController().setSaccadeEnabled(false);
    }

    float getWeight(const std::string& name) const
    {
        int idx = findIndex(shapeNames, name);
        if (idx < 0) return 0.0f;
        return skeletonAnimator.getMorphWeights()[static_cast<size_t>(idx)];
    }
};

TEST_F(FacialAnimatorTest, InitialStateIsNeutral)
{
    EXPECT_EQ(facialAnimator.getCurrentEmotion(), Emotion::NEUTRAL);
    EXPECT_EQ(facialAnimator.getTargetEmotion(), Emotion::NEUTRAL);
    EXPECT_FALSE(facialAnimator.isTransitioning());
}

TEST_F(FacialAnimatorTest, IsMappedAfterSetup)
{
    EXPECT_TRUE(facialAnimator.isMapped());
}

TEST_F(FacialAnimatorTest, MapBlendShapesFromMorphData)
{
    FacialAnimator animator;
    SkeletonAnimator skelAnimator;
    auto morphData = createTestMorphData({"jawOpen", "mouthSmileLeft"});

    animator.setAnimator(&skelAnimator);
    animator.mapBlendShapes(morphData);
    EXPECT_TRUE(animator.isMapped());
}

TEST_F(FacialAnimatorTest, SetEmotionImmediateAppliesWeights)
{
    facialAnimator.setEmotionImmediate(Emotion::HAPPY);
    facialAnimator.update(0.0f);

    // Happy preset has mouthSmileLeft = 0.7
    EXPECT_NEAR(getWeight(BlendShape::MOUTH_SMILE_LEFT), 0.7f, 0.001f);
    EXPECT_NEAR(getWeight(BlendShape::MOUTH_SMILE_RIGHT), 0.7f, 0.001f);
}

TEST_F(FacialAnimatorTest, NeutralClearsAllWeights)
{
    facialAnimator.setEmotionImmediate(Emotion::HAPPY);
    facialAnimator.update(0.0f);
    EXPECT_GT(getWeight(BlendShape::MOUTH_SMILE_LEFT), 0.0f);

    facialAnimator.setEmotionImmediate(Emotion::NEUTRAL);
    facialAnimator.update(0.0f);
    EXPECT_FLOAT_EQ(getWeight(BlendShape::MOUTH_SMILE_LEFT), 0.0f);
}

TEST_F(FacialAnimatorTest, EmotionTransitionBlends)
{
    facialAnimator.setEmotionImmediate(Emotion::HAPPY);
    facialAnimator.update(0.0f);

    // Start transition to SAD over 1 second
    facialAnimator.setEmotion(Emotion::SAD, 1.0f);
    EXPECT_TRUE(facialAnimator.isTransitioning());

    // At t=0.5 with smoothstep, progress is 0.5
    facialAnimator.update(0.5f);

    // mouthSmileLeft: HAPPY=0.7, SAD=0.0 → blend at t=0.5 → 0.35
    EXPECT_NEAR(getWeight(BlendShape::MOUTH_SMILE_LEFT), 0.35f, 0.05f);

    // mouthFrownLeft: HAPPY=0.0, SAD=0.6 → blend at t=0.5 → 0.3
    EXPECT_NEAR(getWeight(BlendShape::MOUTH_FROWN_LEFT), 0.3f, 0.05f);
}

TEST_F(FacialAnimatorTest, EmotionTransitionCompletes)
{
    facialAnimator.setEmotionImmediate(Emotion::HAPPY);
    facialAnimator.update(0.0f);

    facialAnimator.setEmotion(Emotion::SAD, 0.5f);
    facialAnimator.update(0.6f);  // past transition duration

    EXPECT_FALSE(facialAnimator.isTransitioning());
    EXPECT_EQ(facialAnimator.getCurrentEmotion(), Emotion::SAD);
    EXPECT_NEAR(getWeight(BlendShape::MOUTH_FROWN_LEFT), 0.6f, 0.001f);
    EXPECT_FLOAT_EQ(getWeight(BlendShape::MOUTH_SMILE_LEFT), 0.0f);
}

TEST_F(FacialAnimatorTest, EmotionIntensityScalesWeights)
{
    facialAnimator.setEmotionImmediate(Emotion::HAPPY);
    facialAnimator.setEmotionIntensity(0.5f);
    facialAnimator.update(0.0f);

    // mouthSmileLeft should be 0.7 * 0.5 = 0.35
    EXPECT_NEAR(getWeight(BlendShape::MOUTH_SMILE_LEFT), 0.35f, 0.001f);
}

TEST_F(FacialAnimatorTest, BlendEmotionsInterpolates)
{
    // Blend 50% happy + 50% sad
    facialAnimator.blendEmotions(Emotion::HAPPY, Emotion::SAD, 0.5f);
    facialAnimator.update(0.0f);

    // mouthSmileLeft: HAPPY=0.7, SAD=0.0 → 0.35
    EXPECT_NEAR(getWeight(BlendShape::MOUTH_SMILE_LEFT), 0.35f, 0.001f);
    // mouthFrownLeft: HAPPY=0.0, SAD=0.6 → 0.3
    EXPECT_NEAR(getWeight(BlendShape::MOUTH_FROWN_LEFT), 0.3f, 0.001f);
}

TEST_F(FacialAnimatorTest, EyeControllerIntegration)
{
    // Enable blink, trigger one
    facialAnimator.getEyeController().setBlinkEnabled(true);
    facialAnimator.getEyeController().triggerBlink();
    facialAnimator.update(0.05f);

    // Blink weight should be written to the animator
    float blinkW = getWeight(BlendShape::EYE_BLINK_LEFT);
    EXPECT_GT(blinkW, 0.0f);
}

TEST_F(FacialAnimatorTest, EyeWeightsAdditiveWithEmotion)
{
    // Pain preset has eyeSquintLeft = 0.7
    facialAnimator.setEmotionImmediate(Emotion::PAIN);

    // Trigger a blink (eyeBlinkLeft additive on top of pain's 0.3)
    facialAnimator.getEyeController().setBlinkEnabled(true);
    facialAnimator.getEyeController().triggerBlink();
    facialAnimator.update(0.08f);  // near full closure

    // eyeBlinkLeft from pain = 0.3, plus blink weight (~1.0) should clamp to 1.0
    float blinkW = getWeight(BlendShape::EYE_BLINK_LEFT);
    EXPECT_NEAR(blinkW, 1.0f, 0.05f);
}

TEST_F(FacialAnimatorTest, LipSyncOverridesMouthShapes)
{
    facialAnimator.setEmotionImmediate(Emotion::HAPPY);
    facialAnimator.setLipSyncWeight(BlendShape::JAW_OPEN, 0.8f);
    facialAnimator.setLipSyncAlpha(1.0f);  // full override
    facialAnimator.update(0.0f);

    // jawOpen: emotion=0.1 (happy), lip sync=0.8 with alpha=1.0 → override to 0.8
    EXPECT_NEAR(getWeight(BlendShape::JAW_OPEN), 0.8f, 0.001f);

    // Non-mouth shape should be unaffected by lip sync
    EXPECT_NEAR(getWeight(BlendShape::CHEEK_SQUINT_LEFT), 0.5f, 0.001f);
}

TEST_F(FacialAnimatorTest, LipSyncPartialAlpha)
{
    facialAnimator.setEmotionImmediate(Emotion::HAPPY);
    facialAnimator.setLipSyncWeight(BlendShape::JAW_OPEN, 0.5f);
    facialAnimator.setLipSyncAlpha(0.5f);
    facialAnimator.update(0.0f);

    // jawOpen: lerp(0.1, 0.5, 0.5) = 0.3
    EXPECT_NEAR(getWeight(BlendShape::JAW_OPEN), 0.3f, 0.001f);
}

TEST_F(FacialAnimatorTest, ClearLipSyncRestoresEmotion)
{
    facialAnimator.setEmotionImmediate(Emotion::HAPPY);
    facialAnimator.setLipSyncWeight(BlendShape::JAW_OPEN, 0.8f);
    facialAnimator.setLipSyncAlpha(1.0f);
    facialAnimator.update(0.0f);
    EXPECT_NEAR(getWeight(BlendShape::JAW_OPEN), 0.8f, 0.001f);

    facialAnimator.clearLipSync();
    facialAnimator.update(0.0f);
    EXPECT_NEAR(getWeight(BlendShape::JAW_OPEN), 0.1f, 0.001f);
}

TEST_F(FacialAnimatorTest, ClonePreservesState)
{
    facialAnimator.setEmotionImmediate(Emotion::ANGRY);
    facialAnimator.setEmotionIntensity(0.8f);

    auto cloned = facialAnimator.clone();
    auto* clonedFacial = dynamic_cast<FacialAnimator*>(cloned.get());
    ASSERT_NE(clonedFacial, nullptr);

    EXPECT_EQ(clonedFacial->getCurrentEmotion(), Emotion::ANGRY);
    EXPECT_FLOAT_EQ(clonedFacial->getEmotionIntensity(), 0.8f);
    EXPECT_TRUE(clonedFacial->isMapped());
}

TEST_F(FacialAnimatorTest, PartialShapeMapping)
{
    // Model with only 3 of the 52 ARKit shapes
    FacialAnimator partial;
    SkeletonAnimator skelAnimator;
    std::vector<std::string> partialNames = {"mouthSmileLeft", "mouthSmileRight", "jawOpen"};
    skelAnimator.setMorphTargetCount(3);
    partial.setAnimator(&skelAnimator);
    partial.mapBlendShapes(partialNames);
    partial.getEyeController().setBlinkEnabled(false);
    partial.getEyeController().setSaccadeEnabled(false);

    partial.setEmotionImmediate(Emotion::HAPPY);
    partial.update(0.0f);

    // Only the 3 available shapes should get weights
    EXPECT_NEAR(skelAnimator.getMorphWeights()[0], 0.7f, 0.001f);  // mouthSmileLeft
    EXPECT_NEAR(skelAnimator.getMorphWeights()[1], 0.7f, 0.001f);  // mouthSmileRight
    EXPECT_NEAR(skelAnimator.getMorphWeights()[2], 0.1f, 0.001f);  // jawOpen
}

TEST_F(FacialAnimatorTest, MidTransitionInterrupt)
{
    facialAnimator.setEmotionImmediate(Emotion::HAPPY);
    facialAnimator.update(0.0f);

    // Start transition to SAD
    facialAnimator.setEmotion(Emotion::SAD, 1.0f);
    facialAnimator.update(0.5f);  // halfway

    // Interrupt: switch to ANGRY
    facialAnimator.setEmotion(Emotion::ANGRY, 1.0f);
    EXPECT_TRUE(facialAnimator.isTransitioning());
    EXPECT_EQ(facialAnimator.getTargetEmotion(), Emotion::ANGRY);

    // Complete the angry transition
    facialAnimator.update(1.1f);
    EXPECT_FALSE(facialAnimator.isTransitioning());
    EXPECT_EQ(facialAnimator.getCurrentEmotion(), Emotion::ANGRY);
    EXPECT_NEAR(getWeight(BlendShape::BROW_DOWN_LEFT), 0.8f, 0.001f);
}

TEST_F(FacialAnimatorTest, NoAnimatorDoesNotCrash)
{
    FacialAnimator orphan;
    orphan.mapBlendShapes(shapeNames);
    orphan.setEmotionImmediate(Emotion::HAPPY);
    orphan.update(1.0f / 60.0f);  // should not crash
}

TEST_F(FacialAnimatorTest, WeightsClampedTo01)
{
    facialAnimator.setEmotionImmediate(Emotion::PAIN);  // eyeBlinkLeft = 0.3

    // Add blink on top (should clamp to 1.0, not exceed)
    facialAnimator.getEyeController().setBlinkEnabled(true);
    facialAnimator.getEyeController().triggerBlink();
    facialAnimator.update(0.08f);

    float blinkW = getWeight(BlendShape::EYE_BLINK_LEFT);
    EXPECT_LE(blinkW, 1.0f);
    EXPECT_GE(blinkW, 0.0f);
}
