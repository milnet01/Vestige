// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file skeleton.cpp
/// @brief Skeleton implementation.
#include "animation/skeleton.h"

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

} // namespace Vestige
