// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ragdoll_preset.cpp
/// @brief Ragdoll preset definitions implementation.

#include "physics/ragdoll_preset.h"
#include "animation/skeleton.h"

#include <glm/gtc/constants.hpp>

namespace Vestige
{

RagdollPreset RagdollPreset::createHumanoid()
{
    RagdollPreset preset;
    preset.name = "Humanoid";

    auto deg = [](float d) { return glm::radians(d); };

    // Helper to add a joint definition
    auto addJoint = [&](const std::string& name, RagdollShapeType shape,
                        glm::vec3 size, float mass,
                        float normalCone, float planeCone,
                        float twistMin, float twistMax,
                        float friction = 0.0f,
                        glm::vec3 offset = glm::vec3(0.0f))
    {
        RagdollJointDef def;
        def.boneName = name;
        def.shapeType = shape;
        def.shapeSize = size;
        def.shapeOffset = offset;
        def.mass = mass;
        def.normalHalfCone = deg(normalCone);
        def.planeHalfCone = deg(planeCone);
        def.twistMin = deg(twistMin);
        def.twistMax = deg(twistMax);
        def.maxFrictionTorque = friction;
        preset.joints.push_back(def);
    };

    // Root — Hips
    addJoint("Hips", RagdollShapeType::BOX,
             glm::vec3(0.15f, 0.10f, 0.10f), 15.0f,
             15.0f, 15.0f, -10.0f, 10.0f, 1.0f);

    // Spine
    addJoint("Spine", RagdollShapeType::BOX,
             glm::vec3(0.12f, 0.12f, 0.08f), 10.0f,
             15.0f, 15.0f, -10.0f, 10.0f, 0.5f);

    // Chest
    addJoint("Chest", RagdollShapeType::BOX,
             glm::vec3(0.15f, 0.14f, 0.10f), 12.0f,
             15.0f, 15.0f, -15.0f, 15.0f, 0.5f);

    // Head
    addJoint("Head", RagdollShapeType::SPHERE,
             glm::vec3(0.10f, 0.0f, 0.0f), 5.0f,
             40.0f, 40.0f, -45.0f, 45.0f, 0.2f);

    // Left upper arm
    addJoint("LeftUpperArm", RagdollShapeType::CAPSULE,
             glm::vec3(0.04f, 0.12f, 0.0f), 3.0f,
             80.0f, 45.0f, -45.0f, 45.0f);

    // Left lower arm (hinge-like: 0 normal cone, large plane cone)
    addJoint("LeftLowerArm", RagdollShapeType::CAPSULE,
             glm::vec3(0.035f, 0.12f, 0.0f), 2.0f,
             0.0f, 120.0f, -45.0f, 45.0f);

    // Left hand
    addJoint("LeftHand", RagdollShapeType::BOX,
             glm::vec3(0.04f, 0.02f, 0.06f), 0.5f,
             30.0f, 30.0f, -20.0f, 20.0f);

    // Right upper arm
    addJoint("RightUpperArm", RagdollShapeType::CAPSULE,
             glm::vec3(0.04f, 0.12f, 0.0f), 3.0f,
             80.0f, 45.0f, -45.0f, 45.0f);

    // Right lower arm
    addJoint("RightLowerArm", RagdollShapeType::CAPSULE,
             glm::vec3(0.035f, 0.12f, 0.0f), 2.0f,
             0.0f, 120.0f, -45.0f, 45.0f);

    // Right hand
    addJoint("RightHand", RagdollShapeType::BOX,
             glm::vec3(0.04f, 0.02f, 0.06f), 0.5f,
             30.0f, 30.0f, -20.0f, 20.0f);

    // Left upper leg
    addJoint("LeftUpperLeg", RagdollShapeType::CAPSULE,
             glm::vec3(0.06f, 0.18f, 0.0f), 8.0f,
             45.0f, 45.0f, -30.0f, 30.0f);

    // Left lower leg (hinge-like)
    addJoint("LeftLowerLeg", RagdollShapeType::CAPSULE,
             glm::vec3(0.05f, 0.18f, 0.0f), 5.0f,
             0.0f, 130.0f, -15.0f, 15.0f);

    // Left foot
    addJoint("LeftFoot", RagdollShapeType::BOX,
             glm::vec3(0.05f, 0.03f, 0.10f), 1.0f,
             25.0f, 25.0f, -10.0f, 10.0f);

    // Right upper leg
    addJoint("RightUpperLeg", RagdollShapeType::CAPSULE,
             glm::vec3(0.06f, 0.18f, 0.0f), 8.0f,
             45.0f, 45.0f, -30.0f, 30.0f);

    // Right lower leg
    addJoint("RightLowerLeg", RagdollShapeType::CAPSULE,
             glm::vec3(0.05f, 0.18f, 0.0f), 5.0f,
             0.0f, 130.0f, -15.0f, 15.0f);

    // Right foot
    addJoint("RightFoot", RagdollShapeType::BOX,
             glm::vec3(0.05f, 0.03f, 0.10f), 1.0f,
             25.0f, 25.0f, -10.0f, 10.0f);

    return preset;
}

RagdollPreset RagdollPreset::createSimple(const Skeleton& skeleton)
{
    RagdollPreset preset;
    preset.name = "Simple";

    int jointCount = skeleton.getJointCount();
    for (int i = 0; i < jointCount; ++i)
    {
        RagdollJointDef def;
        def.boneName = skeleton.m_joints[static_cast<size_t>(i)].name;
        def.shapeType = RagdollShapeType::CAPSULE;
        def.shapeSize = glm::vec3(0.05f, 0.08f, 0.0f);
        def.mass = 2.0f;
        def.normalHalfCone = glm::radians(30.0f);
        def.planeHalfCone = glm::radians(30.0f);
        def.twistMin = glm::radians(-20.0f);
        def.twistMax = glm::radians(20.0f);
        preset.joints.push_back(def);
    }

    return preset;
}

} // namespace Vestige
