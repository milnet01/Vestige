// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_environment_forces.cpp
/// @brief Tests for the centralized EnvironmentForces system.
#include "environment/environment_forces.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace Vestige;

// ============================================================================
// Construction and defaults
// ============================================================================

TEST(EnvironmentForces, DefaultState)
{
    EnvironmentForces env;

    // Wind should be zero by default (strength = 0)
    EXPECT_FLOAT_EQ(env.getBaseWindStrength(), 0.0f);
    EXPECT_FLOAT_EQ(env.getGustIntensity(), 0.0f);

    // Default weather
    EXPECT_FLOAT_EQ(env.getTemperature(glm::vec3(0.0f)), 20.0f);
    EXPECT_FLOAT_EQ(env.getHumidity(glm::vec3(0.0f)), 0.5f);
    EXPECT_FLOAT_EQ(env.getWetness(glm::vec3(0.0f)), 0.0f);
    EXPECT_FLOAT_EQ(env.getPrecipitationIntensity(), 0.0f);
    EXPECT_FLOAT_EQ(env.getAirDensity(glm::vec3(0.0f)), 1.225f);

    // Default water level (very low, effectively no buoyancy)
    EXPECT_FLOAT_EQ(env.getWaterLevel(), -1000.0f);
}

TEST(EnvironmentForces, Reset)
{
    EnvironmentForces env;
    env.setWindDirection(glm::vec3(1.0f, 0.0f, 0.0f));
    env.setWindStrength(5.0f);
    env.update(1.0f);

    env.reset();
    EXPECT_FLOAT_EQ(env.getBaseWindStrength(), 0.0f);
    EXPECT_FLOAT_EQ(env.getElapsedTime(), 0.0f);
    EXPECT_FLOAT_EQ(env.getGustIntensity(), 0.0f);
}

// ============================================================================
// Wind configuration
// ============================================================================

TEST(EnvironmentForces, SetWindDirection)
{
    EnvironmentForces env;
    env.setWindDirection(glm::vec3(3.0f, 0.0f, 4.0f));

    glm::vec3 dir = env.getBaseWindDirection();
    EXPECT_NEAR(dir.x, 0.6f, 0.001f);
    EXPECT_NEAR(dir.y, 0.0f, 0.001f);
    EXPECT_NEAR(dir.z, 0.8f, 0.001f);
    EXPECT_NEAR(glm::length(dir), 1.0f, 0.001f);
}

TEST(EnvironmentForces, SetWindDirectionZeroSafe)
{
    EnvironmentForces env;
    glm::vec3 original = env.getBaseWindDirection();
    env.setWindDirection(glm::vec3(0.0f));
    // Should not change when zero vector is passed
    EXPECT_EQ(env.getBaseWindDirection(), original);
}

TEST(EnvironmentForces, SetWindStrength)
{
    EnvironmentForces env;
    env.setWindStrength(3.5f);
    EXPECT_FLOAT_EQ(env.getBaseWindStrength(), 3.5f);
}

TEST(EnvironmentForces, SetWindStrengthClampsNegative)
{
    EnvironmentForces env;
    env.setWindStrength(-2.0f);
    EXPECT_FLOAT_EQ(env.getBaseWindStrength(), 0.0f);
}

// ============================================================================
// Wind velocity queries
// ============================================================================

TEST(EnvironmentForces, ZeroStrengthGivesZeroVelocity)
{
    EnvironmentForces env;
    env.setWindDirection(glm::vec3(1.0f, 0.0f, 0.0f));
    env.setWindStrength(0.0f);

    glm::vec3 vel = env.getWindVelocity(glm::vec3(0.0f));
    EXPECT_FLOAT_EQ(vel.x, 0.0f);
    EXPECT_FLOAT_EQ(vel.y, 0.0f);
    EXPECT_FLOAT_EQ(vel.z, 0.0f);
}

TEST(EnvironmentForces, WindVelocityAfterGusting)
{
    EnvironmentForces env;
    env.setWindDirection(glm::vec3(1.0f, 0.0f, 0.0f));
    env.setWindStrength(5.0f);

    // Advance enough time for the gust SM to start gusting
    for (int i = 0; i < 100; ++i)
    {
        env.update(0.1f);  // 10 seconds total
    }

    glm::vec3 vel = env.getWindVelocity(glm::vec3(0.0f));
    // Wind should be non-zero (gust SM has had time to cycle)
    // We can't predict exact value due to RNG, but speed should be bounded
    float speed = glm::length(vel);
    EXPECT_GE(speed, 0.0f);
    EXPECT_LE(speed, 15.0f);  // 5.0 strength * ~1.5 spatial * ~1.23 flutter max
}

