// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ik_solver.cpp
/// @brief Inverse kinematics solver implementations.
#include "animation/ik_solver.h"

#include <algorithm>
#include <cmath>

namespace Vestige
{

static constexpr float IK_EPSILON = 1e-5f;

/// @brief Safely compute acos, clamping input to [-1,1].
static float safeAcos(float x)
{
    return std::acos(std::clamp(x, -1.0f, 1.0f));
}

/// @brief Safely normalize a vector, returning fallback if too short.
static glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback = glm::vec3(0.0f, 1.0f, 0.0f))
{
    float len = glm::length(v);
    if (len < IK_EPSILON) return fallback;
    return v / len;
}

/// @brief NLerp between two quaternions by weight, ensuring shortest path.
static glm::quat nlerpShortest(const glm::quat& a, const glm::quat& b, float t)
{
    glm::quat target = b;
    if (glm::dot(a, b) < 0.0f)
    {
        target = -b;
    }
    glm::quat result = a * (1.0f - t) + target * t;
    return glm::normalize(result);
}

// ---------------------------------------------------------------------------
// Two-Bone IK
// ---------------------------------------------------------------------------

TwoBoneIKResult solveTwoBoneIK(const TwoBoneIKRequest& req)
{
    TwoBoneIKResult result;
    result.startLocalRot = req.startLocalRot;
    result.midLocalRot = req.midLocalRot;
    result.reached = false;

    if (req.weight <= 0.0f)
    {
        return result;
    }

    // Bone lengths
    float lab = glm::length(req.midPos - req.startPos);      // upper bone
    float lcb = glm::length(req.endPos - req.midPos);        // lower bone
    float totalLength = lab + lcb;

    if (lab < IK_EPSILON || lcb < IK_EPSILON)
    {
        return result; // Degenerate chain
    }

    // Distance to target, clamped to solvable range
    float lat = glm::length(req.target - req.startPos);
    bool withinReach = lat <= totalLength;
    lat = std::clamp(lat, IK_EPSILON, totalLength - IK_EPSILON);

    result.reached = withinReach && req.weight >= 1.0f;

    // Current interior angles
    glm::vec3 ac = safeNormalize(req.endPos - req.startPos);
    glm::vec3 ab = safeNormalize(req.midPos - req.startPos);
    glm::vec3 ba = safeNormalize(req.startPos - req.midPos);
    glm::vec3 bc = safeNormalize(req.endPos - req.midPos);
    glm::vec3 at = safeNormalize(req.target - req.startPos);

    float acab0 = safeAcos(glm::dot(ac, ab)); // Angle at start: effector-start vs mid-start
    float babc0 = safeAcos(glm::dot(ba, bc)); // Angle at mid: start-mid vs effector-mid
    float acat0 = safeAcos(glm::dot(ac, at)); // Angle from effector dir to target dir

    // Desired angles (law of cosines)
    float acab1 = safeAcos((lcb * lcb - lab * lab - lat * lat) / (-2.0f * lab * lat));
    float babc1 = safeAcos((lat * lat - lab * lab - lcb * lcb) / (-2.0f * lab * lcb));

    // Rotation axis for bend adjustment
    // Use pole vector as stable reference when chain is nearly extended
    glm::vec3 midForward = req.midGlobalRot * glm::vec3(0.0f, 0.0f, 1.0f);
    glm::vec3 rawAxis = glm::cross(ac, ab);
    glm::vec3 axis0;
    if (glm::length(rawAxis) < IK_EPSILON)
    {
        // Nearly extended — use pole vector for stability
        axis0 = safeNormalize(glm::cross(ac, midForward));
        if (glm::length(glm::cross(ac, midForward)) < IK_EPSILON)
        {
            axis0 = safeNormalize(glm::cross(ac, glm::vec3(0.0f, 1.0f, 0.0f)));
        }
    }
    else
    {
        axis0 = glm::normalize(rawAxis);
    }

    // Rotation axis for swing toward target
    glm::vec3 rawSwing = glm::cross(ac, at);
    glm::vec3 axis1;
    if (glm::length(rawSwing) < IK_EPSILON)
    {
        axis1 = axis0; // Target is along current effector direction
    }
    else
    {
        axis1 = glm::normalize(rawSwing);
    }

    // Build correction quaternions in local space
    glm::quat invStartGlobal = glm::inverse(req.startGlobalRot);
    glm::quat invMidGlobal = glm::inverse(req.midGlobalRot);

    // r0: adjust start joint bend angle
    glm::vec3 localAxis0Start = invStartGlobal * axis0;
    glm::quat r0 = glm::angleAxis(acab1 - acab0, localAxis0Start);

    // r1: adjust mid joint bend angle
    glm::vec3 localAxis0Mid = invMidGlobal * axis0;
    glm::quat r1 = glm::angleAxis(babc1 - babc0, localAxis0Mid);

    // r2: swing start joint toward target
    glm::vec3 localAxis1Start = invStartGlobal * axis1;
    glm::quat r2 = glm::angleAxis(acat0, localAxis1Start);

    // Apply corrections
    glm::quat newStartLocal = req.startLocalRot * r0 * r2;
    glm::quat newMidLocal = req.midLocalRot * r1;

    // --- Pole vector alignment ---
    // Rotate the chain around the start→target axis so the mid joint sits in
    // the pole-vector half-plane.
    //
    // AUDIT A4 — two fixes:
    //   (1) midPerp is derived from the post-solve mid offset via forward
    //       kinematics on newStartGlobal, not by re-rotating the pre-solve
    //       world bone vector ad-hoc; the FK form makes the pose explicit.
    //   (2) poleRot must be pre-multiplied in start's PARENT frame, not
    //       post-multiplied in start's local frame. Post-multiplying in the
    //       local frame applies the rotation BEFORE r0/r2 in the FK chain,
    //       so r0/r2 then unwind the pole alignment. The rotation we want
    //       is "rotate the world chain by poleAngle around at"; expressed
    //       as a delta to newStartLocal that's
    //           newStartLocal' = (parentGlobal⁻¹ · poleRotWorld · parentGlobal) · newStartLocal
    //       which we implement compactly by rotating around at expressed in
    //       the parent frame.
    const glm::quat parentGlobal = req.startGlobalRot * glm::inverse(req.startLocalRot);
    const glm::quat invParentGlobal = glm::inverse(parentGlobal);
    const glm::quat newStartGlobal = parentGlobal * newStartLocal;
    const glm::vec3 boneVecLocal =
        glm::inverse(req.startGlobalRot) * (req.midPos - req.startPos);
    const glm::vec3 postSolveMidOffset = newStartGlobal * boneVecLocal;

    const glm::vec3 projMidOnTarget = at * glm::dot(postSolveMidOffset, at);
    const glm::vec3 midPerp = safeNormalize(postSolveMidOffset - projMidOnTarget);
    const glm::vec3 polePerp = safeNormalize(req.poleVector - at * glm::dot(req.poleVector, at));

    if (glm::length(midPerp) > IK_EPSILON && glm::length(polePerp) > IK_EPSILON)
    {
        float poleAngle = safeAcos(glm::dot(midPerp, polePerp));
        glm::vec3 poleCross = glm::cross(midPerp, polePerp);
        if (glm::dot(poleCross, at) < 0.0f) poleAngle = -poleAngle;

        glm::quat poleRot = glm::angleAxis(poleAngle, invParentGlobal * at);
        newStartLocal = poleRot * newStartLocal;
    }

    // Blend with weight
    if (req.weight < 1.0f)
    {
        result.startLocalRot = nlerpShortest(req.startLocalRot, newStartLocal, req.weight);
        result.midLocalRot = nlerpShortest(req.midLocalRot, newMidLocal, req.weight);
    }
    else
    {
        result.startLocalRot = newStartLocal;
        result.midLocalRot = newMidLocal;
    }

    return result;
}

