// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_spline_path.cpp
/// @brief Unit tests for SplinePath (Catmull-Rom spline evaluation and mesh generation).
#include <gtest/gtest.h>

#include <cmath>

#include "environment/spline_path.h"

using namespace Vestige;

TEST(SplinePathTest, EmptySpline)
{
    SplinePath path;
    EXPECT_EQ(path.getWaypointCount(), 0);
    EXPECT_FLOAT_EQ(path.getLength(), 0.0f);
}

TEST(SplinePathTest, SinglePoint)
{
    SplinePath path;
    path.addWaypoint(glm::vec3(5.0f, 0.0f, 5.0f));

    glm::vec3 p = path.evaluate(0.5f);
    EXPECT_NEAR(p.x, 5.0f, 0.01f);
    EXPECT_NEAR(p.z, 5.0f, 0.01f);
}

TEST(SplinePathTest, TwoPointLinear)
{
    SplinePath path;
    path.addWaypoint(glm::vec3(0.0f, 0.0f, 0.0f));
    path.addWaypoint(glm::vec3(10.0f, 0.0f, 0.0f));

    // At t=0, should be at start
    glm::vec3 start = path.evaluate(0.0f);
    EXPECT_NEAR(start.x, 0.0f, 0.01f);

    // At t=1, should be at end
    glm::vec3 end = path.evaluate(1.0f);
    EXPECT_NEAR(end.x, 10.0f, 0.01f);

    // At t=0.5, should be in the middle
    glm::vec3 mid = path.evaluate(0.5f);
    EXPECT_NEAR(mid.x, 5.0f, 0.01f);

    // Length should be ~10
    EXPECT_NEAR(path.getLength(), 10.0f, 0.1f);
}

TEST(SplinePathTest, ThreePointSpline)
{
    SplinePath path;
    path.addWaypoint(glm::vec3(0.0f, 0.0f, 0.0f));
    path.addWaypoint(glm::vec3(5.0f, 0.0f, 5.0f));
    path.addWaypoint(glm::vec3(10.0f, 0.0f, 0.0f));

    // The spline should pass through the middle point
    glm::vec3 mid = path.evaluate(0.5f);
    EXPECT_NEAR(mid.x, 5.0f, 0.2f);
    EXPECT_NEAR(mid.z, 5.0f, 0.2f);

    // Start and end should match
    glm::vec3 start = path.evaluate(0.0f);
    EXPECT_NEAR(start.x, 0.0f, 0.01f);
    glm::vec3 end = path.evaluate(1.0f);
    EXPECT_NEAR(end.x, 10.0f, 0.01f);
}

TEST(SplinePathTest, EvaluateTangent)
{
    SplinePath path;
    path.addWaypoint(glm::vec3(0.0f, 0.0f, 0.0f));
    path.addWaypoint(glm::vec3(10.0f, 0.0f, 0.0f));

    glm::vec3 tangent = path.evaluateTangent(0.5f);
    // Should point in the +X direction
    EXPECT_GT(tangent.x, 0.9f);
    // Should be approximately normalized
    EXPECT_NEAR(glm::length(tangent), 1.0f, 0.01f);
}

TEST(SplinePathTest, InsertAndRemoveWaypoints)
{
    SplinePath path;
    path.addWaypoint(glm::vec3(0.0f));
    path.addWaypoint(glm::vec3(10.0f, 0.0f, 0.0f));

    // Insert a waypoint at index 1
    path.insertWaypoint(1, glm::vec3(5.0f, 0.0f, 5.0f));
    EXPECT_EQ(path.getWaypointCount(), 3);
    EXPECT_NEAR(path.getWaypoints()[1].x, 5.0f, 0.01f);

    // Remove it
    path.removeWaypoint(1);
    EXPECT_EQ(path.getWaypointCount(), 2);
}

