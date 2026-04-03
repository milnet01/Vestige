/// @file test_catmull_rom_spline.cpp
/// @brief Unit tests for CatmullRomSpline evaluation, tangents, sampling, and arc length.

#include "utils/catmull_rom_spline.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>

using namespace Vestige;

// =============================================================================
// Empty and single-point edge cases
// =============================================================================

TEST(CatmullRomSplineTest, EmptySplineReturnsZero)
{
    CatmullRomSpline spline;
    glm::vec3 result = spline.evaluate(0.0f);
    EXPECT_FLOAT_EQ(result.x, 0.0f);
    EXPECT_FLOAT_EQ(result.y, 0.0f);
    EXPECT_FLOAT_EQ(result.z, 0.0f);
}

TEST(CatmullRomSplineTest, EmptySplinePointCount)
{
    CatmullRomSpline spline;
    EXPECT_EQ(spline.getPointCount(), 0u);
}

TEST(CatmullRomSplineTest, SinglePointReturnsThatPoint)
{
    CatmullRomSpline spline;
    spline.addPoint(glm::vec3(3.0f, 5.0f, 7.0f));

    glm::vec3 result = spline.evaluate(0.0f);
    EXPECT_FLOAT_EQ(result.x, 3.0f);
    EXPECT_FLOAT_EQ(result.y, 5.0f);
    EXPECT_FLOAT_EQ(result.z, 7.0f);
}

TEST(CatmullRomSplineTest, SinglePointAnyTReturnsSamePoint)
{
    CatmullRomSpline spline;
    spline.addPoint(glm::vec3(1.0f, 2.0f, 3.0f));

    glm::vec3 r1 = spline.evaluate(0.5f);
    glm::vec3 r2 = spline.evaluate(100.0f);
    EXPECT_FLOAT_EQ(r1.x, 1.0f);
    EXPECT_FLOAT_EQ(r2.x, 1.0f);
}

// =============================================================================
// Two-point spline (linear interpolation)
// =============================================================================

TEST(CatmullRomSplineTest, TwoPointsAtT0ReturnsFirstPoint)
{
    CatmullRomSpline spline;
    spline.addPoint(glm::vec3(0.0f, 0.0f, 0.0f));
    spline.addPoint(glm::vec3(10.0f, 0.0f, 0.0f));

    glm::vec3 result = spline.evaluate(0.0f);
    EXPECT_NEAR(result.x, 0.0f, 0.01f);
    EXPECT_NEAR(result.y, 0.0f, 0.01f);
    EXPECT_NEAR(result.z, 0.0f, 0.01f);
}

TEST(CatmullRomSplineTest, TwoPointsAtT1ReturnsSecondPoint)
{
    CatmullRomSpline spline;
    spline.addPoint(glm::vec3(0.0f, 0.0f, 0.0f));
    spline.addPoint(glm::vec3(10.0f, 0.0f, 0.0f));

    glm::vec3 result = spline.evaluate(1.0f);
    EXPECT_NEAR(result.x, 10.0f, 0.01f);
    EXPECT_NEAR(result.y, 0.0f, 0.01f);
    EXPECT_NEAR(result.z, 0.0f, 0.01f);
}

TEST(CatmullRomSplineTest, TwoPointsLinearInterpolationAtMidpoint)
{
    CatmullRomSpline spline;
    spline.addPoint(glm::vec3(0.0f, 0.0f, 0.0f));
    spline.addPoint(glm::vec3(10.0f, 0.0f, 0.0f));

    glm::vec3 result = spline.evaluate(0.5f);
    EXPECT_NEAR(result.x, 5.0f, 0.01f);
    EXPECT_NEAR(result.y, 0.0f, 0.01f);
    EXPECT_NEAR(result.z, 0.0f, 0.01f);
}

// =============================================================================
// Three-point spline -- smooth curve
// =============================================================================

TEST(CatmullRomSplineTest, ThreePointsPassesThroughControlPoints)
{
    CatmullRomSpline spline;
    spline.addPoint(glm::vec3(0.0f, 0.0f, 0.0f));
    spline.addPoint(glm::vec3(5.0f, 0.0f, 5.0f));
    spline.addPoint(glm::vec3(10.0f, 0.0f, 0.0f));

    // At t=0, should be at first point
    glm::vec3 start = spline.evaluate(0.0f);
    EXPECT_NEAR(start.x, 0.0f, 0.01f);
    EXPECT_NEAR(start.z, 0.0f, 0.01f);

    // At t=1, should be at second point
    glm::vec3 mid = spline.evaluate(1.0f);
    EXPECT_NEAR(mid.x, 5.0f, 0.01f);
    EXPECT_NEAR(mid.z, 5.0f, 0.01f);

    // At t=2, should be at third point
    glm::vec3 end = spline.evaluate(2.0f);
    EXPECT_NEAR(end.x, 10.0f, 0.01f);
    EXPECT_NEAR(end.z, 0.0f, 0.01f);
}

