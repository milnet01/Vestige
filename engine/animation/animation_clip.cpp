// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file animation_clip.cpp
/// @brief AnimationClip implementation.
#include "animation/animation_clip.h"

#include <algorithm>

namespace Vestige
{

float AnimationClip::getDuration() const
{
    return m_duration;
}

const std::string& AnimationClip::getName() const
{
    return m_name;
}

void AnimationClip::computeDuration()
{
    m_duration = 0.0f;
    for (const auto& channel : m_channels)
    {
        if (!channel.timestamps.empty())
        {
            m_duration = std::max(m_duration, channel.timestamps.back());
        }
    }
}

} // namespace Vestige
