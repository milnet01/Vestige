/// @file test_frustum_culling.cpp
/// @brief Unit tests for frustum plane extraction and AABB-vs-frustum culling.
#include "utils/frustum.h"

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

using namespace Vestige;

// --- extractFrustumPlanes tests ---

TEST(FrustumTest, IdentityMatrixProducesValidPlanes)
{
    // Identity VP = looking down -Z, clip space maps directly
    glm::mat4 identity(1.0f);
    auto planes = extractFrustumPlanes(identity);

    // All 6 planes should have non-zero normals
    for (const auto& p : planes)
    {
        float normalLen = glm::length(glm::vec3(p));
        EXPECT_GT(normalLen, 0.0f);
    }
}

TEST(FrustumTest, PerspectiveProjectionPlanes)
{
    // Standard perspective: 90 degree FOV, 1:1 aspect, near=0.1, far=100
    glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    auto planes = extractFrustumPlanes(proj * view);

    // The origin should be inside the frustum (it's at the camera position,
    // but the near plane is at z=-0.1, so origin is behind the near plane)
    // A point at (0, 0, -1) should be inside
    AABB insideBox = AABB::fromCenterSize(glm::vec3(0, 0, -1), glm::vec3(0.1f));
    EXPECT_TRUE(isAabbInFrustum(insideBox, planes));

    // A point far behind the camera should be outside
    AABB behindBox = AABB::fromCenterSize(glm::vec3(0, 0, 10), glm::vec3(0.1f));
    EXPECT_FALSE(isAabbInFrustum(behindBox, planes));
}

TEST(FrustumTest, OrthographicProjectionPlanes)
{
    // Orthographic: x=[-10,10], y=[-10,10], z=[-0.1,-100]
    glm::mat4 proj = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    auto planes = extractFrustumPlanes(proj * view);

    // Center of ortho volume should be inside
    AABB insideBox = AABB::fromCenterSize(glm::vec3(0, 0, -50), glm::vec3(1.0f));
    EXPECT_TRUE(isAabbInFrustum(insideBox, planes));

    // Point at x=20 should be outside (ortho x range is [-10, 10])
    AABB outsideRight = AABB::fromCenterSize(glm::vec3(20, 0, -50), glm::vec3(1.0f));
    EXPECT_FALSE(isAabbInFrustum(outsideRight, planes));

    // Point beyond far plane should be outside
    AABB outsideFar = AABB::fromCenterSize(glm::vec3(0, 0, -200), glm::vec3(1.0f));
    EXPECT_FALSE(isAabbInFrustum(outsideFar, planes));
}

// --- isAabbInFrustum tests ---

TEST(FrustumTest, BoxFullyInsideFrustum)
{
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto planes = extractFrustumPlanes(proj * view);

    // Box at origin, well within camera's view
    AABB box = AABB::fromCenterSize(glm::vec3(0, 0, 0), glm::vec3(1.0f));
    EXPECT_TRUE(isAabbInFrustum(box, planes));
}

TEST(FrustumTest, BoxFullyOutsideFrustum)
{
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto planes = extractFrustumPlanes(proj * view);

    // Box far to the left, outside the 60-degree FOV
    AABB box = AABB::fromCenterSize(glm::vec3(-100, 0, 0), glm::vec3(1.0f));
    EXPECT_FALSE(isAabbInFrustum(box, planes));
}

TEST(FrustumTest, BoxBehindCameraIsOutside)
{
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto planes = extractFrustumPlanes(proj * view);

    // Box behind the camera (camera is at z=5 looking toward z=0, so z=10 is behind)
    AABB box = AABB::fromCenterSize(glm::vec3(0, 0, 10), glm::vec3(1.0f));
    EXPECT_FALSE(isAabbInFrustum(box, planes));
}

TEST(FrustumTest, BoxBeyondFarPlaneIsOutside)
{
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 50.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto planes = extractFrustumPlanes(proj * view);

    // Box 200 units in front of camera (far plane is 50)
    AABB box = AABB::fromCenterSize(glm::vec3(0, 0, -200), glm::vec3(1.0f));
    EXPECT_FALSE(isAabbInFrustum(box, planes));
}

TEST(FrustumTest, BoxPartiallyIntersectingFrustumIsInside)
{
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto planes = extractFrustumPlanes(proj * view);

    // Large box that straddles the near plane
    AABB box = AABB::fromCenterSize(glm::vec3(0, 0, 5), glm::vec3(10.0f));
    EXPECT_TRUE(isAabbInFrustum(box, planes));
}

TEST(FrustumTest, LargeBoxContainingFrustumIsInside)
{
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto planes = extractFrustumPlanes(proj * view);

    // Enormous box that completely contains the entire frustum
    AABB box = AABB::fromCenterSize(glm::vec3(0, 0, 0), glm::vec3(1000.0f));
    EXPECT_TRUE(isAabbInFrustum(box, planes));
}

TEST(FrustumTest, ZeroSizeBoxAtOriginInsideFrustum)
{
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto planes = extractFrustumPlanes(proj * view);

    // Degenerate box (point) at origin — should be inside since origin is in view
    AABB box = AABB::fromCenterSize(glm::vec3(0, 0, 0), glm::vec3(0.0f));
    EXPECT_TRUE(isAabbInFrustum(box, planes));
}

TEST(FrustumTest, BoxAboveFrustumIsOutside)
{
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto planes = extractFrustumPlanes(proj * view);

    // Box very high up, outside the vertical FOV
    AABB box = AABB::fromCenterSize(glm::vec3(0, 100, 0), glm::vec3(1.0f));
    EXPECT_FALSE(isAabbInFrustum(box, planes));
}

// --- Shadow cascade frustum culling tests (orthographic projection) ---

TEST(FrustumTest, OrthoShadowCascadeCullsOutsideCasters)
{
    // Simulate a cascade looking down -Y (top-down shadow)
    glm::mat4 proj = glm::ortho(-20.0f, 20.0f, -20.0f, 20.0f, 0.1f, 50.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0, 25, 0), glm::vec3(0, 0, 0), glm::vec3(0, 0, -1));
    auto planes = extractFrustumPlanes(proj * view);

    // Object within the cascade volume
    AABB insideBox = AABB::fromCenterSize(glm::vec3(0, 5, 0), glm::vec3(2.0f));
    EXPECT_TRUE(isAabbInFrustum(insideBox, planes));

    // Object far outside cascade x-range
    AABB outsideBox = AABB::fromCenterSize(glm::vec3(50, 5, 0), glm::vec3(2.0f));
    EXPECT_FALSE(isAabbInFrustum(outsideBox, planes));
}

// --- Mesh auto-AABB computation test ---

TEST(MeshBoundsTest, UploadComputesLocalBounds)
{
    // This test uses Mesh without GPU context, so we can only test the
    // bounds are correctly set. The actual GPU upload will fail without
    // OpenGL context, but the bounds computation happens on the CPU.
    // Skipping this test since Mesh::upload requires an OpenGL context.
    // The AABB computation is tested implicitly via the full engine.
    GTEST_SKIP() << "Mesh::upload() requires OpenGL context";
}