TEST(CatmullRomSplineTest, ThreePointsSmoothBetweenSegments)
{
    CatmullRomSpline spline;
    spline.addPoint(glm::vec3(0.0f, 0.0f, 0.0f));
    spline.addPoint(glm::vec3(5.0f, 0.0f, 5.0f));
    spline.addPoint(glm::vec3(10.0f, 0.0f, 0.0f));

    // Evaluate at many points and verify continuity (no large jumps)
    glm::vec3 prev = spline.evaluate(0.0f);
    for (int i = 1; i <= 100; ++i)
    {
        float t = static_cast<float>(i) / 50.0f; // 0 to 2.0
        glm::vec3 curr = spline.evaluate(t);
        float dist = glm::distance(prev, curr);
        EXPECT_LT(dist, 0.5f) << "Discontinuity at t=" << t;
        prev = curr;
    }
}

// =============================================================================
// Tangent evaluation
// =============================================================================

TEST(CatmullRomSplineTest, TangentAtStartPointsForward)
{
    CatmullRomSpline spline;
    spline.addPoint(glm::vec3(0.0f, 0.0f, 0.0f));
    spline.addPoint(glm::vec3(10.0f, 0.0f, 0.0f));

    glm::vec3 tangent = spline.evaluateTangent(0.0f);
    // Should point in the +X direction
    EXPECT_GT(tangent.x, 0.0f);
}

TEST(CatmullRomSplineTest, TangentFewerThanTwoPointsReturnDefault)
{
    CatmullRomSpline spline;
    glm::vec3 tangent = spline.evaluateTangent(0.0f);
    // Default is (0, 0, 1) according to the implementation
    EXPECT_FLOAT_EQ(tangent.z, 1.0f);

    spline.addPoint(glm::vec3(1.0f, 2.0f, 3.0f));
    tangent = spline.evaluateTangent(0.5f);
    EXPECT_FLOAT_EQ(tangent.z, 1.0f);
}

TEST(CatmullRomSplineTest, TangentNonZeroMagnitude)
{
    CatmullRomSpline spline;
    spline.addPoint(glm::vec3(0.0f, 0.0f, 0.0f));
    spline.addPoint(glm::vec3(5.0f, 5.0f, 0.0f));
    spline.addPoint(glm::vec3(10.0f, 0.0f, 0.0f));

    glm::vec3 tangent = spline.evaluateTangent(0.5f);
    float length = glm::length(tangent);
    EXPECT_GT(length, 0.0f);
}

// =============================================================================
// Sampling
// =============================================================================

TEST(CatmullRomSplineTest, SampleEmptySplineReturnsEmpty)
{
    CatmullRomSpline spline;
    auto samples = spline.sample(10);
    EXPECT_TRUE(samples.empty());
}

TEST(CatmullRomSplineTest, SampleSinglePointReturnsSinglePoint)
{
    CatmullRomSpline spline;
    spline.addPoint(glm::vec3(1.0f, 2.0f, 3.0f));
    auto samples = spline.sample(10);
    EXPECT_EQ(samples.size(), 1u);
    EXPECT_NEAR(samples[0].x, 1.0f, 0.01f);
}

TEST(CatmullRomSplineTest, SampleProducesCorrectPointCount)
{
    CatmullRomSpline spline;
    spline.addPoint(glm::vec3(0.0f, 0.0f, 0.0f));
    spline.addPoint(glm::vec3(5.0f, 0.0f, 0.0f));
    spline.addPoint(glm::vec3(10.0f, 0.0f, 0.0f));

    int samplesPerSegment = 10;
    auto samples = spline.sample(samplesPerSegment);
    // 2 segments * 10 samples + 1 final = 21
    EXPECT_EQ(samples.size(), 21u);
}

TEST(CatmullRomSplineTest, SampleFourPointsCorrectCount)
{
    CatmullRomSpline spline;
    spline.addPoint(glm::vec3(0.0f));
    spline.addPoint(glm::vec3(3.0f, 0.0f, 0.0f));
    spline.addPoint(glm::vec3(6.0f, 0.0f, 0.0f));
    spline.addPoint(glm::vec3(9.0f, 0.0f, 0.0f));

    int samplesPerSegment = 5;
    auto samples = spline.sample(samplesPerSegment);
    // 3 segments * 5 samples + 1 = 16
    EXPECT_EQ(samples.size(), 16u);
}

// =============================================================================
// Arc length
// =============================================================================

TEST(CatmullRomSplineTest, ApproxLengthEmptyIsZero)
{
    CatmullRomSpline spline;
    EXPECT_FLOAT_EQ(spline.getApproxLength(), 0.0f);
}

