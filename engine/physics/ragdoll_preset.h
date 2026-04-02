/// @file ragdoll_preset.h
/// @brief Ragdoll preset definitions — body shapes, joint limits, and mass per bone.
#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Vestige
{

class Skeleton;

/// @brief Collision shape type for a ragdoll body part.
enum class RagdollShapeType : uint8_t
{
    CAPSULE,
    SPHERE,
    BOX
};

/// @brief Defines the physical properties of a single ragdoll joint/body part.
struct RagdollJointDef
{
    std::string boneName;
    RagdollShapeType shapeType = RagdollShapeType::CAPSULE;

    /// Shape dimensions:
    /// - CAPSULE: (radius, halfHeight, 0)
    /// - SPHERE:  (radius, 0, 0)
    /// - BOX:     (halfX, halfY, halfZ)
    glm::vec3 shapeSize = glm::vec3(0.05f, 0.1f, 0.0f);

    /// Local offset from the bone origin (bone-space).
    glm::vec3 shapeOffset = glm::vec3(0.0f);

    float mass = 1.0f;

    // SwingTwist constraint limits (radians)
    float normalHalfCone = 0.0f;   ///< Swing limit around the normal axis
    float planeHalfCone = 0.0f;    ///< Swing limit in the plane
    float twistMin = 0.0f;         ///< Minimum twist angle
    float twistMax = 0.0f;         ///< Maximum twist angle
    float maxFrictionTorque = 0.0f;
};

/// @brief A named collection of RagdollJointDefs that defines a complete ragdoll.
///
/// Only bones listed in the preset get physics bodies. Parent-child
/// constraint topology follows the skeleton hierarchy.
struct RagdollPreset
{
    std::string name;
    std::vector<RagdollJointDef> joints;

    /// @brief Creates a standard 15-joint humanoid ragdoll preset.
    /// Expects a skeleton with bones named: Hips, Spine, Chest, Head,
    /// LeftUpperArm, LeftLowerArm, LeftHand, RightUpperArm, RightLowerArm,
    /// RightHand, LeftUpperLeg, LeftLowerLeg, LeftFoot, RightUpperLeg,
    /// RightLowerLeg, RightFoot.
    static RagdollPreset createHumanoid();

    /// @brief Creates a minimal ragdoll with uniform capsules (for testing).
    /// @param skeleton The skeleton to create bodies for (all joints get bodies).
    static RagdollPreset createSimple(const Skeleton& skeleton);
};

} // namespace Vestige
