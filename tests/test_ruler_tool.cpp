/// @file test_ruler_tool.cpp
/// @brief Unit tests for the RulerTool measurement system.
#include "editor/tools/ruler_tool.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>

using namespace Vestige;

TEST(RulerToolTest, StartsInactive)
{
    RulerTool ruler;
    EXPECT_EQ(ruler.getState(), RulerTool::State::INACTIVE);
    EXPECT_FALSE(ruler.isActive());
    EXPECT_FALSE(ruler.hasMeasurement());
}

TEST(RulerToolTest, StartMeasurementEntersWaitingA)
{
    RulerTool ruler;
    ruler.startMeasurement();
    EXPECT_EQ(ruler.getState(), RulerTool::State::WAITING_A);
    EXPECT_TRUE(ruler.isActive());
    EXPECT_FALSE(ruler.hasMeasurement());
}

TEST(RulerToolTest, FirstClickSetsPointA)
{
    RulerTool ruler;
    ruler.startMeasurement();

    bool consumed = ruler.processClick(glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_TRUE(consumed);
    EXPECT_EQ(ruler.getState(), RulerTool::State::WAITING_B);
    EXPECT_EQ(ruler.getPointA(), glm::vec3(1.0f, 0.0f, 0.0f));
}

TEST(RulerToolTest, SecondClickCompleteMeasurement)
{
    RulerTool ruler;
    ruler.startMeasurement();
    ruler.processClick(glm::vec3(0.0f, 0.0f, 0.0f));
    ruler.processClick(glm::vec3(3.0f, 4.0f, 0.0f));

    EXPECT_TRUE(ruler.hasMeasurement());
    EXPECT_EQ(ruler.getState(), RulerTool::State::MEASURED);
    EXPECT_FLOAT_EQ(ruler.getDistance(), 5.0f);
    EXPECT_EQ(ruler.getPointA(), glm::vec3(0.0f, 0.0f, 0.0f));
    EXPECT_EQ(ruler.getPointB(), glm::vec3(3.0f, 4.0f, 0.0f));
}

TEST(RulerToolTest, CancelReturnsToInactive)
{
    RulerTool ruler;
    ruler.startMeasurement();
    ruler.processClick(glm::vec3(1.0f, 0.0f, 0.0f));
    ruler.cancel();

    EXPECT_EQ(ruler.getState(), RulerTool::State::INACTIVE);
    EXPECT_FALSE(ruler.isActive());
}

TEST(RulerToolTest, ClickWhileMeasuredStartsNewMeasurement)
{
    RulerTool ruler;
    ruler.startMeasurement();
    ruler.processClick(glm::vec3(0.0f, 0.0f, 0.0f));
    ruler.processClick(glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_TRUE(ruler.hasMeasurement());

    // Clicking again starts a new measurement
    ruler.processClick(glm::vec3(5.0f, 0.0f, 0.0f));
    EXPECT_EQ(ruler.getState(), RulerTool::State::WAITING_B);
    EXPECT_EQ(ruler.getPointA(), glm::vec3(5.0f, 0.0f, 0.0f));
}

TEST(RulerToolTest, ClickWhenInactiveDoesNothing)
{
    RulerTool ruler;
    bool consumed = ruler.processClick(glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_FALSE(consumed);
    EXPECT_EQ(ruler.getState(), RulerTool::State::INACTIVE);
}

TEST(RulerToolTest, ZeroDistanceMeasurement)
{
    RulerTool ruler;
    ruler.startMeasurement();
    ruler.processClick(glm::vec3(2.0f, 3.0f, 4.0f));
    ruler.processClick(glm::vec3(2.0f, 3.0f, 4.0f));

    EXPECT_TRUE(ruler.hasMeasurement());
    EXPECT_FLOAT_EQ(ruler.getDistance(), 0.0f);
}

TEST(RulerToolTest, QueueDebugDrawDoesNotCrash)
{
    RulerTool ruler;
    EXPECT_NO_THROW(ruler.queueDebugDraw());

    ruler.startMeasurement();
    EXPECT_NO_THROW(ruler.queueDebugDraw());

    ruler.processClick(glm::vec3(0.0f));
    EXPECT_NO_THROW(ruler.queueDebugDraw());

    ruler.processClick(glm::vec3(1.0f));
    EXPECT_NO_THROW(ruler.queueDebugDraw());
}
