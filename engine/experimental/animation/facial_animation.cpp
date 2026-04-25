// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file facial_animation.cpp
/// @brief FacialAnimator implementation: emotion transitions, layer merging.

#include "experimental/animation/facial_animation.h"
#include "animation/skeleton_animator.h"
#include "core/logger.h"

#include <algorithm>

namespace Vestige
{

FacialAnimator::FacialAnimator() = default;
FacialAnimator::~FacialAnimator() = default;

void FacialAnimator::update(float deltaTime)
{
    if (!m_animator || m_totalTargets == 0)
    {
        return;
    }

    // 1. Advance emotion transition
    if (m_currentEmotion != m_targetEmotion)
    {
        m_transitionTime += deltaTime;
        if (m_transitionTime >= m_transitionDuration)
        {
            m_transitionTime = m_transitionDuration;
            m_currentEmotion = m_targetEmotion;
            m_sourceWeights = m_targetWeights;
        }
    }

    // 2. Update eye controller
    m_eyeController.update(deltaTime);

    // 3. Merge all layers and write to animator
    mergeAndApply();
}

std::unique_ptr<Component> FacialAnimator::clone() const
{
    auto cloned = std::make_unique<FacialAnimator>();
    cloned->m_shapeNameToIndex = m_shapeNameToIndex;
    cloned->m_totalTargets = m_totalTargets;
    cloned->m_currentEmotion = m_currentEmotion;
    cloned->m_targetEmotion = m_targetEmotion;
    cloned->m_transitionTime = 0.0f;
    cloned->m_transitionDuration = 0.0f;
    cloned->m_intensity = m_intensity;
    cloned->m_sourceWeights = m_sourceWeights;
    cloned->m_targetWeights = m_targetWeights;
    cloned->m_eyeController = m_eyeController;
    cloned->m_lipSyncAlpha = m_lipSyncAlpha;
    // m_animator intentionally not copied — must be set for the new entity.
    return cloned;
}

// --- Setup ---

void FacialAnimator::setAnimator(SkeletonAnimator* animator)
{
    m_animator = animator;
}

void FacialAnimator::mapBlendShapes(const MorphTargetData& morphData)
{
    std::vector<std::string> names;
    names.reserve(morphData.targets.size());
    for (const auto& target : morphData.targets)
    {
        names.push_back(target.name);
    }
    mapBlendShapes(names);
}

void FacialAnimator::mapBlendShapes(const std::vector<std::string>& shapeNames)
{
    m_shapeNameToIndex.clear();
    for (size_t i = 0; i < shapeNames.size(); ++i)
    {
        if (!shapeNames[i].empty())
        {
            m_shapeNameToIndex[shapeNames[i]] = static_cast<int>(i);
        }
    }
    m_totalTargets = static_cast<int>(shapeNames.size());

    auto targetCount = static_cast<size_t>(m_totalTargets);
    m_sourceWeights.assign(targetCount, 0.0f);
    m_targetWeights.assign(targetCount, 0.0f);

    if (m_animator)
    {
        m_animator->setMorphTargetCount(m_totalTargets);
    }

    Logger::info("FacialAnimator: mapped " + std::to_string(m_shapeNameToIndex.size()) +
                 " blend shapes from " + std::to_string(m_totalTargets) + " morph targets");
}

bool FacialAnimator::isMapped() const
{
    return m_totalTargets > 0;
}

// --- Emotions ---

void FacialAnimator::setEmotion(Emotion emotion, float transitionDuration)
{
    if (emotion == m_targetEmotion)
    {
        return;
    }

    // Snapshot current blended state as the transition source
    if (m_transitionDuration > 0.0f && m_currentEmotion != m_targetEmotion)
    {
        // Mid-transition: compute current blended weights
        float t = glm::smoothstep(0.0f, 1.0f, m_transitionTime / m_transitionDuration);
        for (size_t i = 0; i < static_cast<size_t>(m_totalTargets); ++i)
        {
            m_sourceWeights[i] = glm::mix(m_sourceWeights[i], m_targetWeights[i], t);
        }
    }
    // Otherwise m_sourceWeights already represents the current emotion

    m_targetEmotion = emotion;
    m_transitionTime = 0.0f;
    m_transitionDuration = transitionDuration;
    resolvePresetWeights(emotion, m_targetWeights);
}

void FacialAnimator::setEmotionImmediate(Emotion emotion)
{
    m_currentEmotion = emotion;
    m_targetEmotion = emotion;
    m_transitionTime = 0.0f;
    m_transitionDuration = 0.0f;
    resolvePresetWeights(emotion, m_sourceWeights);
    m_targetWeights = m_sourceWeights;
}

Emotion FacialAnimator::getCurrentEmotion() const
{
    return m_currentEmotion;
}

Emotion FacialAnimator::getTargetEmotion() const
{
    return m_targetEmotion;
}

bool FacialAnimator::isTransitioning() const
{
    return m_currentEmotion != m_targetEmotion;
}

void FacialAnimator::setEmotionIntensity(float intensity)
{
    m_intensity = glm::clamp(intensity, 0.0f, 1.0f);
}

float FacialAnimator::getEmotionIntensity() const
{
    return m_intensity;
}

void FacialAnimator::blendEmotions(Emotion a, Emotion b, float t)
{
    std::vector<float> weightsA(static_cast<size_t>(m_totalTargets), 0.0f);
    std::vector<float> weightsB(static_cast<size_t>(m_totalTargets), 0.0f);
    resolvePresetWeights(a, weightsA);
    resolvePresetWeights(b, weightsB);

    t = glm::clamp(t, 0.0f, 1.0f);
    for (size_t i = 0; i < static_cast<size_t>(m_totalTargets); ++i)
    {
        m_sourceWeights[i] = glm::mix(weightsA[i], weightsB[i], t);
    }
    m_targetWeights = m_sourceWeights;
    m_currentEmotion = (t < 0.5f) ? a : b;
    m_targetEmotion = m_currentEmotion;
    m_transitionTime = 0.0f;
    m_transitionDuration = 0.0f;
}

// --- Eye controller ---

EyeController& FacialAnimator::getEyeController()
{
    return m_eyeController;
}

const EyeController& FacialAnimator::getEyeController() const
{
    return m_eyeController;
}

// --- Lip sync ---

void FacialAnimator::setLipSyncWeight(const std::string& shapeName, float weight)
{
    m_lipSyncWeights[shapeName] = weight;
}

void FacialAnimator::clearLipSync()
{
    m_lipSyncWeights.clear();
}

void FacialAnimator::setLipSyncAlpha(float alpha)
{
    m_lipSyncAlpha = glm::clamp(alpha, 0.0f, 1.0f);
}

// --- Private ---

void FacialAnimator::resolvePresetWeights(Emotion emotion, std::vector<float>& outWeights) const
{
    std::fill(outWeights.begin(), outWeights.end(), 0.0f);

    const auto& preset = FacialPresets::get(emotion);
    for (const auto& entry : preset.entries)
    {
        int idx = resolveIndex(entry.shapeName);
        if (idx >= 0)
        {
            outWeights[static_cast<size_t>(idx)] = entry.weight;
        }
    }
}

void FacialAnimator::mergeAndApply()
{
    // Compute emotion transition progress
    float t = 1.0f;
    if (m_transitionDuration > 0.0f && m_currentEmotion != m_targetEmotion)
    {
        t = glm::smoothstep(0.0f, 1.0f, m_transitionTime / m_transitionDuration);
    }

    const auto& eyeWeights = m_eyeController.getWeights();
    bool hasLipSync = !m_lipSyncWeights.empty();

    for (const auto& [shapeName, shapeIndex] : m_shapeNameToIndex)
    {
        auto idx = static_cast<size_t>(shapeIndex);

        // Emotion layer: blend between source and target
        float emotionW = glm::mix(m_sourceWeights[idx], m_targetWeights[idx], t);
        emotionW *= m_intensity;

        // Eye layer: additive
        float eyeW = 0.0f;
        auto eyeIt = eyeWeights.find(shapeName);
        if (eyeIt != eyeWeights.end())
        {
            eyeW = eyeIt->second;
        }

        // Lip sync layer: regional override for mouth shapes
        float finalW;
        if (hasLipSync && isMouthShape(shapeName))
        {
            float lipW = 0.0f;
            auto lipIt = m_lipSyncWeights.find(shapeName);
            if (lipIt != m_lipSyncWeights.end())
            {
                lipW = lipIt->second;
            }
            finalW = glm::mix(emotionW, lipW, m_lipSyncAlpha) + eyeW;
        }
        else
        {
            finalW = emotionW + eyeW;
        }

        finalW = glm::clamp(finalW, 0.0f, 1.0f);
        m_animator->setMorphWeight(shapeIndex, finalW);
    }
}

int FacialAnimator::resolveIndex(const std::string& shapeName) const
{
    auto it = m_shapeNameToIndex.find(shapeName);
    return (it != m_shapeNameToIndex.end()) ? it->second : -1;
}

bool FacialAnimator::isMouthShape(const std::string& name)
{
    // Mouth/jaw/tongue shapes are the lip sync region
    return name.compare(0, 5, "mouth") == 0 ||
           name.compare(0, 3, "jaw") == 0 ||
           name.compare(0, 6, "tongue") == 0;
}

} // namespace Vestige