TEST(EnvironmentForces, WindVelocityDependsOnPosition)
{
    EnvironmentForces env;
    env.setWindDirection(glm::vec3(1.0f, 0.0f, 0.0f));
    env.setWindStrength(5.0f);
    env.setGustsEnabled(false);  // Disable gusts for deterministic comparison

    // Advance time so flutter is non-zero
    env.update(1.0f);

    glm::vec3 pos1(0.0f, 0.0f, 0.0f);
    glm::vec3 pos2(100.0f, 0.0f, 100.0f);

    glm::vec3 vel1 = env.getWindVelocity(pos1);
    glm::vec3 vel2 = env.getWindVelocity(pos2);

    // Different positions should give different velocities due to spatial noise
    // (unless noise happens to produce same value, which is extremely unlikely
    // at these positions)
    EXPECT_NE(vel1, vel2);
}

TEST(EnvironmentForces, WindSpeedMatchesVelocityMagnitude)
{
    EnvironmentForces env;
    env.setWindDirection(glm::vec3(0.0f, 0.0f, -1.0f));
    env.setWindStrength(3.0f);
    env.setGustsEnabled(false);
    env.update(0.5f);

    glm::vec3 pos(10.0f, 0.0f, 20.0f);
    float speed = env.getWindSpeed(pos);
    float velMag = glm::length(env.getWindVelocity(pos));

    EXPECT_NEAR(speed, velMag, 0.0001f);
}

TEST(EnvironmentForces, GustsDisabledGivesConstantGustIntensity)
{
    EnvironmentForces env;
    env.setGustsEnabled(false);
    env.update(0.5f);

    // With gusts disabled, gust intensity should be 1.0 (full strength always)
    EXPECT_FLOAT_EQ(env.getGustIntensity(), 1.0f);
}

// ============================================================================
// Gust state machine
// ============================================================================

TEST(EnvironmentForces, GustStateTransitions)
{
    EnvironmentForces env;
    env.setWindDirection(glm::vec3(1.0f, 0.0f, 0.0f));
    env.setWindStrength(5.0f);

    // Collect gust intensity samples over 30 seconds
    bool sawLow = false;
    bool sawHigh = false;

    for (int i = 0; i < 300; ++i)
    {
        env.update(0.1f);
        float gust = env.getGustIntensity();
        if (gust < 0.1f) sawLow = true;
        if (gust > 0.4f) sawHigh = true;
    }

    // The gust SM should have cycled through both blow and calm phases
    EXPECT_TRUE(sawLow) << "Gust SM never went calm in 30 seconds";
    EXPECT_TRUE(sawHigh) << "Gust SM never gusted in 30 seconds";
}

TEST(EnvironmentForces, DirectionVariation)
{
    EnvironmentForces env;
    env.setWindDirection(glm::vec3(1.0f, 0.0f, 0.0f));
    env.setWindStrength(5.0f);

    // After enough updates, direction offset should be non-zero
    for (int i = 0; i < 200; ++i)
    {
        env.update(0.1f);
    }

    glm::vec3 offset = env.getWindDirectionOffset();
    // At least one component should be non-zero after 20 seconds
    EXPECT_GT(glm::length(offset), 0.0f);
}

// ============================================================================
// Wind force
// ============================================================================

TEST(EnvironmentForces, WindForceZeroOnPerpendicularSurface)
{
    EnvironmentForces env;
    env.setWindDirection(glm::vec3(1.0f, 0.0f, 0.0f));
    env.setWindStrength(5.0f);
    env.setGustsEnabled(false);
    env.update(0.5f);

    // Surface normal perpendicular to wind direction
    glm::vec3 normal(0.0f, 1.0f, 0.0f);
    glm::vec3 force = env.getWindForce(glm::vec3(0.0f), 1.0f, normal);

    // Force should be along the normal, but dot(wind, normal) ≈ 0
    // (may not be exactly zero due to flutter/offset but should be very small)
    EXPECT_NEAR(force.y, 0.0f, 0.5f);
}

TEST(EnvironmentForces, WindForceProportionalToArea)
{
    EnvironmentForces env;
    env.setWindDirection(glm::vec3(1.0f, 0.0f, 0.0f));
    env.setWindStrength(5.0f);
    env.setGustsEnabled(false);
    env.update(0.5f);

    glm::vec3 normal(1.0f, 0.0f, 0.0f);
    glm::vec3 pos(0.0f);
    glm::vec3 force1 = env.getWindForce(pos, 1.0f, normal);
    glm::vec3 force2 = env.getWindForce(pos, 2.0f, normal);

    // Force should scale linearly with area
    EXPECT_NEAR(glm::length(force2), glm::length(force1) * 2.0f, 0.001f);
}

// ============================================================================
// Turbulence scale
// ============================================================================