// ---------------------------------------------------------------------------
// Look-At IK
// ---------------------------------------------------------------------------

LookAtIKResult solveLookAtIK(const LookAtIKRequest& req)
{
    LookAtIKResult result;
    result.localRot = req.jointLocalRot;

    if (req.weight <= 0.0f)
    {
        return result;
    }

    // Current forward direction in model space
    glm::vec3 currentForward = req.jointGlobalRot * req.forwardAxis;

    // Desired direction
    glm::vec3 toTarget = req.target - req.jointPos;
    float dist = glm::length(toTarget);
    if (dist < IK_EPSILON)
    {
        return result;
    }
    glm::vec3 desiredDir = toTarget / dist;

    // Angle between current forward and desired direction
    float angle = safeAcos(glm::dot(currentForward, desiredDir));

    // Clamp to max angle
    if (angle < IK_EPSILON)
    {
        return result;
    }
    angle = std::min(angle, req.maxAngle);

    // Rotation axis (in model space)
    glm::vec3 axis = glm::cross(currentForward, desiredDir);
    float axisLen = glm::length(axis);
    if (axisLen < IK_EPSILON)
    {
        // Parallel or anti-parallel — pick an arbitrary perpendicular axis
        axis = glm::vec3(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(currentForward, axis)) > 0.99f)
        {
            axis = glm::vec3(1.0f, 0.0f, 0.0f);
        }
    }
    else
    {
        axis /= axisLen;
    }

    // Build correction in local space
    glm::quat invGlobal = glm::inverse(req.jointGlobalRot);
    glm::vec3 localAxis = invGlobal * axis;
    glm::quat correction = glm::angleAxis(angle, localAxis);

    glm::quat newLocalRot = req.jointLocalRot * correction;

    // Blend with weight
    if (req.weight < 1.0f)
    {
        result.localRot = nlerpShortest(req.jointLocalRot, newLocalRot, req.weight);
    }
    else
    {
        result.localRot = newLocalRot;
    }

    return result;
}

