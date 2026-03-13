/// @file test_aabb.cpp
/// @brief Unit tests for the AABB collision system.
#include "utils/aabb.h"

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

using namespace Vestige;

TEST(AABBTest, UnitCube)
{
    AABB box = AABB::unitCube();
    EXPECT_FLOAT_EQ(box.min.x, -0.5f);
    EXPECT_FLOAT_EQ(box.max.x, 0.5f);
    EXPECT_FLOAT_EQ(box.min.y, -0.5f);
    EXPECT_FLOAT_EQ(box.max.y, 0.5f);
}

TEST(AABBTest, FromCenterSize)
{
    AABB box = AABB::fromCenterSize(glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(2.0f, 4.0f, 6.0f));
    EXPECT_FLOAT_EQ(box.min.x, 0.0f);
    EXPECT_FLOAT_EQ(box.max.x, 2.0f);
    EXPECT_FLOAT_EQ(box.min.y, 0.0f);
    EXPECT_FLOAT_EQ(box.max.y, 4.0f);
    EXPECT_FLOAT_EQ(box.min.z, 0.0f);
    EXPECT_FLOAT_EQ(box.max.z, 6.0f);
}

TEST(AABBTest, GetCenterAndSize)
{
    AABB box = {glm::vec3(-1.0f), glm::vec3(3.0f)};
    glm::vec3 center = box.getCenter();
    glm::vec3 size = box.getSize();
    EXPECT_FLOAT_EQ(center.x, 1.0f);
    EXPECT_FLOAT_EQ(center.y, 1.0f);
    EXPECT_FLOAT_EQ(center.z, 1.0f);
    EXPECT_FLOAT_EQ(size.x, 4.0f);
    EXPECT_FLOAT_EQ(size.y, 4.0f);
    EXPECT_FLOAT_EQ(size.z, 4.0f);
}

TEST(AABBTest, IntersectsOverlapping)
{
    AABB a = {glm::vec3(0.0f), glm::vec3(2.0f)};
    AABB b = {glm::vec3(1.0f), glm::vec3(3.0f)};
    EXPECT_TRUE(a.intersects(b));
    EXPECT_TRUE(b.intersects(a));
}

TEST(AABBTest, IntersectsNotOverlapping)
{
    AABB a = {glm::vec3(0.0f), glm::vec3(1.0f)};
    AABB b = {glm::vec3(2.0f), glm::vec3(3.0f)};
    EXPECT_FALSE(a.intersects(b));
    EXPECT_FALSE(b.intersects(a));
}

TEST(AABBTest, IntersectsTouching)
{
    AABB a = {glm::vec3(0.0f), glm::vec3(1.0f)};
    AABB b = {glm::vec3(1.0f), glm::vec3(2.0f)};
    EXPECT_TRUE(a.intersects(b));
}

TEST(AABBTest, ContainsPoint)
{
    AABB box = {glm::vec3(0.0f), glm::vec3(2.0f)};
    EXPECT_TRUE(box.contains(glm::vec3(1.0f)));
    EXPECT_TRUE(box.contains(glm::vec3(0.0f)));     // On boundary
    EXPECT_FALSE(box.contains(glm::vec3(3.0f)));
    EXPECT_FALSE(box.contains(glm::vec3(-1.0f)));
}

TEST(AABBTest, TransformedByTranslation)
{
    AABB box = AABB::unitCube();
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 0.0f));
    AABB result = box.transformed(translation);
    EXPECT_NEAR(result.min.x, 4.5f, 0.001f);
    EXPECT_NEAR(result.max.x, 5.5f, 0.001f);
    EXPECT_NEAR(result.min.y, -0.5f, 0.001f);
    EXPECT_NEAR(result.max.y, 0.5f, 0.001f);
}

TEST(AABBTest, TransformedByScale)
{
    AABB box = AABB::unitCube();
    glm::mat4 scaling = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));
    AABB result = box.transformed(scaling);
    EXPECT_NEAR(result.min.x, -1.0f, 0.001f);
    EXPECT_NEAR(result.max.x, 1.0f, 0.001f);
}

TEST(AABBTest, MinPushOutResolvesCollision)
{
    AABB a = {glm::vec3(0.0f), glm::vec3(2.0f)};
    AABB b = {glm::vec3(1.5f, 0.0f, 0.0f), glm::vec3(3.5f, 2.0f, 2.0f)};
    glm::vec3 push = a.getMinPushOut(b);
    // Should push on X axis (smallest overlap = 0.5)
    EXPECT_NEAR(push.x, -0.5f, 0.001f);
    EXPECT_FLOAT_EQ(push.y, 0.0f);
    EXPECT_FLOAT_EQ(push.z, 0.0f);
}
