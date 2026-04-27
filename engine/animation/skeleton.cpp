// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file skeleton.cpp
/// @brief Skeleton implementation.
#include "animation/skeleton.h"

#include <cassert>
#include <vector>

namespace Vestige
{

int Skeleton::getJointCount() const
{
    return static_cast<int>(m_joints.size());
}

int Skeleton::findJoint(const std::string& name) const
{
    for (int i = 0; i < static_cast<int>(m_joints.size()); ++i)
    {
        if (m_joints[static_cast<size_t>(i)].name == name)
        {
            return i;
        }
    }
    return -1;
}

// AUDIT A1 — DFS pre-order build. glTF does not require skin.joints to be
// listed parent-before-child; computeBoneMatrices used to assume that
// ordering and produced silent corruption when an exporter wrote a leaf
// before its parent. m_updateOrder makes the iteration order explicit and
// independent of the storage order in m_joints.
void Skeleton::buildUpdateOrder()
{
    const int n = static_cast<int>(m_joints.size());
    m_updateOrder.clear();
    m_updateOrder.reserve(static_cast<size_t>(n));

    if (n == 0)
    {
        return;
    }

    std::vector<std::vector<int>> children(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        const int p = m_joints[static_cast<size_t>(i)].parentIndex;
        if (p >= 0 && p < n)
        {
            children[static_cast<size_t>(p)].push_back(i);
        }
    }

    std::vector<char> visited(static_cast<size_t>(n), 0);
    std::vector<int> stack;
    stack.reserve(static_cast<size_t>(n));

    for (int root : m_rootJoints)
    {
        if (root < 0 || root >= n)
        {
            continue;
        }
        stack.push_back(root);
        while (!stack.empty())
        {
            const int j = stack.back();
            stack.pop_back();
            if (visited[static_cast<size_t>(j)])
            {
                continue;  // Cycle / multi-parent — skip the duplicate.
            }
            visited[static_cast<size_t>(j)] = 1;
            m_updateOrder.push_back(j);
            const auto& kids = children[static_cast<size_t>(j)];
            for (auto it = kids.rbegin(); it != kids.rend(); ++it)
            {
                stack.push_back(*it);
            }
        }
    }

    // Defensive: any joint not reachable from a root (orphan parent index
    // out of range, or cycle) appended at the tail in storage order so the
    // caller still iterates every joint exactly once.
    for (int i = 0; i < n; ++i)
    {
        if (!visited[static_cast<size_t>(i)])
        {
            m_updateOrder.push_back(i);
        }
    }

#ifndef NDEBUG
    // Invariant: parent's position in m_updateOrder < its own position.
    std::vector<int> position(static_cast<size_t>(n), -1);
    for (size_t k = 0; k < m_updateOrder.size(); ++k)
    {
        position[static_cast<size_t>(m_updateOrder[k])] = static_cast<int>(k);
    }
    for (int i = 0; i < n; ++i)
    {
        const int p = m_joints[static_cast<size_t>(i)].parentIndex;
        if (p >= 0 && p < n)
        {
            assert(position[static_cast<size_t>(p)]
                 < position[static_cast<size_t>(i)]
                 && "Skeleton::buildUpdateOrder: parent must precede child");
        }
    }
#endif
}

} // namespace Vestige
