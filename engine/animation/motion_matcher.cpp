// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file motion_matcher.cpp
/// @brief Runtime motion matching driver implementation.

#include "animation/motion_matcher.h"
#include "animation/skeleton_animator.h"

#include <chrono>
#include <cmath>

namespace Vestige
{

void MotionMatcher::setDatabase(std::shared_ptr<MotionDatabase> db)
{
    m_database = std::move(db);
    m_currentFrame = -1;
    m_searchAccumulator = m_searchInterval; // Force immediate first search
}

void MotionMatcher::setAnimator(SkeletonAnimator* animator)
{
    m_animator = animator;
}

void MotionMatcher::update(const glm::vec2& inputDir, float inputSpeed,
                           float cameraYaw, float dt)
{
    if (!m_database || !m_database->isBuilt() || !m_animator)
        return;

    // Update trajectory prediction
    m_trajectoryPredictor.update(inputDir, inputSpeed, cameraYaw, dt);

    // Update inertialization decay
    m_inertialization.update(dt);

    // Advance current frame time
    m_currentFrameTime += dt;
    ++m_framesSinceTransition;

    // Check if it's time to search
    m_searchAccumulator += dt;
    if (m_searchAccumulator >= m_searchInterval)
    {
        m_searchAccumulator -= m_searchInterval;
        performSearch();
    }
}

void MotionMatcher::performSearch()
{
    int numFeatures = m_database->getFeatureCount();
    if (numFeatures <= 0)
        return;

    // Reuse query vector across searches to avoid per-search allocation
    m_queryBuffer.resize(static_cast<size_t>(numFeatures));
    std::fill(m_queryBuffer.begin(), m_queryBuffer.end(), 0.0f);
    buildQueryVector(m_queryBuffer.data());

    // Time the search
    auto startTime = std::chrono::high_resolution_clock::now();

    // Search with exclusion of current frame neighborhood
    int excludeRadius = static_cast<int>(m_searchInterval * 30.0f); // ~3 frames at 30Hz
    MotionSearchResult result = m_database->search(
        m_queryBuffer.data(), m_tagMask, m_currentFrame, excludeRadius);

    auto endTime = std::chrono::high_resolution_clock::now();
    m_searchTimeMicros = std::chrono::duration<float, std::micro>(
        endTime - startTime).count();

    m_lastSearchCost = result.cost;
    m_lastMatchFrame = result.frameIndex;
    m_lastMatchClip = result.clipIndex;
    m_lastMatchClipTime = result.clipTime;

    if (result.frameIndex < 0)
        return;

    // Decide whether to transition
    bool shouldTransition = false;

    if (m_currentFrame < 0)
    {
        // No current animation — always transition
        shouldTransition = true;
    }
    else
    {
        // Transition if the best match is far from current playback
        // and the cost improvement justifies the switch
        int frameDist = std::abs(result.frameIndex - m_currentFrame);
        bool differentClip = result.clipIndex !=
            m_database->getFrameInfo(m_currentFrame).clipIndex;

        if (differentClip || frameDist > excludeRadius)
        {
            if (result.cost < m_transitionCostThreshold ||
                m_framesSinceTransition > 30) // Force re-evaluation after 1 second
            {
                shouldTransition = true;
            }
        }
    }

    if (shouldTransition)
    {
        // Record inertialization offsets before switching
        if (m_currentFrame >= 0 && m_animator->hasBones())
        {
            const auto& srcPose = m_database->getPose(m_currentFrame);
            const auto& dstPose = m_database->getPose(result.frameIndex);

            m_inertialization.start(
                srcPose.positions, srcPose.rotations, srcPose.velocities,
                dstPose.positions, dstPose.rotations, dstPose.velocities,
                m_inertializationHalflife);
        }

        // Switch the animator to the new clip/time
        if (result.clipIndex >= 0 && result.clipIndex < m_animator->getClipCount())
        {
            m_animator->playIndex(result.clipIndex);
        }

        m_currentFrame = result.frameIndex;
        m_currentFrameTime = 0.0f;
        m_framesSinceTransition = 0;
    }
    else
    {
        // Advance current frame estimate based on elapsed time
        float sampleRate = 30.0f;
        int frameAdvance = static_cast<int>(m_currentFrameTime * sampleRate);
        if (frameAdvance > 0 && m_currentFrame >= 0)
        {
            int newFrame = m_currentFrame + frameAdvance;
            if (newFrame < m_database->getFrameCount())
            {
                // Only advance if still in the same clip — validate that
                // every frame in [m_currentFrame+1 .. newFrame] belongs to
                // the same clip, not just the endpoint.  Because frames are
                // stored contiguously per clip, it is sufficient to check
                // the endpoint: if it is still in the same clip, so are all
                // intermediate frames.  However, we must also verify that
                // newFrame does not exceed the clip's last frame to avoid
                // running past the clip boundary into the next clip's range.
                int currentClip = m_database->getFrameInfo(m_currentFrame).clipIndex;
                if (m_database->getFrameInfo(newFrame).clipIndex == currentClip)
                {
                    m_currentFrame = newFrame;
                    m_currentFrameTime -= static_cast<float>(frameAdvance) / sampleRate;
                }
                else
                {
                    // newFrame crossed into a different clip. Clamp to the
                    // last frame that still belongs to the current clip by
                    // scanning backwards from newFrame-1.
                    int clampedFrame = newFrame - 1;
                    while (clampedFrame > m_currentFrame &&
                           m_database->getFrameInfo(clampedFrame).clipIndex != currentClip)
                    {
                        --clampedFrame;
                    }
                    if (clampedFrame > m_currentFrame &&
                        m_database->getFrameInfo(clampedFrame).clipIndex == currentClip)
                    {
                        m_currentFrame = clampedFrame;
                    }
                    // Consumed the accumulated time regardless of clamping
                    m_currentFrameTime -= static_cast<float>(frameAdvance) / sampleRate;
                }
            }
        }
    }
}

void MotionMatcher::buildQueryVector(float* query) const
{
    const auto& schema = m_database->getSchema();

    // Get current pose from animator
    SkeletonPose currentPose;
    if (m_animator->hasBones())
    {
        const auto& boneMatrices = m_animator->getBoneMatrices();
        int boneCount = static_cast<int>(boneMatrices.size());

        currentPose.positions.resize(static_cast<size_t>(boneCount));
        currentPose.rotations.resize(static_cast<size_t>(boneCount));
        currentPose.velocities.resize(static_cast<size_t>(boneCount), glm::vec3(0.0f));

        for (int i = 0; i < boneCount; ++i)
        {
            size_t si = static_cast<size_t>(i);
            currentPose.positions[si] = glm::vec3(boneMatrices[si][3]);
            currentPose.rotations[si] = glm::quat_cast(boneMatrices[si]);
        }

        // Estimate velocities from the database if we have a current frame
        if (m_currentFrame >= 0 && m_currentFrame < m_database->getFrameCount())
        {
            currentPose.velocities = m_database->getPose(m_currentFrame).velocities;
        }
    }

    // Get trajectory predictions
    const auto& trajSamples = schema.getTrajectorySamples();
    int numTraj = static_cast<int>(trajSamples.size());
    std::vector<float> sampleTimes(static_cast<size_t>(numTraj));
    for (int i = 0; i < numTraj; ++i)
    {
        sampleTimes[static_cast<size_t>(i)] = trajSamples[static_cast<size_t>(i)].timeOffset;
    }

    std::vector<glm::vec2> trajPositions(static_cast<size_t>(numTraj));
    std::vector<glm::vec2> trajDirections(static_cast<size_t>(numTraj));
    m_trajectoryPredictor.predictTrajectory(
        trajPositions.data(), trajDirections.data(),
        sampleTimes.data(), numTraj);

    // Extract features
    glm::vec3 rootPos(0.0f);
    float rootRotY = 0.0f;
    if (!currentPose.positions.empty())
    {
        rootPos = currentPose.positions[0];
        glm::vec3 forward = currentPose.rotations[0] * glm::vec3(0.0f, 0.0f, -1.0f);
        rootRotY = std::atan2(forward.x, -forward.z);
    }

    FeatureExtractor::extract(schema, currentPose, rootPos, rootRotY,
                              trajPositions.data(), trajDirections.data(),
                              query);
}

TrajectoryPredictor& MotionMatcher::getTrajectoryPredictor()
{
    return m_trajectoryPredictor;
}

const Inertialization& MotionMatcher::getInertialization() const
{
    return m_inertialization;
}

void MotionMatcher::setSearchInterval(float seconds)
{
    m_searchInterval = seconds;
}

void MotionMatcher::setTransitionCost(float threshold)
{
    m_transitionCostThreshold = threshold;
}

void MotionMatcher::setInertializationHalflife(float halflife)
{
    m_inertializationHalflife = halflife;
}

void MotionMatcher::setTrajectoryHalflife(float halflife)
{
    m_trajectoryPredictor.setVelocityHalflife(halflife);
    m_trajectoryPredictor.setFacingHalflife(halflife);
}

void MotionMatcher::setTagMask(uint32_t mask)
{
    m_tagMask = mask;
}

float MotionMatcher::getLastSearchCost() const
{
    return m_lastSearchCost;
}

int MotionMatcher::getLastMatchFrame() const
{
    return m_lastMatchFrame;
}

int MotionMatcher::getLastMatchClip() const
{
    return m_lastMatchClip;
}

float MotionMatcher::getLastMatchClipTime() const
{
    return m_lastMatchClipTime;
}

float MotionMatcher::getSearchTimeMicros() const
{
    return m_searchTimeMicros;
}

int MotionMatcher::getFramesSinceTransition() const
{
    return m_framesSinceTransition;
}

bool MotionMatcher::isActive() const
{
    return m_database && m_database->isBuilt() && m_animator;
}

} // namespace Vestige
