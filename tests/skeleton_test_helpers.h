// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file skeleton_test_helpers.h
/// @brief Shared chain-skeleton builder for animation unit tests.
///
/// /test-audit 2026-05-17 Ts19-D2: the same single-joint and
/// joint-chain skeleton-building boilerplate was duplicated across
/// test_animation_state_machine.cpp, test_root_motion.cpp, and
/// test_crossfade.cpp. The named-joint skeletons used by
/// test_motion_matching.cpp and test_advanced_physics.cpp encode
/// real anatomy (Hips/LeftFoot/RightFoot, biped rig), so they are
/// intentionally NOT consolidated here — those tests want full
/// control over joint names and offsets.
#pragma once

#include "animation/skeleton.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <memory>
#include <string>

namespace Vestige::Testing
{

/// @brief Build an `N`-joint chain: joint 0 is the root, joint i (i > 0)
///        has parent (i-1) and its local-bind transform translates by
///        @a perJointOffset relative to its parent. Identity inverse-bind
///        matrices — adequate for animator-only tests that don't exercise
///        skinning math.
inline std::shared_ptr<Skeleton> makeJointChainSkeleton(
    int jointCount = 1,
    const glm::vec3& perJointOffset = glm::vec3(0.0f))
{
    auto skel = std::make_shared<Skeleton>();
    for (int i = 0; i < jointCount; ++i)
    {
        Joint j;
        j.name = "joint_" + std::to_string(i);
        j.parentIndex = (i == 0) ? -1 : (i - 1);
        j.inverseBindMatrix = glm::mat4(1.0f);
        j.localBindTransform = (i == 0)
            ? glm::mat4(1.0f)
            : glm::translate(glm::mat4(1.0f), perJointOffset);
        skel->m_joints.push_back(j);
    }
    skel->m_rootJoints.push_back(0);
    return skel;
}

}  // namespace Vestige::Testing
