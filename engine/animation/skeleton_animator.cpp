/// @file skeleton_animator.cpp
/// @brief SkeletonAnimator component implementation.
#include "animation/skeleton_animator.h"
#include "animation/animation_sampler.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace Vestige
{

SkeletonAnimator::SkeletonAnimator() = default;
SkeletonAnimator::~SkeletonAnimator() = default;

void SkeletonAnimator::update(float deltaTime)
{
    if (!m_playing || m_paused || m_activeClipIndex < 0 || !m_skeleton)
    {
        return;
    }

    const auto& clip = m_clips[static_cast<size_t>(m_activeClipIndex)];
    if (!clip || clip->getDuration() <= 0.0f)
    {
        return;
    }

    // Advance time
    m_currentTime += deltaTime * m_speed;

    if (m_looping)
    {
        m_currentTime = std::fmod(m_currentTime, clip->getDuration());
        if (m_currentTime < 0.0f)
        {
            m_currentTime += clip->getDuration();
        }
    }
    else if (m_currentTime >= clip->getDuration())
    {
        m_currentTime = clip->getDuration();
        m_playing = false;
    }

    // Sample all channels
    for (const auto& channel : clip->m_channels)
    {
        int ji = channel.jointIndex;
        if (ji < 0 || ji >= m_skeleton->getJointCount())
        {
            continue;
        }
        size_t idx = static_cast<size_t>(ji);

        switch (channel.targetPath)
        {
        case AnimTargetPath::TRANSLATION:
            m_localTranslations[idx] = sampleVec3(channel, m_currentTime);
            break;
        case AnimTargetPath::ROTATION:
            m_localRotations[idx] = sampleQuat(channel, m_currentTime);
            break;
        case AnimTargetPath::SCALE:
            m_localScales[idx] = sampleVec3(channel, m_currentTime);
            break;
        }
    }

    computeBoneMatrices();
}

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
    copy->setEnabled(isEnabled());

    // Allocate per-instance buffers if skeleton is set
    if (m_skeleton)
    {
        int jc = m_skeleton->getJointCount();
        size_t count = static_cast<size_t>(jc);
        copy->m_localTranslations.resize(count);
        copy->m_localRotations.resize(count);
        copy->m_localScales.resize(count);
        copy->m_globalTransforms.resize(count);
        copy->m_boneMatrices.resize(count, glm::mat4(1.0f));

        // Initialize from bind pose
        for (size_t i = 0; i < count; ++i)
        {
            const auto& joint = m_skeleton->m_joints[i];
            // Decompose bind transform to TRS
            copy->m_localTranslations[i] = glm::vec3(joint.localBindTransform[3]);
            copy->m_localRotations[i] = glm::quat_cast(joint.localBindTransform);
            copy->m_localScales[i] = glm::vec3(
                glm::length(glm::vec3(joint.localBindTransform[0])),
                glm::length(glm::vec3(joint.localBindTransform[1])),
                glm::length(glm::vec3(joint.localBindTransform[2])));
        }
    }

    return copy;
}

void SkeletonAnimator::setSkeleton(std::shared_ptr<Skeleton> skeleton)
{
    m_skeleton = std::move(skeleton);

    if (m_skeleton)
    {
        int jc = m_skeleton->getJointCount();
        size_t count = static_cast<size_t>(jc);
        m_localTranslations.resize(count);
        m_localRotations.resize(count);
        m_localScales.resize(count);
        m_globalTransforms.resize(count);
        m_boneMatrices.resize(count, glm::mat4(1.0f));

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
    else
    {
        m_localTranslations.clear();
        m_localRotations.clear();
        m_localScales.clear();
        m_globalTransforms.clear();
        m_boneMatrices.clear();
    }
}

const std::shared_ptr<Skeleton>& SkeletonAnimator::getSkeleton() const
{
    return m_skeleton;
}

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
    return m_clips[static_cast<size_t>(index)];
}

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
}

void SkeletonAnimator::stop()
{
    m_playing = false;
    m_currentTime = 0.0f;
    m_activeClipIndex = -1;
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

const std::vector<glm::mat4>& SkeletonAnimator::getBoneMatrices() const
{
    return m_boneMatrices;
}

bool SkeletonAnimator::hasBones() const
{
    return m_skeleton && !m_boneMatrices.empty() && m_playing;
}

} // namespace Vestige
