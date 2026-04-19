// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file motion_database.cpp
/// @brief Motion matching database implementation.

#include "animation/motion_database.h"
#include "animation/animation_sampler.h"
#include "core/logger.h"

#include <algorithm>
#include <cstring>

namespace Vestige
{

void MotionDatabase::build(const FeatureSchema& schema,
                           const std::vector<AnimClipEntry>& clips,
                           const Skeleton& skeleton,
                           float sampleRate)
{
    m_schema = schema;
    m_numFeatures = schema.getDimensionCount();
    m_clips = clips;
    m_sampleRate = sampleRate;
    m_built = false;

    m_features.clear();
    m_frameInfo.clear();
    m_poses.clear();

    if (m_numFeatures <= 0 || clips.empty())
    {
        Logger::warning("MotionDatabase::build — empty schema or no clips");
        return;
    }

    // Sample all clips at the given rate
    for (int i = 0; i < static_cast<int>(clips.size()); ++i)
    {
        sampleClip(i, clips[static_cast<size_t>(i)], skeleton, sampleRate);
    }

    if (m_poses.empty())
    {
        Logger::warning("MotionDatabase::build — no frames extracted");
        return;
    }

    // Extract features from sampled poses
    extractFeatures();

    // Normalize features
    int numFrames = static_cast<int>(m_frameInfo.size());
    m_normalizer.compute(m_features.data(), numFrames, m_numFeatures);

    // Normalize the feature matrix in-place
    for (int f = 0; f < numFrames; ++f)
    {
        m_normalizer.normalize(&m_features[static_cast<size_t>(f * m_numFeatures)],
                               m_numFeatures);
    }

    // Apply weight scaling (features pre-scaled by sqrt(weight))
    std::vector<float> weights = m_schema.getWeights();
    std::vector<float> sqrtWeights(weights.size());
    for (size_t i = 0; i < weights.size(); ++i)
    {
        sqrtWeights[i] = std::sqrt(weights[i]);
    }

    for (int f = 0; f < numFrames; ++f)
    {
        for (int d = 0; d < m_numFeatures; ++d)
        {
            m_features[static_cast<size_t>(f * m_numFeatures + d)] *=
                sqrtWeights[static_cast<size_t>(d)];
        }
    }

    // Build KD-tree
    buildSearchStructure();

    m_built = true;
    Logger::info("MotionDatabase built: " + std::to_string(numFrames)
                 + " frames, " + std::to_string(m_numFeatures) + " features");
}

void MotionDatabase::sampleClip(int clipIdx, const AnimClipEntry& entry,
                                const Skeleton& skeleton, float sampleRate)
{
    if (!entry.clip)
        return;

    const auto& clip = *entry.clip;
    float duration = clip.getDuration();
    if (duration <= 0.0f || sampleRate <= 0.0f)
        return;

    float dt = 1.0f / sampleRate;
    int numJoints = skeleton.getJointCount();

    // Sample at each time step
    SkeletonPose prevPose;
    prevPose.positions.resize(static_cast<size_t>(numJoints), glm::vec3(0.0f));

    bool hasPrev = false;

    for (float t = 0.0f; t < duration; t += dt)
    {
        SkeletonPose pose;
        pose.positions.resize(static_cast<size_t>(numJoints), glm::vec3(0.0f));
        pose.rotations.resize(static_cast<size_t>(numJoints), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        pose.velocities.resize(static_cast<size_t>(numJoints), glm::vec3(0.0f));

        // Sample each channel of the clip at time t
        std::vector<glm::vec3> localTranslations(static_cast<size_t>(numJoints));
        std::vector<glm::quat> localRotations(static_cast<size_t>(numJoints));
        std::vector<glm::vec3> localScales(static_cast<size_t>(numJoints), glm::vec3(1.0f));

        // Initialize from bind pose
        for (int j = 0; j < numJoints; ++j)
        {
            const auto& joint = skeleton.m_joints[static_cast<size_t>(j)];
            glm::mat4 bind = joint.localBindTransform;
            localTranslations[static_cast<size_t>(j)] = glm::vec3(bind[3]);
            localRotations[static_cast<size_t>(j)] = glm::quat_cast(bind);
        }

        // Override with clip channel data
        for (const auto& channel : clip.m_channels)
        {
            if (channel.jointIndex < 0 || channel.jointIndex >= numJoints)
                continue;

            size_t ji = static_cast<size_t>(channel.jointIndex);
            switch (channel.targetPath)
            {
                case AnimTargetPath::TRANSLATION:
                    localTranslations[ji] = sampleVec3(channel, t);
                    break;
                case AnimTargetPath::ROTATION:
                    localRotations[ji] = sampleQuat(channel, t);
                    break;
                case AnimTargetPath::SCALE:
                    localScales[ji] = sampleVec3(channel, t);
                    break;
                default:
                    break;
            }
        }

        // Compute model-space transforms by walking hierarchy
        std::vector<glm::mat4> globalTransforms(static_cast<size_t>(numJoints));
        for (int j = 0; j < numJoints; ++j)
        {
            size_t ji = static_cast<size_t>(j);
            glm::mat4 local = glm::mat4_cast(localRotations[ji]);
            local[3] = glm::vec4(localTranslations[ji], 1.0f);
            // Apply scale
            local[0] *= localScales[ji].x;
            local[1] *= localScales[ji].y;
            local[2] *= localScales[ji].z;

            int parent = skeleton.m_joints[ji].parentIndex;
            if (parent >= 0)
                globalTransforms[ji] = globalTransforms[static_cast<size_t>(parent)] * local;
            else
                globalTransforms[ji] = local;

            pose.positions[ji] = glm::vec3(globalTransforms[ji][3]);
            pose.rotations[ji] = glm::quat_cast(globalTransforms[ji]);
        }

        // Compute velocities via finite difference
        if (hasPrev)
        {
            for (int j = 0; j < numJoints; ++j)
            {
                size_t ji = static_cast<size_t>(j);
                pose.velocities[ji] = (pose.positions[ji] - prevPose.positions[ji]) / dt;
            }
        }

        m_poses.push_back(std::move(pose));

        FrameInfo info;
        info.clipIndex = clipIdx;
        info.clipTime = t;
        info.tags = entry.defaultTags;
        info.mirrored = false;
        m_frameInfo.push_back(info);

        prevPose.positions.resize(static_cast<size_t>(numJoints));
        for (int j = 0; j < numJoints; ++j)
        {
            prevPose.positions[static_cast<size_t>(j)] =
                m_poses.back().positions[static_cast<size_t>(j)];
        }
        hasPrev = true;
    }
}

void MotionDatabase::extractFeatures()
{
    int numFrames = static_cast<int>(m_poses.size());
    m_features.resize(static_cast<size_t>(numFrames * m_numFeatures), 0.0f);

    const auto& trajSamples = m_schema.getTrajectorySamples();
    int numTrajSamples = static_cast<int>(trajSamples.size());
    float dt = 1.0f / m_sampleRate;

    for (int f = 0; f < numFrames; ++f)
    {
        const auto& pose = m_poses[static_cast<size_t>(f)];

        // Root position and rotation for model-space transform
        glm::vec3 rootPos(0.0f);
        float rootRotY = 0.0f;
        if (!pose.positions.empty())
        {
            rootPos = pose.positions[0];
            // Extract Y rotation from root quaternion
            glm::quat rootRot = pose.rotations[0];
            glm::vec3 forward = rootRot * glm::vec3(0.0f, 0.0f, -1.0f);
            rootRotY = std::atan2(forward.x, -forward.z);
        }

        // Compute trajectory features by looking ahead in the animation
        std::vector<glm::vec2> trajPositions(static_cast<size_t>(numTrajSamples));
        std::vector<glm::vec2> trajDirections(static_cast<size_t>(numTrajSamples));

        float cosY = std::cos(-rootRotY);
        float sinY = std::sin(-rootRotY);

        for (int t = 0; t < numTrajSamples; ++t)
        {
            int futureFrame = f + static_cast<int>(trajSamples[static_cast<size_t>(t)].timeOffset / dt);

            // Clamp to database bounds (within same clip)
            const auto& thisInfo = m_frameInfo[static_cast<size_t>(f)];
            int clampedFrame = futureFrame;
            if (clampedFrame >= numFrames)
                clampedFrame = numFrames - 1;
            // Ensure we stay within the same clip
            if (clampedFrame >= 0 &&
                m_frameInfo[static_cast<size_t>(clampedFrame)].clipIndex != thisInfo.clipIndex)
            {
                clampedFrame = f; // Fallback to current
            }

            if (clampedFrame >= 0 && clampedFrame < numFrames)
            {
                const auto& futurePose = m_poses[static_cast<size_t>(clampedFrame)];
                glm::vec3 futureRootPos = futurePose.positions[0];

                // Position relative to current root, in model space
                glm::vec3 relPos = futureRootPos - rootPos;
                trajPositions[static_cast<size_t>(t)] = glm::vec2(
                    cosY * relPos.x + sinY * relPos.z,
                    -sinY * relPos.x + cosY * relPos.z);

                // Direction from current facing
                glm::quat futureRot = futurePose.rotations[0];
                glm::vec3 futureForward = futureRot * glm::vec3(0.0f, 0.0f, -1.0f);
                trajDirections[static_cast<size_t>(t)] = glm::vec2(
                    cosY * futureForward.x + sinY * futureForward.z,
                    -sinY * futureForward.x + cosY * futureForward.z);
            }
        }

        FeatureExtractor::extract(m_schema, pose, rootPos, rootRotY,
                                  trajPositions.data(), trajDirections.data(),
                                  &m_features[static_cast<size_t>(f * m_numFeatures)]);
    }
}

void MotionDatabase::buildSearchStructure()
{
    int numFrames = static_cast<int>(m_frameInfo.size());
    m_kdTree.build(m_features.data(), numFrames, m_numFeatures);
}

MotionSearchResult MotionDatabase::search(const float* query, uint32_t tagMask,
                                          int excludeFrame, int excludeRadius) const
{
    if (!m_built)
        return {};

    // Copy and normalize the query
    std::vector<float> normalizedQuery(query, query + m_numFeatures);
    m_normalizer.normalize(normalizedQuery.data(), m_numFeatures);

    // Apply weight scaling
    std::vector<float> weights = m_schema.getWeights();
    for (int d = 0; d < m_numFeatures; ++d)
    {
        normalizedQuery[static_cast<size_t>(d)] *= std::sqrt(weights[static_cast<size_t>(d)]);
    }

    // Gather frame tags for tag filtering
    std::vector<uint32_t> tags;
    if (tagMask != 0)
    {
        tags.resize(m_frameInfo.size());
        for (size_t i = 0; i < m_frameInfo.size(); ++i)
        {
            tags[i] = m_frameInfo[i].tags;
            // Mark NO_ENTRY frames as tag 0 so they're excluded
            if (m_frameInfo[i].tags & MotionTags::NO_ENTRY)
                tags[i] = 0;
        }
    }

    // Search
    KDSearchResult kdResult = m_kdTree.findNearest(
        normalizedQuery.data(), tagMask,
        tags.empty() ? nullptr : tags.data());

    // If we need to exclude nearby frames, do a brute-force re-search
    if (excludeFrame >= 0 && excludeRadius > 0 &&
        kdResult.frameIndex >= 0 &&
        std::abs(kdResult.frameIndex - excludeFrame) <= excludeRadius &&
        m_frameInfo[static_cast<size_t>(kdResult.frameIndex)].clipIndex ==
        m_frameInfo[static_cast<size_t>(excludeFrame)].clipIndex)
    {
        // Brute force with exclusion
        KDSearchResult best;
        int numFrames = static_cast<int>(m_frameInfo.size());
        for (int i = 0; i < numFrames; ++i)
        {
            // Skip excluded range
            if (std::abs(i - excludeFrame) <= excludeRadius &&
                m_frameInfo[static_cast<size_t>(i)].clipIndex ==
                m_frameInfo[static_cast<size_t>(excludeFrame)].clipIndex)
                continue;

            // Tag filtering
            if (tagMask != 0 && (m_frameInfo[static_cast<size_t>(i)].tags & tagMask) == 0)
                continue;
            if (m_frameInfo[static_cast<size_t>(i)].tags & MotionTags::NO_ENTRY)
                continue;

            float dist = 0.0f;
            for (int d = 0; d < m_numFeatures; ++d)
            {
                float diff = normalizedQuery[static_cast<size_t>(d)]
                           - m_features[static_cast<size_t>(i * m_numFeatures + d)];
                dist += diff * diff;
            }

            if (dist < best.cost)
            {
                best.cost = dist;
                best.frameIndex = i;
            }
        }
        kdResult = best;
    }

    MotionSearchResult result;
    result.frameIndex = kdResult.frameIndex;
    result.cost = kdResult.cost;

    if (kdResult.frameIndex >= 0)
    {
        const auto& info = m_frameInfo[static_cast<size_t>(kdResult.frameIndex)];
        result.clipIndex = info.clipIndex;
        result.clipTime = info.clipTime;
        result.mirrored = info.mirrored;
    }

    return result;
}

void MotionDatabase::normalizeQuery(float* query) const
{
    m_normalizer.normalize(query, m_numFeatures);
}

int MotionDatabase::getFrameCount() const
{
    return static_cast<int>(m_frameInfo.size());
}

int MotionDatabase::getFeatureCount() const
{
    return m_numFeatures;
}

const FrameInfo& MotionDatabase::getFrameInfo(int frameIndex) const
{
    // std::clamp(x, 0, -1) is UB (hi < lo); guard against an empty database.
    if (m_frameInfo.empty())
    {
        static const FrameInfo s_empty{};
        return s_empty;
    }
    size_t idx = static_cast<size_t>(std::clamp(frameIndex, 0,
        static_cast<int>(m_frameInfo.size()) - 1));
    return m_frameInfo[idx];
}

const SkeletonPose& MotionDatabase::getPose(int frameIndex) const
{
    if (m_poses.empty())
    {
        static const SkeletonPose s_empty{};
        return s_empty;
    }
    size_t idx = static_cast<size_t>(std::clamp(frameIndex, 0,
        static_cast<int>(m_poses.size()) - 1));
    return m_poses[idx];
}

const FeatureSchema& MotionDatabase::getSchema() const
{
    return m_schema;
}

const FeatureNormalizer& MotionDatabase::getNormalizer() const
{
    return m_normalizer;
}

bool MotionDatabase::isBuilt() const
{
    return m_built;
}

void MotionDatabase::addMirroredFrames(
    const std::vector<std::pair<int, int>>& leftRightPairs)
{
    int originalCount = static_cast<int>(m_poses.size());
    if (originalCount == 0)
        return;

    for (int f = 0; f < originalCount; ++f)
    {
        const auto& srcPose = m_poses[static_cast<size_t>(f)];
        const auto& srcInfo = m_frameInfo[static_cast<size_t>(f)];

        SkeletonPose mirrorPose;
        mirrorPose.positions = srcPose.positions;
        mirrorPose.rotations = srcPose.rotations;
        mirrorPose.velocities = srcPose.velocities;

        // Swap left/right bone pairs
        for (const auto& pair : leftRightPairs)
        {
            size_t left = static_cast<size_t>(pair.first);
            size_t right = static_cast<size_t>(pair.second);

            if (left < mirrorPose.positions.size() && right < mirrorPose.positions.size())
            {
                std::swap(mirrorPose.positions[left], mirrorPose.positions[right]);
                std::swap(mirrorPose.rotations[left], mirrorPose.rotations[right]);
                std::swap(mirrorPose.velocities[left], mirrorPose.velocities[right]);
            }
        }

        // Negate X axis for all bones (mirror across sagittal plane)
        for (size_t j = 0; j < mirrorPose.positions.size(); ++j)
        {
            mirrorPose.positions[j].x = -mirrorPose.positions[j].x;
            mirrorPose.velocities[j].x = -mirrorPose.velocities[j].x;

            // Mirror rotation: negate Y and Z components of quaternion
            mirrorPose.rotations[j].y = -mirrorPose.rotations[j].y;
            mirrorPose.rotations[j].z = -mirrorPose.rotations[j].z;
        }

        m_poses.push_back(std::move(mirrorPose));

        FrameInfo mirrorInfo = srcInfo;
        mirrorInfo.mirrored = true;
        m_frameInfo.push_back(mirrorInfo);
    }

    // Re-extract features and rebuild
    extractFeatures();

    int numFrames = static_cast<int>(m_frameInfo.size());
    m_normalizer.compute(m_features.data(), numFrames, m_numFeatures);

    for (int f = 0; f < numFrames; ++f)
    {
        m_normalizer.normalize(&m_features[static_cast<size_t>(f * m_numFeatures)],
                               m_numFeatures);
    }

    std::vector<float> weights = m_schema.getWeights();
    for (int f = 0; f < numFrames; ++f)
    {
        for (int d = 0; d < m_numFeatures; ++d)
        {
            m_features[static_cast<size_t>(f * m_numFeatures + d)] *=
                std::sqrt(weights[static_cast<size_t>(d)]);
        }
    }

    buildSearchStructure();

    Logger::info("MotionDatabase mirrored: " + std::to_string(numFrames)
                 + " total frames (doubled from " + std::to_string(originalCount) + ")");
}

} // namespace Vestige
