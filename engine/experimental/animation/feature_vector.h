// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file feature_vector.h
/// @brief Motion matching feature schema and extraction.
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

namespace Vestige
{

class Skeleton;
struct AnimationChannel;

/// @brief Defines which bone data to include in a feature vector.
struct BoneFeature
{
    int boneIndex = -1;
    bool position = true;      ///< 3 floats (model-space relative to root)
    bool velocity = true;      ///< 3 floats (model-space)
    float weight = 1.0f;
};

/// @brief Defines a trajectory sample point for feature extraction.
struct TrajectorySample
{
    float timeOffset = 0.0f;   ///< Seconds into the future (positive)
    float positionWeight = 1.0f;  ///< Weight for XZ position (2 floats)
    float directionWeight = 1.0f; ///< Weight for XZ direction (2 floats)
};

/// @brief Configures which features are extracted for motion matching.
///
/// The schema determines the dimensionality and composition of feature
/// vectors used in the motion database. A default schema uses 27 dimensions:
/// feet positions/velocities (12), hip velocity (3), trajectory pos/dir (12).
class FeatureSchema
{
public:
    /// @brief Adds a bone feature to the schema.
    void addBoneFeature(const BoneFeature& bf);

    /// @brief Adds a trajectory sample point.
    void addTrajectorySample(const TrajectorySample& ts);

    /// @brief Total number of float dimensions in a feature vector.
    int getDimensionCount() const;

    /// @brief Creates the standard 27-dimension schema.
    /// Requires bone indices for left foot, right foot, and hip.
    static FeatureSchema createDefault(int leftFootBone, int rightFootBone,
                                       int hipBone);

    /// @brief Gets the bone features.
    const std::vector<BoneFeature>& getBoneFeatures() const;

    /// @brief Gets the trajectory samples.
    const std::vector<TrajectorySample>& getTrajectorySamples() const;

    /// @brief Gets the per-dimension weights (computed from bone/trajectory weights).
    /// Size equals getDimensionCount().
    std::vector<float> getWeights() const;

private:
    std::vector<BoneFeature> m_boneFeatures;
    std::vector<TrajectorySample> m_trajectorySamples;
};

/// @brief Stores a full skeleton pose for one frame (model-space).
struct SkeletonPose
{
    std::vector<glm::vec3> positions;   ///< Per-joint model-space positions
    std::vector<glm::quat> rotations;   ///< Per-joint model-space rotations
    std::vector<glm::vec3> velocities;  ///< Per-joint model-space velocities
};

/// @brief Extracts feature vectors from skeleton poses.
///
/// Given a FeatureSchema and skeleton poses at consecutive frames,
/// produces a flat float array representing the feature vector.
class FeatureExtractor
{
public:
    /// @brief Extracts a feature vector from a single frame pose.
    /// @param schema Feature schema defining which features to extract.
    /// @param pose Current frame's skeleton pose (model-space).
    /// @param rootPosition Character root position (world XZ).
    /// @param rootRotationY Character root Y-axis rotation (radians).
    /// @param trajectoryPositions Future trajectory positions (XZ, root-relative).
    /// @param trajectoryDirections Future trajectory directions (XZ, root-relative).
    /// @param output Output float array (must be at least schema.getDimensionCount()).
    static void extract(const FeatureSchema& schema,
                        const SkeletonPose& pose,
                        const glm::vec3& rootPosition,
                        float rootRotationY,
                        const glm::vec2* trajectoryPositions,
                        const glm::vec2* trajectoryDirections,
                        float* output);
};

/// @brief Computes and stores normalization parameters (mean/stddev per dimension).
class FeatureNormalizer
{
public:
    /// @brief Computes mean and stddev from a feature matrix.
    /// @param features Row-major feature matrix (numFrames × numFeatures).
    /// @param numFrames Number of rows.
    /// @param numFeatures Number of columns (dimensions).
    void compute(const float* features, int numFrames, int numFeatures);

    /// @brief Normalizes a single feature vector in-place.
    void normalize(float* feature, int numFeatures) const;

    /// @brief Denormalizes a single feature vector in-place.
    void denormalize(float* feature, int numFeatures) const;

    /// @brief Gets the mean values.
    const std::vector<float>& getMean() const;

    /// @brief Gets the stddev values.
    const std::vector<float>& getStddev() const;

    /// @brief Whether normalization params have been computed.
    bool isReady() const;

private:
    std::vector<float> m_mean;
    std::vector<float> m_stddev;
    bool m_ready = false;
};

} // namespace Vestige