TEST(CatmullRomSplineTest, ApproxLengthSinglePointIsZero)
{
    CatmullRomSpline spline;
    spline.addPoint(glm::vec3(5.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(spline.getApproxLength(), 0.0f);
}

TEST(CatmullRomSplineTest, ApproxLengthStraightLine)
{
    CatmullRomSpline spline;
    spline.addPoint(glm::vec3(0.0f, 0.0f, 0.0f));
    spline.addPoint(glm::vec3(10.0f, 0.0f, 0.0f));

    float length = spline.getApproxLength();
    EXPECT_NEAR(length, 10.0f, 0.1f);
}

TEST(CatmullRomSplineTest, ApproxLengthCurveIsLongerThanChord)
{
    CatmullRomSpline spline;
    spline.addPoint(glm::vec3(0.0f, 0.0f, 0.0f));
    spline.addPoint(glm::vec3(5.0f, 5.0f, 0.0f));
    spline.addPoint(glm::vec3(10.0f, 0.0f, 0.0f));

    float length = spline.getApproxLength();
    // Chord distance is 10.0, curve through (5,5) should be longer
    EXPECT_GT(length, 10.0f);
    // But not unreasonably long for this shape
    EXPECT_LT(length, 20.0f);
}

// =============================================================================
// addPoint / clear / getPointCount
// =============================================================================

TEST(CatmullRomSplineTest, AddPointIncreasesCount)
{
    CatmullRomSpline spline;
    EXPECT_EQ(spline.getPointCount(), 0u);

    spline.addPoint(glm::vec3(0.0f));
    EXPECT_EQ(spline.getPointCount(), 1u);

    spline.addPoint(glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(spline.getPointCount(), 2u);

    spline.addPoint(glm::vec3(2.0f, 0.0f, 0.0f));
    EXPECT_EQ(spline.getPointCount(), 3u);
}

TEST(CatmullRomSplineTest, ClearResetsToEmpty)
{
    CatmullRomSpline spline;
    spline.addPoint(glm::vec3(0.0f));
    spline.addPoint(glm::vec3(1.0f, 0.0f, 0.0f));
    spline.addPoint(glm::vec3(2.0f, 0.0f, 0.0f));
    EXPECT_EQ(spline.getPointCount(), 3u);

    spline.clear();
    EXPECT_EQ(spline.getPointCount(), 0u);
}

TEST(CatmullRomSplineTest, ClearThenEvaluateReturnsZero)
{
    CatmullRomSpline spline;
    spline.addPoint(glm::vec3(5.0f, 5.0f, 5.0f));
    spline.clear();

    glm::vec3 result = spline.evaluate(0.0f);
    EXPECT_FLOAT_EQ(result.x, 0.0f);
    EXPECT_FLOAT_EQ(result.y, 0.0f);
    EXPECT_FLOAT_EQ(result.z, 0.0f);
}

TEST(CatmullRomSplineTest, GetPointReturnsCorrectValue)
{
    CatmullRomSpline spline;
    spline.addPoint(glm::vec3(1.0f, 2.0f, 3.0f));
    spline.addPoint(glm::vec3(4.0f, 5.0f, 6.0f));

    const glm::vec3& p0 = spline.getPoint(0);
    EXPECT_FLOAT_EQ(p0.x, 1.0f);
    EXPECT_FLOAT_EQ(p0.y, 2.0f);
    EXPECT_FLOAT_EQ(p0.z, 3.0f);

    const glm::vec3& p1 = spline.getPoint(1);
    EXPECT_FLOAT_EQ(p1.x, 4.0f);
    EXPECT_FLOAT_EQ(p1.y, 5.0f);
    EXPECT_FLOAT_EQ(p1.z, 6.0f);
}

// =============================================================================
// Constructor with control points
// =============================================================================

TEST(CatmullRomSplineTest, ConstructFromVector)
{
    std::vector<glm::vec3> points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(5.0f, 0.0f, 0.0f),
        glm::vec3(10.0f, 0.0f, 0.0f)
    };

    CatmullRomSpline spline(points);
    EXPECT_EQ(spline.getPointCount(), 3u);
    EXPECT_NEAR(spline.evaluate(0.0f).x, 0.0f, 0.01f);
    EXPECT_NEAR(spline.evaluate(2.0f).x, 10.0f, 0.01f);
}

// =============================================================================
// T clamping behavior
// =============================================================================

TEST(CatmullRomSplineTest, NegativeTClampedToStart)
{
    CatmullRomSpline spline;
    spline.addPoint(glm::vec3(0.0f, 0.0f, 0.0f));
    spline.addPoint(glm::vec3(10.0f, 0.0f, 0.0f));

    glm::vec3 result = spline.evaluate(-5.0f);
    EXPECT_NEAR(result.x, 0.0f, 0.01f);
}

TEST(CatmullRomSplineTest, ExcessiveTClampedToEnd)
{
    CatmullRomSpline spline;
    spline.addPoint(glm::vec3(0.0f, 0.0f, 0.0f));
    spline.addPoint(glm::vec3(10.0f, 0.0f, 0.0f));

    glm::vec3 result = spline.evaluate(100.0f);
    EXPECT_NEAR(result.x, 10.0f, 0.01f);
}
