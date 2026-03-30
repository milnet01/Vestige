/// @file animation_clip.h
/// @brief Animation clip data — keyframe channels loaded from glTF animations.
#pragma once

#include <string>
#include <vector>

namespace Vestige
{

/// @brief Interpolation method for keyframes.
enum class AnimInterpolation
{
    STEP,         ///< No interpolation — snap to nearest keyframe
    LINEAR,       ///< Linear interpolation (SLERP for quaternions)
    CUBICSPLINE   ///< Hermite spline with in/out tangents (Phase 7B)
};

/// @brief What property a channel animates.
enum class AnimTargetPath
{
    TRANSLATION,  ///< vec3
    ROTATION,     ///< quat (vec4 xyzw in glTF)
    SCALE         ///< vec3
};

/// @brief A single animation channel — targets one property of one joint.
struct AnimationChannel
{
    int jointIndex = -1;                   ///< Which joint this channel animates
    AnimTargetPath targetPath;             ///< Which property (T, R, or S)
    AnimInterpolation interpolation;       ///< How to interpolate between keyframes

    std::vector<float> timestamps;         ///< Keyframe times in seconds (sorted ascending)
    std::vector<float> values;             ///< Packed keyframe values:
                                           ///<   TRANSLATION/SCALE: 3 floats per key
                                           ///<   ROTATION: 4 floats per key (x,y,z,w)
};

/// @brief A named animation clip containing one or more channels.
class AnimationClip
{
public:
    /// @brief Gets the clip duration (max timestamp across all channels).
    float getDuration() const;

    /// @brief Gets the clip name.
    const std::string& getName() const;

    /// @brief Recomputes m_duration from channel data.
    void computeDuration();

    std::string m_name;
    std::vector<AnimationChannel> m_channels;
    float m_duration = 0.0f;  ///< Cached during load
};

} // namespace Vestige
