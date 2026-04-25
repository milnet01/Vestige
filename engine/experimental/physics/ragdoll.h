// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ragdoll.h
/// @brief Ragdoll system — wraps Jolt's native ragdoll with Vestige skeleton conversion.
#pragma once

#include "experimental/physics/ragdoll_preset.h"
#include "physics/physics_world.h"
#include "animation/skeleton.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Reference.h>
#include <Jolt/Physics/Ragdoll/Ragdoll.h>
#include <Jolt/Skeleton/Skeleton.h>
#include <Jolt/Skeleton/SkeletonPose.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <vector>

namespace Vestige
{

/// @brief Ragdoll operating state.
enum class RagdollState : uint8_t
{
    INACTIVE,     ///< Not in physics world
    ACTIVE,       ///< Full physics simulation (limp)
    POWERED,      ///< Motors drive toward animation pose
    KINEMATIC     ///< Bodies moved kinematically to match animation exactly
};

/// @brief Manages a Jolt ragdoll instance and converts between Vestige and Jolt skeletons.
///
/// Usage:
/// 1. Call create() with a skeleton, preset, and current bone world matrices
/// 2. Call activate() to start physics simulation
/// 3. Call getBoneMatrices() each frame for rendering
/// 4. Call driveToPose() for powered ragdoll mode
/// 5. Call deactivate() or destroy() when done
class Ragdoll
{
public:
    Ragdoll() = default;
    ~Ragdoll();

    // Non-copyable
    Ragdoll(const Ragdoll&) = delete;
    Ragdoll& operator=(const Ragdoll&) = delete;

    /// @brief Creates the ragdoll in the physics world from a skeleton and preset.
    /// @param world The physics world to add bodies to.
    /// @param skeleton The Vestige skeleton (bone hierarchy).
    /// @param preset Defines shapes, masses, and joint limits for each bone.
    /// @param boneWorldMatrices Current world-space bone transforms (from SkeletonAnimator).
    /// @return True if creation succeeded.
    bool create(PhysicsWorld& world, const Skeleton& skeleton,
                const RagdollPreset& preset,
                const std::vector<glm::mat4>& boneWorldMatrices);

    /// @brief Removes the ragdoll from the physics world and releases resources.
    void destroy();

    /// @brief Activates ragdoll for full physics simulation (go limp).
    void activate();

    /// @brief Deactivates ragdoll (removes from physics, returns to INACTIVE).
    void deactivate();

    /// @brief Drives ragdoll bodies toward target animation matrices using motors.
    /// @param targetWorldMatrices Target bone world matrices (joint * inverseBindMatrix format NOT needed,
    ///        pass raw global joint transforms).
    /// @param deltaTime Physics timestep.
    void driveToPose(const std::vector<glm::mat4>& targetWorldMatrices, float deltaTime);

    /// @brief Sets the motor strength for powered ragdoll mode.
    /// @param strength 0 = completely limp, 1 = full motor tracking.
    void setMotorStrength(float strength);

    /// @brief Gets the current motor strength.
    float getMotorStrength() const { return m_motorStrength; }

    /// @brief Sets a specific body part to dynamic or kinematic for partial ragdoll.
    /// @param jointIndex Index into the Vestige skeleton.
    /// @param dynamic True = physics-driven, false = kinematic (animation-driven).
    void setPartMotionType(int jointIndex, bool dynamic);

    /// @brief Gets the bone matrices for skinned rendering.
    /// Output matches SkeletonAnimator format: globalTransform * inverseBindMatrix.
    /// @param outMatrices Receives one matrix per skeleton joint.
    void getBoneMatrices(std::vector<glm::mat4>& outMatrices) const;

    /// @brief Gets the world-space root position of the ragdoll.
    glm::vec3 getRootPosition() const;

    /// @brief Gets the world-space root rotation of the ragdoll.
    glm::quat getRootRotation() const;

    /// @brief Returns the current ragdoll state.
    RagdollState getState() const { return m_state; }

    /// @brief Returns true if the ragdoll has been created.
    bool isCreated() const { return m_joltRagdoll != nullptr; }

    /// @brief Returns true if any ragdoll body is awake.
    bool isPhysicsActive() const;

    /// @brief Gets the number of physics bodies in the ragdoll.
    int getBodyCount() const;

    /// @brief Gets the Jolt body ID for a specific skeleton joint.
    /// @return Invalid BodyID if the joint has no ragdoll body.
    JPH::BodyID getBodyIdForJoint(int jointIndex) const;

    /// @brief Applies an impulse to a specific ragdoll body.
    /// @param jointIndex Skeleton joint index.
    /// @param impulse World-space impulse.
    /// @param worldPoint World-space application point.
    void applyImpulseToJoint(int jointIndex, const glm::vec3& impulse,
                              const glm::vec3& worldPoint);

    /// @brief Sets initial velocities from animation bone velocities.
    /// Call right after activate() for smooth animation-to-ragdoll transition.
    /// @param boneVelocities Per-joint linear velocities in world space.
    void setInitialVelocities(const std::vector<glm::vec3>& boneVelocities);

private:
    /// @brief Builds a JPH::Skeleton from a Vestige::Skeleton.
    JPH::Ref<JPH::Skeleton> buildJoltSkeleton(const Skeleton& skeleton) const;

    /// @brief Builds RagdollSettings from the preset and skeleton.
    JPH::Ref<JPH::RagdollSettings> buildSettings(
        const JPH::Skeleton* jphSkeleton,
        const Skeleton& skeleton,
        const RagdollPreset& preset,
        const std::vector<glm::mat4>& boneWorldMatrices) const;

    /// @brief Creates a Jolt shape from a RagdollJointDef.
    JPH::Ref<JPH::Shape> createShape(const RagdollJointDef& def) const;

    /// @brief Converts Vestige bone world matrices to a JPH::SkeletonPose.
    void toJoltPose(const std::vector<glm::mat4>& boneWorldMatrices,
                    JPH::SkeletonPose& outPose) const;

    PhysicsWorld* m_world = nullptr;
    const Skeleton* m_skeleton = nullptr;
    RagdollState m_state = RagdollState::INACTIVE;
    float m_motorStrength = 1.0f;

    // Jolt ragdoll objects (ref-counted via JPH::Ref)
    JPH::Ref<JPH::Ragdoll> m_joltRagdoll;
    JPH::Ref<JPH::RagdollSettings> m_settings;
    JPH::Ref<JPH::Skeleton> m_joltSkeleton;

    // Mapping: Vestige joint index -> ragdoll part index (-1 if no body)
    std::vector<int> m_jointToPartIndex;

    // Mapping: ragdoll part index -> Vestige joint index
    std::vector<int> m_partToJointIndex;

    // Collision group counter for ragdoll instances
    static uint32_t s_nextGroupId;
};

} // namespace Vestige
