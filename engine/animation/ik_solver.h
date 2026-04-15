// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ik_solver.h
/// @brief Inverse kinematics solvers (two-bone IK, look-at IK, foot IK).
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Vestige
{

// ---------------------------------------------------------------------------
// Two-Bone IK
// ---------------------------------------------------------------------------

/// @brief Input for the two-bone IK solver.
struct TwoBoneIKRequest
{
    glm::vec3 startPos;           ///< Start joint position (model space)
    glm::vec3 midPos;             ///< Mid joint position (model space)
    glm::vec3 endPos;             ///< End effector position (model space)

    glm::quat startGlobalRot;     ///< Start joint rotation (model space)
    glm::quat midGlobalRot;       ///< Mid joint rotation (model space)

    glm::quat startLocalRot;      ///< Start joint local rotation (to be corrected)
    glm::quat midLocalRot;        ///< Mid joint local rotation (to be corrected)

    glm::vec3 target;             ///< Target position (model space)
    glm::vec3 poleVector = glm::vec3(0.0f, 0.0f, 1.0f); ///< Mid-joint direction hint
    float weight = 1.0f;          ///< Blend weight [0,1]
};

/// @brief Output of the two-bone IK solver.
struct TwoBoneIKResult
{
    glm::quat startLocalRot;      ///< Corrected local rotation for start joint
    glm::quat midLocalRot;        ///< Corrected local rotation for mid joint
    bool reached = false;          ///< True if target is within reach
};

/// @brief Solves two-bone IK analytically (e.g. arm: shoulder→elbow→hand).
/// @param req Input positions, rotations, target, and pole vector.
/// @return Corrected local rotations for start and mid joints.
TwoBoneIKResult solveTwoBoneIK(const TwoBoneIKRequest& req);

// ---------------------------------------------------------------------------
// Look-At IK
// ---------------------------------------------------------------------------

/// @brief Input for the look-at IK solver.
struct LookAtIKRequest
{
    glm::vec3 jointPos;            ///< Joint position (model space)
    glm::quat jointGlobalRot;      ///< Joint rotation (model space)
    glm::quat jointLocalRot;       ///< Joint local rotation (to be corrected)

    glm::vec3 target;              ///< Look-at target (model space)
    glm::vec3 forwardAxis = glm::vec3(0.0f, 0.0f, -1.0f); ///< Joint's forward in local space

    float maxAngle = glm::radians(90.0f); ///< Maximum rotation angle (radians)
    float weight = 1.0f;           ///< Blend weight [0,1]
};

/// @brief Output of the look-at IK solver.
struct LookAtIKResult
{
    glm::quat localRot;            ///< Corrected local rotation
};

/// @brief Rotates a joint to face a target, with angle clamping.
/// @param req Input joint state, target, and constraints.
/// @return Corrected local rotation.
LookAtIKResult solveLookAtIK(const LookAtIKRequest& req);

// ---------------------------------------------------------------------------
// Foot IK
// ---------------------------------------------------------------------------

/// @brief Input for the foot IK helper.
struct FootIKRequest
{
    // Leg chain positions and rotations (model space)
    glm::vec3 hipPos;
    glm::vec3 kneePos;
    glm::vec3 anklePos;

    glm::quat hipGlobalRot;
    glm::quat kneeGlobalRot;
    glm::quat ankleGlobalRot;

    glm::quat hipLocalRot;
    glm::quat kneeLocalRot;
    glm::quat ankleLocalRot;

    // Ground contact data (from external raycast)
    glm::vec3 groundPosition;     ///< Where the foot should be placed
    glm::vec3 groundNormal = glm::vec3(0.0f, 1.0f, 0.0f); ///< Surface normal

    glm::vec3 kneeForward = glm::vec3(0.0f, 0.0f, 1.0f); ///< Pole vector for knee
    float weight = 1.0f;
};

/// @brief Output of the foot IK helper.
struct FootIKResult
{
    glm::quat hipLocalRot;
    glm::quat kneeLocalRot;
    glm::quat ankleLocalRot;      ///< Ground alignment rotation
    float pelvisOffset = 0.0f;    ///< How much to lower the pelvis (negative Y)
};

/// @brief Solves foot placement using two-bone IK + ankle alignment.
/// @param req Leg chain state and ground contact info.
/// @return Corrected rotations and pelvis offset.
FootIKResult solveFootIK(const FootIKRequest& req);

} // namespace Vestige