TEST(EnvironmentForces, TurbulenceScaleAffectsSpatialVariation)
{
    // Compare average velocity difference across many position pairs
    // at two different turbulence scales. With smaller scale (higher freq),
    // nearby points should differ more on average.
    EnvironmentForces env1;
    env1.setWindDirection(glm::vec3(1.0f, 0.0f, 0.0f));
    env1.setWindStrength(5.0f);
    env1.setGustsEnabled(false);
    env1.setTurbulenceScale(1.0f);  // High frequency variation
    env1.update(0.5f);

    EnvironmentForces env2;
    env2.setWindDirection(glm::vec3(1.0f, 0.0f, 0.0f));
    env2.setWindStrength(5.0f);
    env2.setGustsEnabled(false);
    env2.setTurbulenceScale(100.0f);  // Low frequency variation
    env2.update(0.5f);

    // Sample many position pairs to average out noise artifacts
    float totalDiff1 = 0.0f;
    float totalDiff2 = 0.0f;
    constexpr int SAMPLES = 20;

    for (int i = 0; i < SAMPLES; ++i)
    {
        float base = static_cast<float>(i) * 5.0f;
        glm::vec3 posA(base, 0.0f, base);
        glm::vec3 posB(base + 3.0f, 0.0f, base + 3.0f);

        totalDiff1 += glm::length(env1.getWindVelocity(posA) - env1.getWindVelocity(posB));
        totalDiff2 += glm::length(env2.getWindVelocity(posA) - env2.getWindVelocity(posB));
    }

    float avgDiff1 = totalDiff1 / SAMPLES;
    float avgDiff2 = totalDiff2 / SAMPLES;

    // High-freq noise (scale=1) should produce more variation between nearby points
    EXPECT_GT(avgDiff1, avgDiff2);
}

TEST(EnvironmentForces, TurbulenceScaleClampsMinimum)
{
    EnvironmentForces env;
    env.setTurbulenceScale(0.0f);
    // Should clamp to minimum (0.1) to avoid division by zero
    env.setWindDirection(glm::vec3(1.0f, 0.0f, 0.0f));
    env.setWindStrength(5.0f);
    env.setGustsEnabled(false);
    env.update(0.5f);

    // Should not crash
    glm::vec3 vel = env.getWindVelocity(glm::vec3(0.0f));
    EXPECT_FALSE(std::isnan(vel.x));
    EXPECT_FALSE(std::isnan(vel.y));
    EXPECT_FALSE(std::isnan(vel.z));
}

// ============================================================================
// Weather state
// ============================================================================

TEST(EnvironmentForces, SetWeather)
{
    EnvironmentForces env;
    WeatherState weather;
    weather.temperature = 35.0f;
    weather.humidity = 0.8f;
    weather.precipitation = 0.5f;
    weather.cloudCover = 0.9f;
    weather.airDensity = 1.15f;

    env.setWeather(weather);

    EXPECT_FLOAT_EQ(env.getTemperature(glm::vec3(0.0f)), 35.0f);
    EXPECT_FLOAT_EQ(env.getHumidity(glm::vec3(0.0f)), 0.8f);
    EXPECT_FLOAT_EQ(env.getPrecipitationIntensity(), 0.5f);
    EXPECT_FLOAT_EQ(env.getAirDensity(glm::vec3(0.0f)), 1.15f);
}

TEST(EnvironmentForces, WetnessAccumulatesDuringRain)
{
    EnvironmentForces env;
    WeatherState weather;
    weather.precipitation = 1.0f;  // Heavy rain
    env.setWeather(weather);

    EXPECT_FLOAT_EQ(env.getWetness(glm::vec3(0.0f)), 0.0f);

    // After 10 seconds of heavy rain, wetness should increase
    for (int i = 0; i < 100; ++i)
    {
        env.update(0.1f);
    }

    float wetness = env.getWetness(glm::vec3(0.0f));
    EXPECT_GT(wetness, 0.0f);
    EXPECT_LE(wetness, 1.0f);

    // After 10 seconds at max precipitation, wetness = 10/30 ≈ 0.333
    EXPECT_NEAR(wetness, 10.0f / 30.0f, 0.01f);
}

TEST(EnvironmentForces, WetnessDriesWithoutRain)
{
    EnvironmentForces env;

    // Pre-wet by ticking with rain on, then sample the actual wetness.
    // Slice 18 Ts1: `WeatherState::wetness` is a *cached output*, not
    // an input — the `setWeather` field is ignored by the update loop.
    // The previous test compared against 0.5 (the value stamped into
    // the input field), masking whether wetting actually happened.
    WeatherState weather;
    weather.precipitation = 1.0f;
    env.setWeather(weather);
    for (int i = 0; i < 50; ++i)
    {
        env.update(0.1f);
    }
    const float preDrying = env.getWetness(glm::vec3(0.0f));
    EXPECT_GT(preDrying, 0.0f)
        << "test premise: wetting must register before drying can be observed";

    // Stop rain — drying should reduce wetness from its current value.
    weather.precipitation = 0.0f;
    env.setWeather(weather);
    for (int i = 0; i < 100; ++i)
    {
        env.update(0.1f);
    }
    const float postDrying = env.getWetness(glm::vec3(0.0f));
    EXPECT_LT(postDrying, preDrying);
    EXPECT_GE(postDrying, 0.0f);
}

