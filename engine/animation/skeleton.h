/// @file skeleton.h
/// @brief Joint hierarchy and skeleton data loaded from glTF skins.
#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Vestige
{

/// @brief A single joint in a skeletal hierarchy.
struct Joint
{
    std::string name;
    int parentIndex = -1;                           ///< Index into Skeleton::m_joints (-1 = root)
    glm::mat4 inverseBindMatrix = glm::mat4(1.0f);  ///< Mesh space → bone space
    glm::mat4 localBindTransform = glm::mat4(1.0f); ///< Default local transform (bind pose)
};

/// @brief A skeleton — a hierarchy of joints loaded from a glTF skin.
class Skeleton
{
public:
    /// @brief Gets the joint count.
    int getJointCount() const;

    /// @brief Finds a joint index by name. Returns -1 if not found.
    int findJoint(const std::string& name) const;

    /// @brief Maximum number of joints supported per skeleton.
    static constexpr int MAX_JOINTS = 128;

    std::vector<Joint> m_joints;
    std::vector<int> m_rootJoints;  ///< Indices of joints with parentIndex == -1
};

} // namespace Vestige
