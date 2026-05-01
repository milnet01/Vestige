// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_spline_path.cpp
/// @brief Unit tests for SplinePath (Catmull-Rom spline evaluation and mesh generation).
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>

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

TEST(SplinePathTest, EvaluateByArcLengthEmpty)
{
    SplinePath path;
    glm::vec3 p = path.evaluateByArcLength(0.0f);
    EXPECT_NEAR(p.x, 0.0f, 0.01f);
    EXPECT_NEAR(p.y, 0.0f, 0.01f);
    EXPECT_NEAR(p.z, 0.0f, 0.01f);
}

TEST(SplinePathTest, EvaluateByArcLengthSinglePoint)
{
    SplinePath path;
    path.addWaypoint(glm::vec3(7.0f, 2.0f, -3.0f));

    // Any s collapses to the single waypoint.
    glm::vec3 p0 = path.evaluateByArcLength(0.0f);
    glm::vec3 p1 = path.evaluateByArcLength(5.0f);
    glm::vec3 pNeg = path.evaluateByArcLength(-1.0f);

    for (const glm::vec3& p : {p0, p1, pNeg})
    {
        EXPECT_NEAR(p.x, 7.0f, 0.01f);
        EXPECT_NEAR(p.y, 2.0f, 0.01f);
        EXPECT_NEAR(p.z, -3.0f, 0.01f);
    }
}

TEST(SplinePathTest, EvaluateByArcLengthLinearMidpoint)
{
    SplinePath path;
    path.addWaypoint(glm::vec3(0.0f, 0.0f, 0.0f));
    path.addWaypoint(glm::vec3(10.0f, 0.0f, 0.0f));

    // Two-point spline degenerates to a straight line of length 10.
    glm::vec3 start = path.evaluateByArcLength(0.0f);
    EXPECT_NEAR(start.x, 0.0f, 0.05f);

    glm::vec3 mid = path.evaluateByArcLength(5.0f);
    EXPECT_NEAR(mid.x, 5.0f, 0.05f);

    glm::vec3 end = path.evaluateByArcLength(10.0f);
    EXPECT_NEAR(end.x, 10.0f, 0.05f);
}

TEST(SplinePathTest, EvaluateByArcLengthClampsBeyondEnd)
{
    SplinePath path;
    path.addWaypoint(glm::vec3(0.0f, 0.0f, 0.0f));
    path.addWaypoint(glm::vec3(10.0f, 0.0f, 0.0f));

    // s past total length clamps to the endpoint.
    glm::vec3 past = path.evaluateByArcLength(1000.0f);
    EXPECT_NEAR(past.x, 10.0f, 0.05f);
    EXPECT_NEAR(past.z, 0.0f, 0.05f);
}

TEST(SplinePathTest, EvaluateByArcLengthClampsNegative)
{
    SplinePath path;
    // Non-origin start so a stub that returns (0,0,0) wouldn't pass.
    path.addWaypoint(glm::vec3(4.0f, 1.0f, -2.0f));
    path.addWaypoint(glm::vec3(14.0f, 1.0f, -2.0f));

    // Negative s clamps to the start.
    glm::vec3 before = path.evaluateByArcLength(-5.0f);
    EXPECT_NEAR(before.x, 4.0f, 0.05f);
    EXPECT_NEAR(before.y, 1.0f, 0.05f);
    EXPECT_NEAR(before.z, -2.0f, 0.05f);
}

TEST(SplinePathTest, EvaluateByArcLengthConstantSpeed)
{
    // The contract: stepping s by a fixed amount moves the same arc
    // distance along the curve regardless of local curvature. Phase
    // 10.8 CM7 cinematic camera relies on this for constant-speed
    // playback through curved sections.
    //
    // Differential check: on a curve where t-to-arc is markedly
    // non-uniform (centripetal CR with unequal segment chord lengths),
    // sample 20 points two ways — by uniform-t and by arc-length —
    // and compare chord-distance spread. Arc-length spacing must be
    // substantially tighter than uniform-t.
    //
    // This avoids assuming chord ≈ arc (which fails on curvy splines)
    // — it asserts only the relative property that actually matters
    // to the consumer.
    SplinePath path;
    path.addWaypoint(glm::vec3(0.0f, 0.0f, 0.0f));
    path.addWaypoint(glm::vec3(1.0f, 0.0f, 1.0f));
    path.addWaypoint(glm::vec3(9.0f, 0.0f, 1.0f));
    path.addWaypoint(glm::vec3(10.0f, 0.0f, 0.0f));

    const float total = path.getLength();
    ASSERT_GT(total, 1.0f);

    constexpr int kSteps = 20;

    auto chordSpread = [&](auto&& sampler) {
        glm::vec3 prev = sampler(0.0f);
        float minStep = std::numeric_limits<float>::infinity();
        float maxStep = 0.0f;
        for (int i = 1; i <= kSteps; ++i)
        {
            const float u = static_cast<float>(i) / static_cast<float>(kSteps);
            glm::vec3 curr = sampler(u);
            const float d = glm::distance(prev, curr);
            minStep = std::min(minStep, d);
            maxStep = std::max(maxStep, d);
            prev = curr;
        }
        // Catches a stub that returns the same point every call.
        EXPECT_GT(maxStep, 0.01f);
        return (maxStep > 0.0f) ? (maxStep - minStep) / maxStep : 0.0f;
    };

    const float uSpread = chordSpread([&](float u) { return path.evaluate(u); });
    const float aSpread = chordSpread(
        [&](float u) { return path.evaluateByArcLength(u * total); });

    // Arc-length spread must be at least 2x tighter than uniform-t —
    // the precise margin depends on curvature, but on this setup
    // uniform-t hits ~0.7 spread while arc-length stays under ~0.1.
    EXPECT_LT(aSpread, uSpread * 0.5f)
        << "arc-length spread=" << aSpread
        << " should be < half uniform-t spread " << uSpread;
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
