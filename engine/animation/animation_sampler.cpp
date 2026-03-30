/// @file animation_sampler.cpp
/// @brief Animation channel interpolation implementation.
#include "animation/animation_sampler.h"

#include <algorithm>
#include <cmath>

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
    return glm::vec3(values[base], values[base + 1], values[base + 2]);
}

/// @brief Reads a quaternion from a flat float array at the given keyframe index.
/// glTF stores quaternions as (x, y, z, w).
static glm::quat readQuat(const std::vector<float>& values, int keyIndex)
{
    size_t base = static_cast<size_t>(keyIndex) * 4;
    // glm::quat constructor is (w, x, y, z)
    return glm::quat(values[base + 3], values[base], values[base + 1], values[base + 2]);
}

glm::vec3 sampleVec3(const AnimationChannel& channel, float time)
{
    const auto& ts = channel.timestamps;
    const auto& vals = channel.values;

    if (ts.empty())
    {
        return glm::vec3(0.0f);
    }

    // Before first keyframe or single keyframe
    if (time <= ts.front() || ts.size() == 1)
    {
        return readVec3(vals, 0);
    }

    // After last keyframe
    if (time >= ts.back())
    {
        return readVec3(vals, static_cast<int>(ts.size()) - 1);
    }

    int i = findKeyframe(ts, time);

    if (channel.interpolation == AnimInterpolation::STEP)
    {
        return readVec3(vals, i);
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

    if (ts.empty())
    {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // Identity
    }

    // Before first keyframe or single keyframe
    if (time <= ts.front() || ts.size() == 1)
    {
        return glm::normalize(readQuat(vals, 0));
    }

    // After last keyframe
    if (time >= ts.back())
    {
        return glm::normalize(readQuat(vals, static_cast<int>(ts.size()) - 1));
    }

    int i = findKeyframe(ts, time);

    if (channel.interpolation == AnimInterpolation::STEP)
    {
        return glm::normalize(readQuat(vals, i));
    }

    // LINEAR — use SLERP for quaternions
    float t = computeT(ts, i, time);
    glm::quat a = readQuat(vals, i);
    glm::quat b = readQuat(vals, i + 1);
    return glm::normalize(glm::slerp(a, b, t));
}

} // namespace Vestige