// ---------------------------------------------------------------------------
// Foot IK
// ---------------------------------------------------------------------------

FootIKResult solveFootIK(const FootIKRequest& req)
{
    FootIKResult result;
    result.hipLocalRot = req.hipLocalRot;
    result.kneeLocalRot = req.kneeLocalRot;
    result.ankleLocalRot = req.ankleLocalRot;
    result.pelvisOffset = 0.0f;

    if (req.weight <= 0.0f)
    {
        return result;
    }

    // 1. Compute pelvis offset — how much to lower the pelvis so the foot reaches the ground
    float currentAnkleY = req.anklePos.y;
    float targetY = req.groundPosition.y;
    result.pelvisOffset = targetY - currentAnkleY;

    // 2. Solve two-bone IK for the leg chain
    // Adjust target: the ankle should reach the ground position
    // The hip position will be offset by pelvisOffset
    glm::vec3 adjustedHipPos = req.hipPos + glm::vec3(0.0f, result.pelvisOffset, 0.0f);

    TwoBoneIKRequest legIK;
    legIK.startPos = adjustedHipPos;
    legIK.midPos = req.kneePos + glm::vec3(0.0f, result.pelvisOffset, 0.0f);
    legIK.endPos = req.anklePos + glm::vec3(0.0f, result.pelvisOffset, 0.0f);
    legIK.startGlobalRot = req.hipGlobalRot;
    legIK.midGlobalRot = req.kneeGlobalRot;
    legIK.startLocalRot = req.hipLocalRot;
    legIK.midLocalRot = req.kneeLocalRot;
    legIK.target = req.groundPosition;
    legIK.poleVector = req.kneeForward;
    legIK.weight = req.weight;

    TwoBoneIKResult legResult = solveTwoBoneIK(legIK);
    result.hipLocalRot = legResult.startLocalRot;
    result.kneeLocalRot = legResult.midLocalRot;

    // 3. Align ankle to ground normal
    // Rotate ankle so that its "up" direction matches the ground normal
    glm::vec3 currentUp = req.ankleGlobalRot * glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 targetUp = req.groundNormal;

    float alignAngle = safeAcos(glm::dot(currentUp, targetUp));
    if (alignAngle > IK_EPSILON)
    {
        glm::vec3 alignAxis = glm::cross(currentUp, targetUp);
        float alignAxisLen = glm::length(alignAxis);
        if (alignAxisLen > IK_EPSILON)
        {
            alignAxis /= alignAxisLen;
            glm::vec3 localAlignAxis = glm::inverse(req.ankleGlobalRot) * alignAxis;
            glm::quat ankleCorrection = glm::angleAxis(alignAngle, localAlignAxis);
            glm::quat newAnkleRot = req.ankleLocalRot * ankleCorrection;

            if (req.weight < 1.0f)
            {
                result.ankleLocalRot = nlerpShortest(req.ankleLocalRot, newAnkleRot, req.weight);
            }
            else
            {
                result.ankleLocalRot = newAnkleRot;
            }
        }
    }

    return result;
}

} // namespace Vestige
