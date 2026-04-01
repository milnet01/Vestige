/// @file facial_animation.h
/// @brief FacialAnimator component: emotion presets, eye animation, and lip sync layering.
#pragma once

#include "animation/eye_controller.h"
#include "animation/facial_presets.h"
#include "animation/morph_target.h"
#include "scene/component.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Vestige
{

class SkeletonAnimator;

/// @brief Component that drives facial blend shape animation.
///
/// Manages three additive layers:
///   1. Emotion (base): Preset expressions with smooth crossfade transitions
///   2. Eye (additive): Procedural blink, look-at gaze, and saccade noise
///   3. Lip sync (regional override): Overrides mouth/jaw shapes (populated by Batch 7)
///
/// Each frame, the layers are merged and the final weights are written to a
/// SkeletonAnimator via setMorphWeight(). Requires mapBlendShapes() to be called
/// once at setup time to resolve ARKit shape names to morph target indices.
class FacialAnimator : public Component
{
public:
    FacialAnimator();
    ~FacialAnimator() override;

    /// @brief Per-frame update: advances emotion transition, eye animation, and merges layers.
    void update(float deltaTime) override;

    /// @brief Deep copy for entity duplication (animator pointer not copied).
    std::unique_ptr<Component> clone() const override;

    // --- Setup ---

    /// @brief Sets the target SkeletonAnimator that receives morph weight output.
    void setAnimator(SkeletonAnimator* animator);

    /// @brief Builds the name-to-index mapping from the model's morph target data.
    /// Call once after loading the model.
    void mapBlendShapes(const MorphTargetData& morphData);

    /// @brief Builds the name-to-index mapping from a list of shape names.
    /// Convenience overload for testing and manual setup.
    void mapBlendShapes(const std::vector<std::string>& shapeNames);

    /// @brief Returns true if blend shapes have been mapped.
    bool isMapped() const;

    // --- Emotions ---

    /// @brief Transitions to an emotion over the given duration (seconds).
    void setEmotion(Emotion emotion, float transitionDuration = 0.5f);

    /// @brief Sets an emotion immediately with no transition.
    void setEmotionImmediate(Emotion emotion);

    /// @brief Gets the current (fully transitioned) emotion.
    Emotion getCurrentEmotion() const;

    /// @brief Gets the target emotion (may differ during transition).
    Emotion getTargetEmotion() const;

    /// @brief Returns true if an emotion transition is in progress.
    bool isTransitioning() const;

    /// @brief Sets the global emotion intensity multiplier [0, 1].
    void setEmotionIntensity(float intensity);

    /// @brief Gets the current emotion intensity multiplier.
    float getEmotionIntensity() const;

    /// @brief Manually blends between two emotions at parameter t [0, 1].
    /// Sets the result as the current emotion state (no transition).
    void blendEmotions(Emotion a, Emotion b, float t);

    // --- Eye controller ---

    /// @brief Gets the eye controller for blink/gaze configuration.
    EyeController& getEyeController();
    const EyeController& getEyeController() const;

    // --- Lip sync layer (Batch 7 integration point) ---

    /// @brief Sets a lip sync blend shape weight.
    void setLipSyncWeight(const std::string& shapeName, float weight);

    /// @brief Clears all lip sync weights.
    void clearLipSync();

    /// @brief Sets the lip sync override alpha [0, 1]. Default 0.8.
    /// Controls how much lip sync overrides the emotion layer for mouth shapes.
    void setLipSyncAlpha(float alpha);

private:
    /// @brief Resolves a preset's sparse entries into a per-index weight array.
    void resolvePresetWeights(Emotion emotion, std::vector<float>& outWeights) const;

    /// @brief Merges emotion + eye + lip sync layers and writes to the animator.
    void mergeAndApply();

    /// @brief Resolves a shape name to a morph target index (-1 if not mapped).
    int resolveIndex(const std::string& shapeName) const;

    /// @brief Returns true if the shape name is a mouth/jaw/tongue region shape.
    static bool isMouthShape(const std::string& name);

    SkeletonAnimator* m_animator = nullptr;
    std::unordered_map<std::string, int> m_shapeNameToIndex;
    int m_totalTargets = 0;

    // Emotion blending
    Emotion m_currentEmotion = Emotion::NEUTRAL;
    Emotion m_targetEmotion = Emotion::NEUTRAL;
    float m_transitionTime = 0.0f;
    float m_transitionDuration = 0.0f;
    float m_intensity = 1.0f;
    std::vector<float> m_sourceWeights;
    std::vector<float> m_targetWeights;

    // Eye controller
    EyeController m_eyeController;

    // Lip sync
    std::unordered_map<std::string, float> m_lipSyncWeights;
    float m_lipSyncAlpha = 0.8f;
};

} // namespace Vestige
