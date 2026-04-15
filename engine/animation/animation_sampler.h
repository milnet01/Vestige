// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file animation_sampler.h
/// @brief Stateless functions that evaluate animation channels at a given time.
#pragma once

#include "animation/animation_clip.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Vestige
{

/// @brief Samples a translation or scale channel at the given time.
/// @param channel The animation channel to sample.
/// @param time Current playback time in seconds.
/// @return Interpolated vec3 value.
glm::vec3 sampleVec3(const AnimationChannel& channel, float time);

/// @brief Samples a rotation channel at the given time.
/// @param channel The animation channel to sample.
/// @param time Current playback time in seconds.
/// @return Interpolated quaternion (normalized).
glm::quat sampleQuat(const AnimationChannel& channel, float time);

} // namespace Vestige
