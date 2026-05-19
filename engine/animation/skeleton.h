// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

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

    /// @brief Builds m_updateOrder as a DFS pre-order traversal of the joint
    /// forest rooted at m_rootJoints. Each joint index appears exactly once,
    /// with every parent listed before its children — the invariant
    /// computeBoneMatrices relies on so a parent's global transform is ready
    /// before its children consume it. Call after m_joints + m_rootJoints
    /// are populated. Idempotent: safe to call again after edits.
    void buildUpdateOrder();

    /// @brief Maximum number of joints supported per skeleton.
    /// Must stay in lockstep with `Renderer::MAX_BONES` (renderer.h) — the
    /// renderer pre-allocates an SSBO of that size and the skinning vertex
    /// shaders (`id_buffer.vert.glsl`, `shadow_depth.vert.glsl`, etc.)
    /// dereference `u_boneMatrices[boneIds.*]` blindly. A `static_assert`
    /// in renderer.cpp pins the two together at compile time.
    static constexpr int MAX_JOINTS = 128;

    std::vector<Joint> m_joints;
    std::vector<int> m_rootJoints;  ///< Indices of joints with parentIndex == -1
    std::vector<int> m_updateOrder; ///< DFS pre-order; parent visited before children. Built by buildUpdateOrder().
};

} // namespace Vestige
