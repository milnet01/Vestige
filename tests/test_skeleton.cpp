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

// ---------------------------------------------------------------------------
// AUDIT A1 — buildUpdateOrder DFS pre-order tests
// ---------------------------------------------------------------------------

TEST(SkeletonTest, BuildUpdateOrderEmptySkeleton_A1)
{
    Skeleton skeleton;
    skeleton.buildUpdateOrder();
    EXPECT_TRUE(skeleton.m_updateOrder.empty());
}

TEST(SkeletonTest, BuildUpdateOrderParentBeforeChildInStorageOrder_A1)
{
    // Already-sorted skeleton: root → spine → head, parent index < child index.
    Skeleton skeleton;
    Joint root;  root.name = "Root";  root.parentIndex = -1;
    Joint spine; spine.name = "Spine"; spine.parentIndex = 0;
    Joint head;  head.name = "Head";  head.parentIndex = 1;
    skeleton.m_joints = {root, spine, head};
    skeleton.m_rootJoints = {0};

    skeleton.buildUpdateOrder();

    ASSERT_EQ(skeleton.m_updateOrder.size(), 3u);
    EXPECT_EQ(skeleton.m_updateOrder[0], 0);
    EXPECT_EQ(skeleton.m_updateOrder[1], 1);
    EXPECT_EQ(skeleton.m_updateOrder[2], 2);
}

TEST(SkeletonTest, BuildUpdateOrderShuffledStorage_A1)
{
    // Storage order: leaf, root, mid. parentIndex still wires correctly,
    // but iterating 0..N would visit the leaf before its parent.
    // buildUpdateOrder must reorder so root comes first.
    Skeleton skeleton;
    Joint leaf; leaf.name = "Leaf"; leaf.parentIndex = 2;  // mid
    Joint root; root.name = "Root"; root.parentIndex = -1;
    Joint mid;  mid.name  = "Mid";  mid.parentIndex  = 1;  // root
    skeleton.m_joints = {leaf, root, mid};
    skeleton.m_rootJoints = {1};

    skeleton.buildUpdateOrder();

    ASSERT_EQ(skeleton.m_updateOrder.size(), 3u);
    EXPECT_EQ(skeleton.m_updateOrder[0], 1);  // root first
    EXPECT_EQ(skeleton.m_updateOrder[1], 2);  // mid second
    EXPECT_EQ(skeleton.m_updateOrder[2], 0);  // leaf last
}

TEST(SkeletonTest, BuildUpdateOrderMultipleRoots_A1)
{
    // Two independent chains, two roots in m_rootJoints.
    Skeleton skeleton;
    Joint rA;   rA.parentIndex = -1;
    Joint cA;   cA.parentIndex = 0;
    Joint rB;   rB.parentIndex = -1;
    Joint cB;   cB.parentIndex = 2;
    skeleton.m_joints = {rA, cA, rB, cB};
    skeleton.m_rootJoints = {0, 2};

    skeleton.buildUpdateOrder();

    ASSERT_EQ(skeleton.m_updateOrder.size(), 4u);
    // rA's chain visited before rB's chain (in m_rootJoints order),
    // and within each chain the parent precedes its child.
    EXPECT_EQ(skeleton.m_updateOrder[0], 0);
    EXPECT_EQ(skeleton.m_updateOrder[1], 1);
    EXPECT_EQ(skeleton.m_updateOrder[2], 2);
    EXPECT_EQ(skeleton.m_updateOrder[3], 3);
}

TEST(SkeletonTest, BuildUpdateOrderIsIdempotent_A1)
{
    Skeleton skeleton;
    Joint root; root.parentIndex = -1;
    Joint mid;  mid.parentIndex = 0;
    Joint leaf; leaf.parentIndex = 1;
    skeleton.m_joints = {root, mid, leaf};
    skeleton.m_rootJoints = {0};

    skeleton.buildUpdateOrder();
    const std::vector<int> first = skeleton.m_updateOrder;
    skeleton.buildUpdateOrder();
    EXPECT_EQ(skeleton.m_updateOrder, first);
}

TEST(SkeletonTest, BuildUpdateOrderParentPositionLessThanChild_A1)
{
    // Wide DAG-ish tree to stress the invariant.
    Skeleton skeleton;
    skeleton.m_joints.resize(6);
    skeleton.m_joints[0].parentIndex = -1;
    skeleton.m_joints[1].parentIndex = 0;
    skeleton.m_joints[2].parentIndex = 0;
    skeleton.m_joints[3].parentIndex = 1;
    skeleton.m_joints[4].parentIndex = 2;
    skeleton.m_joints[5].parentIndex = 4;
    skeleton.m_rootJoints = {0};

    skeleton.buildUpdateOrder();

    ASSERT_EQ(skeleton.m_updateOrder.size(), 6u);
    std::vector<int> position(6, -1);
    for (size_t k = 0; k < skeleton.m_updateOrder.size(); ++k)
    {
        position[static_cast<size_t>(skeleton.m_updateOrder[k])]
            = static_cast<int>(k);
    }
    for (size_t i = 0; i < skeleton.m_joints.size(); ++i)
    {
        const int p = skeleton.m_joints[i].parentIndex;
        if (p >= 0)
        {
            EXPECT_LT(position[static_cast<size_t>(p)], position[i])
                << "parent " << p << " must precede child " << i;
        }
    }
}
