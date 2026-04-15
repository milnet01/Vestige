// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_skeleton.cpp
/// @brief Unit tests for Skeleton and Joint.
#include "animation/skeleton.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>

using namespace Vestige;

TEST(SkeletonTest, EmptySkeletonHasZeroJoints)
{
    Skeleton skeleton;
    EXPECT_EQ(skeleton.getJointCount(), 0);
}

TEST(SkeletonTest, JointCountMatchesVector)
{
    Skeleton skeleton;
    skeleton.m_joints.resize(5);
    EXPECT_EQ(skeleton.getJointCount(), 5);
}

TEST(SkeletonTest, FindJointByName)
{
    Skeleton skeleton;

    Joint root;
    root.name = "Root";
    root.parentIndex = -1;

    Joint spine;
    spine.name = "Spine";
    spine.parentIndex = 0;

    Joint head;
    head.name = "Head";
    head.parentIndex = 1;

    skeleton.m_joints = {root, spine, head};

    EXPECT_EQ(skeleton.findJoint("Root"), 0);
    EXPECT_EQ(skeleton.findJoint("Spine"), 1);
    EXPECT_EQ(skeleton.findJoint("Head"), 2);
    EXPECT_EQ(skeleton.findJoint("NonExistent"), -1);
}

TEST(SkeletonTest, DefaultJointValues)
{
    Joint joint;
    EXPECT_EQ(joint.parentIndex, -1);
    EXPECT_EQ(joint.inverseBindMatrix, glm::mat4(1.0f));
    EXPECT_EQ(joint.localBindTransform, glm::mat4(1.0f));
    EXPECT_TRUE(joint.name.empty());
}

TEST(SkeletonTest, MaxJointsConstant)
{
    EXPECT_EQ(Skeleton::MAX_JOINTS, 128);
}

TEST(SkeletonTest, RootJointsTracking)
{
    Skeleton skeleton;

    Joint root1;
    root1.name = "Root1";
    root1.parentIndex = -1;

    Joint child;
    child.name = "Child";
    child.parentIndex = 0;

    Joint root2;
    root2.name = "Root2";
    root2.parentIndex = -1;

    skeleton.m_joints = {root1, child, root2};
    skeleton.m_rootJoints = {0, 2};

    EXPECT_EQ(skeleton.m_rootJoints.size(), 2u);
    EXPECT_EQ(skeleton.m_rootJoints[0], 0);
    EXPECT_EQ(skeleton.m_rootJoints[1], 2);
}
