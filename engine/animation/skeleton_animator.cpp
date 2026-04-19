// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file skeleton_animator.cpp
/// @brief SkeletonAnimator component implementation.
#include "animation/skeleton_animator.h"
#include "animation/animation_sampler.h"

#include <cmath>

namespace Vestige
{

SkeletonAnimator::SkeletonAnimator() = default;
SkeletonAnimator::~SkeletonAnimator() = default;

// ---------------------------------------------------------------------------
// advanceAndSample — advance a clip's playback time and sample all channels
// ---------------------------------------------------------------------------
void SkeletonAnimator::advanceAndSample(
    int clipIndex, float& time, float deltaTime,
    std::vector<glm::vec3>& translations,
    std::vector<glm::quat>& rotations,
    std::vector<glm::vec3>& scales)
{
    if (clipIndex < 0 || clipIndex >= static_cast<int>(m_clips.size()))
    {
        return;  // Frozen pose — buffers unchanged
    }

    const auto& clip = m_clips[static_cast<size_t>(clipIndex)];
    if (!clip || clip->getDuration() <= 0.0f)
    {
        return;
    }

    // Advance time
    time += deltaTime * m_speed;

    if (m_looping)
    {
        time = std::fmod(time, clip->getDuration());
        if (time < 0.0f)
        {
            time += clip->getDuration();
        }
    }
    else if (time >= clip->getDuration())
    {
        time = clip->getDuration();
    }

    // Sample all channels into the provided buffers
    for (const auto& channel : clip->m_channels)
    {
        int ji = channel.jointIndex;

        // WEIGHTS channels have jointIndex = -1 (they target the mesh, not a joint).
        // For all other channels, validate the joint index.
        if (channel.targetPath != AnimTargetPath::WEIGHTS)
        {
            if (ji < 0 || ji >= m_skeleton->getJointCount())
            {
                continue;
            }
        }

        size_t idx = (ji >= 0) ? static_cast<size_t>(ji) : 0;

        switch (channel.targetPath)
        {
        case AnimTargetPath::TRANSLATION:
            translations[idx] = sampleVec3(channel, time);
            break;
        case AnimTargetPath::ROTATION:
            rotations[idx] = sampleQuat(channel, time);
            break;
        case AnimTargetPath::SCALE:
            scales[idx] = sampleVec3(channel, time);
            break;
        case AnimTargetPath::WEIGHTS:
        {
            // Morph target weight animation: values are packed as N floats per keyframe
            // where N = number of morph targets. The jointIndex is -1 for WEIGHTS channels.
            if (channel.timestamps.empty() || channel.values.empty())
            {
                break;
            }
            size_t keyCount = channel.timestamps.size();
            size_t totalValues = channel.values.size();
            int morphCount = static_cast<int>(totalValues / keyCount);
            if (morphCount <= 0)
            {
                break;
            }

            // Resize morph weights if needed
            if (m_morphWeights.size() < static_cast<size_t>(morphCount))
            {
                m_morphWeights.resize(static_cast<size_t>(morphCount), 0.0f);
            }

            // Find keyframe pair for interpolation
            size_t k = 0;
            for (size_t t = 0; t + 1 < keyCount; ++t)
            {
                if (time < channel.timestamps[t + 1])
                {
                    k = t;
                    break;
                }
                k = t;
            }
            size_t k1 = std::min(k + 1, keyCount - 1);

            if (channel.interpolation == AnimInterpolation::STEP || k == k1)
            {
                for (int m = 0; m < morphCount; ++m)
                {
                    m_morphWeights[static_cast<size_t>(m)] =
                        channel.values[k * static_cast<size_t>(morphCount) + static_cast<size_t>(m)];
                }
            }
            else
            {
                float t0 = channel.timestamps[k];
                float t1 = channel.timestamps[k1];
                float alpha = (t1 > t0) ? (time - t0) / (t1 - t0) : 0.0f;
                alpha = glm::clamp(alpha, 0.0f, 1.0f);

                for (int m = 0; m < morphCount; ++m)
                {
                    float v0 = channel.values[k * static_cast<size_t>(morphCount) + static_cast<size_t>(m)];
                    float v1 = channel.values[k1 * static_cast<size_t>(morphCount) + static_cast<size_t>(m)];
                    m_morphWeights[static_cast<size_t>(m)] = glm::mix(v0, v1, alpha);
                }
            }
            break;
        }
        }
    }
}

// ---------------------------------------------------------------------------
// update — main per-frame update
// ---------------------------------------------------------------------------
void SkeletonAnimator::update(float deltaTime)
{
    if (!m_playing || m_paused || m_activeClipIndex < 0 || !m_skeleton)
    {
        // Clear root motion delta when not playing
        m_rootMotionDeltaPos = glm::vec3(0.0f);
        m_rootMotionDeltaRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    if (m_crossfading)
    {
        // Advance crossfade timer
        m_crossfadeTime += deltaTime;
        float blendFactor = glm::clamp(
            m_crossfadeTime / m_crossfadeDuration, 0.0f, 1.0f);

        // Sample source (outgoing) clip into source buffers
        // If m_sourceClipIndex == -1, buffers hold a frozen pose — skip sampling
        advanceAndSample(m_sourceClipIndex, m_sourceTime, deltaTime,
                         m_sourceTranslations, m_sourceRotations, m_sourceScales);

        // Sample target (incoming) clip into primary buffers
        advanceAndSample(m_activeClipIndex, m_currentTime, deltaTime,
                         m_localTranslations, m_localRotations, m_localScales);

        // Blend per-bone: source → target
        int jointCount = m_skeleton->getJointCount();
        for (int j = 0; j < jointCount; ++j)
        {
            size_t idx = static_cast<size_t>(j);
            m_localTranslations[idx] = glm::mix(
                m_sourceTranslations[idx], m_localTranslations[idx], blendFactor);
            m_localRotations[idx] = glm::slerp(
                m_sourceRotations[idx], m_localRotations[idx], blendFactor);
            m_localScales[idx] = glm::mix(
                m_sourceScales[idx], m_localScales[idx], blendFactor);
        }

        // Crossfade complete?
        if (blendFactor >= 1.0f)
        {
            m_crossfading = false;
            m_sourceClipIndex = -1;
        }
    }
    else
    {
        // Single clip playback
        advanceAndSample(m_activeClipIndex, m_currentTime, deltaTime,
                         m_localTranslations, m_localRotations, m_localScales);

        // Check if non-looping clip finished
        if (!m_looping && m_activeClipIndex >= 0)
        {
            const auto& clip = m_clips[static_cast<size_t>(m_activeClipIndex)];
            if (clip && m_currentTime >= clip->getDuration())
            {
                m_playing = false;
            }
        }
    }

    // Root motion extraction (after sampling/blending, before bone matrix computation)
    extractRootMotion();

    computeBoneMatrices();
}

// ---------------------------------------------------------------------------
// extractRootMotion
// ---------------------------------------------------------------------------
void SkeletonAnimator::extractRootMotion()
{
    m_rootMotionDeltaPos = glm::vec3(0.0f);
    m_rootMotionDeltaRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    if (m_rootMotionMode == RootMotionMode::IGNORE)
    {
        return;
    }

    int ri = m_rootMotionBone;
    if (ri < 0 || ri >= m_skeleton->getJointCount())
    {
        return;
    }
    size_t ridx = static_cast<size_t>(ri);

    if (!m_rootMotionInitialized)
    {
        // First frame: just capture current as baseline, no delta
        m_prevRootPos = m_localTranslations[ridx];
        m_prevRootRot = m_localRotations[ridx];
        m_rootMotionInitialized = true;
    }
    else
    {
        // Compute delta from previous frame
        m_rootMotionDeltaPos = m_localTranslations[ridx] - m_prevRootPos;
        m_rootMotionDeltaRot = glm::inverse(m_prevRootRot) * m_localRotations[ridx];

        // Save current as previous for next frame
        m_prevRootPos = m_localTranslations[ridx];
        m_prevRootRot = m_localRotations[ridx];
    }

    // Zero out the root bone's horizontal motion so the skeleton stays centered
    // Keep Y translation for vertical movement (jumps, stairs)
    m_localTranslations[ridx].x = 0.0f;
    m_localTranslations[ridx].z = 0.0f;
    m_localRotations[ridx] = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

// ---------------------------------------------------------------------------
// initializeBuffers — allocate pose buffers and set bind pose
// ---------------------------------------------------------------------------
void SkeletonAnimator::initializeBuffers()
{
    int jc = m_skeleton->getJointCount();
    size_t count = static_cast<size_t>(jc);
    m_localTranslations.resize(count);
    m_localRotations.resize(count);
    m_localScales.resize(count);
    m_globalTransforms.resize(count);
    m_boneMatrices.resize(count, glm::mat4(1.0f));

    // Source buffers for crossfade
    m_sourceTranslations.resize(count);
    m_sourceRotations.resize(count);
    m_sourceScales.resize(count);

    // Initialize from bind pose
    for (size_t i = 0; i < count; ++i)
    {
        const auto& joint = m_skeleton->m_joints[i];
        m_localTranslations[i] = glm::vec3(joint.localBindTransform[3]);
        m_localRotations[i] = glm::quat_cast(joint.localBindTransform);
        m_localScales[i] = glm::vec3(
            glm::length(glm::vec3(joint.localBindTransform[0])),
            glm::length(glm::vec3(joint.localBindTransform[1])),
            glm::length(glm::vec3(joint.localBindTransform[2])));
    }
}

// ---------------------------------------------------------------------------
// computeBoneMatrices
// ---------------------------------------------------------------------------
void SkeletonAnimator::computeBoneMatrices()
{
    int jointCount = m_skeleton->getJointCount();

    for (int i = 0; i < jointCount; ++i)
    {
        size_t idx = static_cast<size_t>(i);

        // Build local transform from T * R * S
        glm::mat4 local = glm::translate(glm::mat4(1.0f), m_localTranslations[idx])
                        * glm::mat4_cast(m_localRotations[idx])
                        * glm::scale(glm::mat4(1.0f), m_localScales[idx]);

        // Compute global transform: parent's global * local
        int parentIdx = m_skeleton->m_joints[idx].parentIndex;
        if (parentIdx >= 0)
        {
            m_globalTransforms[idx] = m_globalTransforms[static_cast<size_t>(parentIdx)] * local;
        }
        else
        {
            m_globalTransforms[idx] = local;
        }

        // Final bone matrix: global * inverse bind
        m_boneMatrices[idx] = m_globalTransforms[idx]
                            * m_skeleton->m_joints[idx].inverseBindMatrix;
    }
}

// ---------------------------------------------------------------------------
// clone
// ---------------------------------------------------------------------------
std::unique_ptr<Component> SkeletonAnimator::clone() const
{
    auto copy = std::make_unique<SkeletonAnimator>();
    copy->m_skeleton = m_skeleton;
    copy->m_clips = m_clips;
    copy->m_activeClipIndex = m_activeClipIndex;
    copy->m_currentTime = 0.0f;  // Reset playback for cloned entity
    copy->m_speed = m_speed;
    copy->m_looping = m_looping;
    copy->m_paused = false;
    copy->m_playing = m_playing;
    copy->m_rootMotionMode = m_rootMotionMode;
    copy->m_rootMotionBone = m_rootMotionBone;
    copy->setEnabled(isEnabled());

    // Allocate per-instance buffers if skeleton is set
    if (m_skeleton)
    {
        copy->initializeBuffers();
    }

    return copy;
}

// ---------------------------------------------------------------------------
// setSkeleton
// ---------------------------------------------------------------------------
void SkeletonAnimator::setSkeleton(std::shared_ptr<Skeleton> skeleton)
{
    m_skeleton = std::move(skeleton);

    if (m_skeleton)
    {
        initializeBuffers();
    }
    else
    {
        m_localTranslations.clear();
        m_localRotations.clear();
        m_localScales.clear();
        m_globalTransforms.clear();
        m_boneMatrices.clear();
        m_sourceTranslations.clear();
        m_sourceRotations.clear();
        m_sourceScales.clear();
    }
}

const std::shared_ptr<Skeleton>& SkeletonAnimator::getSkeleton() const
{
    return m_skeleton;
}

// ---------------------------------------------------------------------------
// Clip management
// ---------------------------------------------------------------------------
void SkeletonAnimator::addClip(std::shared_ptr<AnimationClip> clip)
{
    m_clips.push_back(std::move(clip));
}

int SkeletonAnimator::getClipCount() const
{
    return static_cast<int>(m_clips.size());
}

const std::shared_ptr<AnimationClip>& SkeletonAnimator::getClip(int index) const
{
    static const std::shared_ptr<AnimationClip> s_null;
    if (index < 0 || index >= static_cast<int>(m_clips.size()))
    {
        return s_null;
    }
    return m_clips[static_cast<size_t>(index)];
}

// ---------------------------------------------------------------------------
// Playback control (instant switch)
// ---------------------------------------------------------------------------
void SkeletonAnimator::play(const std::string& clipName)
{
    for (int i = 0; i < static_cast<int>(m_clips.size()); ++i)
    {
        if (m_clips[static_cast<size_t>(i)]->getName() == clipName)
        {
            playIndex(i);
            return;
        }
    }
}

void SkeletonAnimator::playIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(m_clips.size()))
    {
        return;
    }
    m_activeClipIndex = index;
    m_currentTime = 0.0f;
    m_playing = true;
    m_paused = false;
    m_crossfading = false;
    m_rootMotionInitialized = false;
}

void SkeletonAnimator::stop()
{
    m_playing = false;
    m_currentTime = 0.0f;
    m_activeClipIndex = -1;
    m_crossfading = false;
    m_rootMotionInitialized = false;
}

void SkeletonAnimator::setPaused(bool paused)
{
    m_paused = paused;
}

bool SkeletonAnimator::isPaused() const
{
    return m_paused;
}

void SkeletonAnimator::setLooping(bool loop)
{
    m_looping = loop;
}

bool SkeletonAnimator::isLooping() const
{
    return m_looping;
}

void SkeletonAnimator::setSpeed(float speed)
{
    m_speed = speed;
}

float SkeletonAnimator::getSpeed() const
{
    return m_speed;
}

bool SkeletonAnimator::isPlaying() const
{
    return m_playing;
}

float SkeletonAnimator::getCurrentTime() const
{
    return m_currentTime;
}

int SkeletonAnimator::getActiveClipIndex() const
{
    return m_activeClipIndex;
}

// ---------------------------------------------------------------------------
// Crossfade blending
// ---------------------------------------------------------------------------
void SkeletonAnimator::crossfadeTo(const std::string& clipName, float duration)
{
    for (int i = 0; i < static_cast<int>(m_clips.size()); ++i)
    {
        if (m_clips[static_cast<size_t>(i)]->getName() == clipName)
        {
            crossfadeToIndex(i, duration);
            return;
        }
    }
}

void SkeletonAnimator::crossfadeToIndex(int index, float duration)
{
    if (index < 0 || index >= static_cast<int>(m_clips.size()))
    {
        return;
    }

    if (duration <= 0.0f)
    {
        // Instant switch
        playIndex(index);
        return;
    }

    if (m_crossfading)
    {
        // Already crossfading: snapshot the current blended pose as new source
        m_sourceTranslations = m_localTranslations;
        m_sourceRotations = m_localRotations;
        m_sourceScales = m_localScales;
        m_sourceClipIndex = -1;  // Frozen pose
    }
    else if (m_activeClipIndex >= 0 && m_playing)
    {
        // Normal: current clip becomes source
        m_sourceTranslations = m_localTranslations;
        m_sourceRotations = m_localRotations;
        m_sourceScales = m_localScales;
        m_sourceClipIndex = m_activeClipIndex;
        m_sourceTime = m_currentTime;
    }
    else
    {
        // Not playing anything — just start the new clip
        playIndex(index);
        return;
    }

    m_activeClipIndex = index;
    m_currentTime = 0.0f;
    m_crossfading = true;
    m_crossfadeTime = 0.0f;
    m_crossfadeDuration = duration;
    m_playing = true;
    m_paused = false;
    m_rootMotionInitialized = false;
}

bool SkeletonAnimator::isCrossfading() const
{
    return m_crossfading;
}

// ---------------------------------------------------------------------------
// Root motion
// ---------------------------------------------------------------------------
void SkeletonAnimator::setRootMotionMode(RootMotionMode mode)
{
    m_rootMotionMode = mode;
    m_rootMotionInitialized = false;
}

RootMotionMode SkeletonAnimator::getRootMotionMode() const
{
    return m_rootMotionMode;
}

void SkeletonAnimator::setRootMotionBone(int jointIndex)
{
    m_rootMotionBone = jointIndex;
    m_rootMotionInitialized = false;
}

glm::vec3 SkeletonAnimator::getRootMotionDeltaPosition() const
{
    return m_rootMotionDeltaPos;
}

glm::quat SkeletonAnimator::getRootMotionDeltaRotation() const
{
    return m_rootMotionDeltaRot;
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------
const std::vector<glm::mat4>& SkeletonAnimator::getBoneMatrices() const
{
    return m_boneMatrices;
}

bool SkeletonAnimator::hasBones() const
{
    return m_skeleton && !m_boneMatrices.empty() && m_playing;
}

// ---------------------------------------------------------------------------
// Morph Targets
// ---------------------------------------------------------------------------
const std::vector<float>& SkeletonAnimator::getMorphWeights() const
{
    return m_morphWeights;
}

void SkeletonAnimator::setMorphWeight(int index, float weight)
{
    if (index >= 0 && index < static_cast<int>(m_morphWeights.size()))
    {
        m_morphWeights[static_cast<size_t>(index)] = weight;
    }
}

void SkeletonAnimator::setMorphTargetCount(int count)
{
    if (count > 0)
    {
        m_morphWeights.resize(static_cast<size_t>(count), 0.0f);
    }
}

} // namespace Vestige
