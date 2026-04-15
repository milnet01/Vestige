// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file animation_sampler.cpp
/// @brief Animation channel interpolation implementation.
#include "animation/animation_sampler.h"

#include <algorithm>

namespace Vestige
{

/// @brief Finds the index of the keyframe just before (or at) the given time.
/// Returns the index into the timestamps array such that timestamps[i] <= time < timestamps[i+1].
/// Uses binary search (upper_bound) for O(log n) lookup.
static int findKeyframe(const std::vector<float>& timestamps, float time)
{
    if (timestamps.size() <= 1)
    {
        return 0;
    }

    // upper_bound returns iterator to first element > time
    auto it = std::upper_bound(timestamps.begin(), timestamps.end(), time);

    if (it == timestamps.begin())
    {
        return 0;
    }
    if (it == timestamps.end())
    {
        return static_cast<int>(timestamps.size()) - 2;
    }

    return static_cast<int>(std::distance(timestamps.begin(), it)) - 1;
}

/// @brief Computes the interpolation factor between two keyframes.
static float computeT(const std::vector<float>& timestamps, int index, float time)
{
    float t0 = timestamps[static_cast<size_t>(index)];
    float t1 = timestamps[static_cast<size_t>(index) + 1];
    float duration = t1 - t0;
    if (duration <= 0.0f)
    {
        return 0.0f;
    }
    return std::clamp((time - t0) / duration, 0.0f, 1.0f);
}

/// @brief Reads a vec3 from a flat float array at the given keyframe index.
static glm::vec3 readVec3(const std::vector<float>& values, int keyIndex)
{
    size_t base = static_cast<size_t>(keyIndex) * 3;
    if (base + 2 >= values.size())
        return glm::vec3(0.0f);
    return glm::vec3(values[base], values[base + 1], values[base + 2]);
}

/// @brief Reads a quaternion from a flat float array at the given keyframe index.
/// glTF stores quaternions as (x, y, z, w).
static glm::quat readQuat(const std::vector<float>& values, int keyIndex)
{
    size_t base = static_cast<size_t>(keyIndex) * 4;
    if (base + 3 >= values.size())
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    // glm::quat constructor is (w, x, y, z)
    return glm::quat(values[base + 3], values[base], values[base + 1], values[base + 2]);
}

/// @brief Reads a vec3 from CUBICSPLINE data (3 elements per keyframe: in-tangent, value, out-tangent).
/// @param tripletIndex 0=in-tangent, 1=value, 2=out-tangent
static glm::vec3 readVec3Cubic(const std::vector<float>& values, int keyIndex, int tripletIndex)
{
    // Each keyframe has 3 vec3s (9 floats): [in-tangent(3), value(3), out-tangent(3)]
    size_t base = static_cast<size_t>(keyIndex) * 9 + static_cast<size_t>(tripletIndex) * 3;
    if (base + 2 >= values.size())
        return glm::vec3(0.0f);
    return glm::vec3(values[base], values[base + 1], values[base + 2]);
}

/// @brief Reads a quat from CUBICSPLINE data (3 elements per keyframe: in-tangent, value, out-tangent).
/// @param tripletIndex 0=in-tangent, 1=value, 2=out-tangent
static glm::quat readQuatCubic(const std::vector<float>& values, int keyIndex, int tripletIndex)
{
    // Each keyframe has 3 vec4s (12 floats): [in-tangent(4), value(4), out-tangent(4)]
    size_t base = static_cast<size_t>(keyIndex) * 12 + static_cast<size_t>(tripletIndex) * 4;
    if (base + 3 >= values.size())
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    return glm::quat(values[base + 3], values[base], values[base + 1], values[base + 2]);
}

glm::vec3 sampleVec3(const AnimationChannel& channel, float time)
{
    const auto& ts = channel.timestamps;
    const auto& vals = channel.values;
    bool cubic = (channel.interpolation == AnimInterpolation::CUBICSPLINE);

    if (ts.empty())
    {
        return glm::vec3(0.0f);
    }

    // Before first keyframe or single keyframe
    if (time <= ts.front() || ts.size() == 1)
    {
        return cubic ? readVec3Cubic(vals, 0, 1) : readVec3(vals, 0);
    }

    // After last keyframe
    if (time >= ts.back())
    {
        int last = static_cast<int>(ts.size()) - 1;
        return cubic ? readVec3Cubic(vals, last, 1) : readVec3(vals, last);
    }

    int i = findKeyframe(ts, time);

    if (channel.interpolation == AnimInterpolation::STEP)
    {
        return readVec3(vals, i);
    }

    if (cubic)
    {
        float s = computeT(ts, i, time);
        float s2 = s * s;
        float s3 = s2 * s;
        float td = ts[static_cast<size_t>(i) + 1] - ts[static_cast<size_t>(i)];

        glm::vec3 vk  = readVec3Cubic(vals, i, 1);       // value at k
        glm::vec3 bk  = readVec3Cubic(vals, i, 2);       // out-tangent at k
        glm::vec3 vk1 = readVec3Cubic(vals, i + 1, 1);   // value at k+1
        glm::vec3 ak1 = readVec3Cubic(vals, i + 1, 0);   // in-tangent at k+1

        return (2.0f * s3 - 3.0f * s2 + 1.0f) * vk
             + (s3 - 2.0f * s2 + s)            * td * bk
             + (-2.0f * s3 + 3.0f * s2)        * vk1
             + (s3 - s2)                        * td * ak1;
    }

    // LINEAR
    float t = computeT(ts, i, time);
    glm::vec3 a = readVec3(vals, i);
    glm::vec3 b = readVec3(vals, i + 1);
    return glm::mix(a, b, t);
}

glm::quat sampleQuat(const AnimationChannel& channel, float time)
{
    const auto& ts = channel.timestamps;
    const auto& vals = channel.values;
    bool cubic = (channel.interpolation == AnimInterpolation::CUBICSPLINE);

    if (ts.empty())
    {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // Identity
    }

    // Before first keyframe or single keyframe
    if (time <= ts.front() || ts.size() == 1)
    {
        glm::quat q = cubic ? readQuatCubic(vals, 0, 1) : readQuat(vals, 0);
        return glm::normalize(q);
    }

    // After last keyframe
    if (time >= ts.back())
    {
        int last = static_cast<int>(ts.size()) - 1;
        glm::quat q = cubic ? readQuatCubic(vals, last, 1) : readQuat(vals, last);
        return glm::normalize(q);
    }

    int i = findKeyframe(ts, time);

    if (channel.interpolation == AnimInterpolation::STEP)
    {
        return glm::normalize(readQuat(vals, i));
    }

    if (cubic)
    {
        float s = computeT(ts, i, time);
        float s2 = s * s;
        float s3 = s2 * s;
        float td = ts[static_cast<size_t>(i) + 1] - ts[static_cast<size_t>(i)];

        glm::quat vk  = readQuatCubic(vals, i, 1);       // value at k
        glm::quat bk  = readQuatCubic(vals, i, 2);       // out-tangent at k
        glm::quat vk1 = readQuatCubic(vals, i + 1, 1);   // value at k+1
        glm::quat ak1 = readQuatCubic(vals, i + 1, 0);   // in-tangent at k+1

        // Hermite spline for quaternions (component-wise, then normalize)
        glm::quat result;
        for (int c = 0; c < 4; ++c)
        {
            result[c] = (2.0f * s3 - 3.0f * s2 + 1.0f) * vk[c]
                       + (s3 - 2.0f * s2 + s)            * td * bk[c]
                       + (-2.0f * s3 + 3.0f * s2)        * vk1[c]
                       + (s3 - s2)                        * td * ak1[c];
        }
        return glm::normalize(result);
    }

    // LINEAR — use SLERP for quaternions
    float t = computeT(ts, i, time);
    glm::quat a = readQuat(vals, i);
    glm::quat b = readQuat(vals, i + 1);
    return glm::normalize(glm::slerp(a, b, t));
}

} // namespace Vestige