TEST(EnvironmentForces, WetnessClampsAt1)
{
    EnvironmentForces env;
    WeatherState weather;
    weather.precipitation = 1.0f;
    env.setWeather(weather);

    // Run for a very long time
    for (int i = 0; i < 10000; ++i)
    {
        env.update(0.1f);
    }

    EXPECT_FLOAT_EQ(env.getWetness(glm::vec3(0.0f)), 1.0f);
}

// ============================================================================
// Buoyancy
// ============================================================================

TEST(EnvironmentForces, BuoyancyAboveWater)
{
    EnvironmentForces env;
    env.setWaterLevel(0.0f);

    // Object above water
    glm::vec3 force = env.getBuoyancy(glm::vec3(0.0f, 1.0f, 0.0f), 1.0f, 500.0f);
    EXPECT_FLOAT_EQ(force.x, 0.0f);
    EXPECT_FLOAT_EQ(force.y, 0.0f);
    EXPECT_FLOAT_EQ(force.z, 0.0f);
}

TEST(EnvironmentForces, BuoyancyBelowWater)
{
    EnvironmentForces env;
    env.setWaterLevel(0.0f);

    // 1 m^3 submerged volume
    glm::vec3 force = env.getBuoyancy(glm::vec3(0.0f, -1.0f, 0.0f), 1.0f, 500.0f);

    // Archimedes: F = rho_water * V * g = 997 * 1.0 * 9.81 ≈ 9780.57 N upward
    EXPECT_FLOAT_EQ(force.x, 0.0f);
    EXPECT_NEAR(force.y, 997.0f * 1.0f * 9.81f, 0.01f);
    EXPECT_FLOAT_EQ(force.z, 0.0f);
}

TEST(EnvironmentForces, BuoyancyScalesWithVolume)
{
    EnvironmentForces env;
    env.setWaterLevel(0.0f);

    glm::vec3 force1 = env.getBuoyancy(glm::vec3(0.0f, -1.0f, 0.0f), 1.0f, 500.0f);
    glm::vec3 force2 = env.getBuoyancy(glm::vec3(0.0f, -1.0f, 0.0f), 2.0f, 500.0f);

    EXPECT_NEAR(force2.y, force1.y * 2.0f, 0.01f);
}

TEST(EnvironmentForces, WaterLevelConfiguration)
{
    EnvironmentForces env;
    env.setWaterLevel(5.0f);
    EXPECT_FLOAT_EQ(env.getWaterLevel(), 5.0f);

    // Object at y=3 is below water level 5 → buoyancy should apply
    glm::vec3 force = env.getBuoyancy(glm::vec3(0.0f, 3.0f, 0.0f), 0.5f, 500.0f);
    EXPECT_GT(force.y, 0.0f);
}

// ============================================================================
// Elapsed time
// ============================================================================

TEST(EnvironmentForces, ElapsedTimeAccumulates)
{
    EnvironmentForces env;
    EXPECT_FLOAT_EQ(env.getElapsedTime(), 0.0f);

    env.update(0.5f);
    EXPECT_NEAR(env.getElapsedTime(), 0.5f, 0.0001f);

    env.update(0.3f);
    EXPECT_NEAR(env.getElapsedTime(), 0.8f, 0.0001f);
}

// ============================================================================
// Integration: wind velocity is bounded and sane
// ============================================================================

TEST(EnvironmentForces, WindVelocityNeverNaN)
{
    EnvironmentForces env;
    env.setWindDirection(glm::vec3(1.0f, 0.0f, 0.0f));
    env.setWindStrength(10.0f);

    // Run for many frames with varying dt
    for (int i = 0; i < 500; ++i)
    {
        float dt = 0.001f + static_cast<float>(i % 100) * 0.001f;
        env.update(dt);

        glm::vec3 vel = env.getWindVelocity(glm::vec3(
            static_cast<float>(i), 0.0f, static_cast<float>(i * 3)));

        ASSERT_FALSE(std::isnan(vel.x)) << "NaN at frame " << i;
        ASSERT_FALSE(std::isnan(vel.y)) << "NaN at frame " << i;
        ASSERT_FALSE(std::isnan(vel.z)) << "NaN at frame " << i;
        ASSERT_FALSE(std::isinf(vel.x)) << "Inf at frame " << i;
        ASSERT_FALSE(std::isinf(vel.y)) << "Inf at frame " << i;
        ASSERT_FALSE(std::isinf(vel.z)) << "Inf at frame " << i;
    }
}
