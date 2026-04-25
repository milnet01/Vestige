// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file feature_vector.cpp
/// @brief Motion matching feature schema and extraction implementation.

#include "experimental/animation/feature_vector.h"

#include <cmath>

namespace Vestige
{

// ---------------------------------------------------------------------------
// FeatureSchema
// ---------------------------------------------------------------------------

void FeatureSchema::addBoneFeature(const BoneFeature& bf)
{
    m_boneFeatures.push_back(bf);
}

void FeatureSchema::addTrajectorySample(const TrajectorySample& ts)
{
    m_trajectorySamples.push_back(ts);
}

int FeatureSchema::getDimensionCount() const
{
    int dims = 0;
    for (const auto& bf : m_boneFeatures)
    {
        if (bf.position) dims += 3;
        if (bf.velocity) dims += 3;
    }
    for (const auto& ts : m_trajectorySamples)
    {
        if (ts.positionWeight > 0.0f) dims += 2; // XZ
        if (ts.directionWeight > 0.0f) dims += 2; // XZ
    }
    return dims;
}

FeatureSchema FeatureSchema::createDefault(int leftFootBone, int rightFootBone,
                                           int hipBone)
{
    FeatureSchema schema;

    // Foot positions and velocities
    schema.addBoneFeature({leftFootBone, true, true, 1.0f});
    schema.addBoneFeature({rightFootBone, true, true, 1.0f});

    // Hip velocity only (no position — root-relative position is always 0)
    schema.addBoneFeature({hipBone, false, true, 1.0f});

    // Three future trajectory sample points
    schema.addTrajectorySample({0.33f, 1.0f, 1.0f});
    schema.addTrajectorySample({0.67f, 1.0f, 1.0f});
    schema.addTrajectorySample({1.00f, 1.0f, 1.0f});

    return schema;
}

const std::vector<BoneFeature>& FeatureSchema::getBoneFeatures() const
{
    return m_boneFeatures;
}

const std::vector<TrajectorySample>& FeatureSchema::getTrajectorySamples() const
{
    return m_trajectorySamples;
}

std::vector<float> FeatureSchema::getWeights() const
{
    std::vector<float> weights;
    weights.reserve(static_cast<size_t>(getDimensionCount()));

    for (const auto& bf : m_boneFeatures)
    {
        if (bf.position)
        {
            weights.push_back(bf.weight);
            weights.push_back(bf.weight);
            weights.push_back(bf.weight);
        }
        if (bf.velocity)
        {
            weights.push_back(bf.weight);
            weights.push_back(bf.weight);
            weights.push_back(bf.weight);
        }
    }

    for (const auto& ts : m_trajectorySamples)
    {
        if (ts.positionWeight > 0.0f)
        {
            weights.push_back(ts.positionWeight);
            weights.push_back(ts.positionWeight);
        }
        if (ts.directionWeight > 0.0f)
        {
            weights.push_back(ts.directionWeight);
            weights.push_back(ts.directionWeight);
        }
    }

    return weights;
}

// ---------------------------------------------------------------------------
// FeatureExtractor
// ---------------------------------------------------------------------------

void FeatureExtractor::extract(const FeatureSchema& schema,
                               const SkeletonPose& pose,
                               const glm::vec3& /*rootPosition*/,
                               float rootRotationY,
                               const glm::vec2* trajectoryPositions,
                               const glm::vec2* trajectoryDirections,
                               float* output)
{
    int idx = 0;

    // Inverse root rotation to transform to model space
    float cosY = std::cos(-rootRotationY);
    float sinY = std::sin(-rootRotationY);

    for (const auto& bf : schema.getBoneFeatures())
    {
        if (bf.boneIndex < 0)
            continue;

        size_t bi = static_cast<size_t>(bf.boneIndex);

        if (bf.position && bi < pose.positions.size())
        {
            // Position relative to root, rotated to model space
            const glm::vec3& p = pose.positions[bi];
            output[idx++] = cosY * p.x + sinY * p.z;
            output[idx++] = p.y;
            output[idx++] = -sinY * p.x + cosY * p.z;
        }

        if (bf.velocity && bi < pose.velocities.size())
        {
            // Velocity in model space
            const glm::vec3& v = pose.velocities[bi];
            output[idx++] = cosY * v.x + sinY * v.z;
            output[idx++] = v.y;
            output[idx++] = -sinY * v.x + cosY * v.z;
        }
    }

    // Trajectory features (already in root-relative XZ space)
    int trajIdx = 0;
    for (const auto& ts : schema.getTrajectorySamples())
    {
        if (ts.positionWeight > 0.0f && trajectoryPositions)
        {
            output[idx++] = trajectoryPositions[trajIdx].x;
            output[idx++] = trajectoryPositions[trajIdx].y; // XZ stored as vec2
        }
        if (ts.directionWeight > 0.0f && trajectoryDirections)
        {
            output[idx++] = trajectoryDirections[trajIdx].x;
            output[idx++] = trajectoryDirections[trajIdx].y;
        }
        ++trajIdx;
    }
}

// ---------------------------------------------------------------------------
// FeatureNormalizer
// ---------------------------------------------------------------------------

void FeatureNormalizer::compute(const float* features, int numFrames, int numFeatures)
{
    m_mean.assign(static_cast<size_t>(numFeatures), 0.0f);
    m_stddev.assign(static_cast<size_t>(numFeatures), 1.0f);

    if (numFrames <= 0 || numFeatures <= 0)
    {
        m_ready = true;
        return;
    }

    // Compute mean
    for (int f = 0; f < numFrames; ++f)
    {
        for (int d = 0; d < numFeatures; ++d)
        {
            m_mean[static_cast<size_t>(d)] += features[f * numFeatures + d];
        }
    }
    for (int d = 0; d < numFeatures; ++d)
    {
        m_mean[static_cast<size_t>(d)] /= static_cast<float>(numFrames);
    }

    // Compute stddev
    for (int f = 0; f < numFrames; ++f)
    {
        for (int d = 0; d < numFeatures; ++d)
        {
            float diff = features[f * numFeatures + d] - m_mean[static_cast<size_t>(d)];
            m_stddev[static_cast<size_t>(d)] += diff * diff;
        }
    }
    for (int d = 0; d < numFeatures; ++d)
    {
        m_stddev[static_cast<size_t>(d)] =
            std::sqrt(m_stddev[static_cast<size_t>(d)] / static_cast<float>(numFrames));
        // Avoid division by zero
        if (m_stddev[static_cast<size_t>(d)] < 1e-8f)
            m_stddev[static_cast<size_t>(d)] = 1.0f;
    }

    m_ready = true;
}

void FeatureNormalizer::normalize(float* feature, int numFeatures) const
{
    for (int d = 0; d < numFeatures; ++d)
    {
        feature[d] = (feature[d] - m_mean[static_cast<size_t>(d)])
                   / m_stddev[static_cast<size_t>(d)];
    }
}

void FeatureNormalizer::denormalize(float* feature, int numFeatures) const
{
    for (int d = 0; d < numFeatures; ++d)
    {
        feature[d] = feature[d] * m_stddev[static_cast<size_t>(d)]
                   + m_mean[static_cast<size_t>(d)];
    }
}

const std::vector<float>& FeatureNormalizer::getMean() const
{
    return m_mean;
}

const std::vector<float>& FeatureNormalizer::getStddev() const
{
    return m_stddev;
}

bool FeatureNormalizer::isReady() const
{
    return m_ready;
}

} // namespace Vestige