TEST(SplinePathTest, GeneratePathMesh)
{
    SplinePath path;
    path.addWaypoint(glm::vec3(0.0f, 0.0f, 0.0f));
    path.addWaypoint(glm::vec3(5.0f, 0.0f, 5.0f));
    path.addWaypoint(glm::vec3(10.0f, 0.0f, 0.0f));

    PathMeshData mesh = path.generatePathMesh(1.0f, 1.0f);

    EXPECT_GT(mesh.positions.size(), 0u);
    EXPECT_GT(mesh.indices.size(), 0u);
    EXPECT_EQ(mesh.positions.size(), mesh.normals.size());
    EXPECT_EQ(mesh.positions.size(), mesh.uvs.size());

    // Should have pairs of vertices (left/right edges)
    EXPECT_EQ(mesh.positions.size() % 2, 0u);

    // Indices should form valid triangles (multiples of 3)
    EXPECT_EQ(mesh.indices.size() % 3, 0u);
}

TEST(SplinePathTest, SerializeDeserialize)
{
    SplinePath path;
    path.name = "Garden Path";
    path.width = 2.0f;
    path.addWaypoint(glm::vec3(0.0f, 0.0f, 0.0f));
    path.addWaypoint(glm::vec3(5.0f, 0.0f, 5.0f));
    path.addWaypoint(glm::vec3(10.0f, 0.0f, 0.0f));

    nlohmann::json j = path.serialize();

    SplinePath loaded;
    loaded.deserialize(j);

    EXPECT_EQ(loaded.name, "Garden Path");
    EXPECT_FLOAT_EQ(loaded.width, 2.0f);
    EXPECT_EQ(loaded.getWaypointCount(), 3);
    EXPECT_NEAR(loaded.getWaypoints()[1].x, 5.0f, 0.01f);
    EXPECT_NEAR(loaded.getWaypoints()[1].z, 5.0f, 0.01f);
}

TEST(SplinePathTest, CentripetalAvoidsCusp)
{
    // Yuksel et al. 2011, "Parameterization and Applications of Catmull-Rom
    // Curves" (Computer-Aided Design 43.7) — uniform Catmull-Rom overshoots
    // and can self-intersect when adjacent control-point spacing is uneven;
    // centripetal (alpha = 0.5) provably does not.
    //
    // Setup: inner pair p1, p2 share x = 0, separated only in z. Outer pair
    // p0, p3 sit 10 units away in x. The segment between p1 and p2 must run
    // (roughly) along the z-axis without bulging out in x.
    //
    // With uniform parameterisation, x reaches ~0.47 near local t = 0.25 in
    // the middle segment. With centripetal, |x| stays < 0.1 throughout.
    SplinePath path;
    path.addWaypoint(glm::vec3(-10.0f, 0.0f, 0.0f));
    path.addWaypoint(glm::vec3(  0.0f, 0.0f, 0.0f));
    path.addWaypoint(glm::vec3(  0.0f, 0.0f, 1.0f));
    path.addWaypoint(glm::vec3( 10.0f, 0.0f, 1.0f));

    // Sample the middle segment (global t in [1/3, 2/3]) at 9 interior points.
    for (int i = 1; i < 10; ++i)
    {
        float localT = static_cast<float>(i) / 10.0f;
        float globalT = (1.0f + localT) / 3.0f;
        glm::vec3 p = path.evaluate(globalT);
        EXPECT_LT(std::abs(p.x), 0.15f)
            << "x-overshoot at localT=" << localT << " (x=" << p.x << ")";
    }
}

TEST(SplinePathTest, FourPointCurve)
{
    SplinePath path;
    path.addWaypoint(glm::vec3(0.0f, 0.0f, 0.0f));
    path.addWaypoint(glm::vec3(3.0f, 0.0f, 3.0f));
    path.addWaypoint(glm::vec3(7.0f, 0.0f, 3.0f));
    path.addWaypoint(glm::vec3(10.0f, 0.0f, 0.0f));

    // The curve should be smooth — evaluate at many points and check continuity
    glm::vec3 prev = path.evaluate(0.0f);
    for (int i = 1; i <= 100; ++i)
    {
        float t = static_cast<float>(i) / 100.0f;
        glm::vec3 curr = path.evaluate(t);
        float dist = glm::distance(prev, curr);
        // No point should jump more than 0.5m at this resolution
        EXPECT_LT(dist, 0.5f) << "Discontinuity at t=" << t;
        prev = curr;
    }
}
