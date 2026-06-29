// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_ducking.cpp
/// @brief Phase 10 audio quick-wins (AX13) — coverage for the side-chain
///        ducking router and mix_graph.json parsing: empty-router
///        identity, single-route slew parity with `updateDucking`,
///        two-routes-one-target product accumulation, and the
///        Ui/Master/unknown-bus rejection rules. Headless.

#include <gtest/gtest.h>

#include "audio/audio_ducking.h"
#include "audio/audio_mixer.h"

#include <nlohmann/json.hpp>

#include <array>

using namespace Vestige;
using nlohmann::json;

namespace
{
constexpr float kEps = 1e-4f;

std::array<bool, AudioBusCount> noActivity()
{
    return std::array<bool, AudioBusCount>{};  // all false
}

std::array<bool, AudioBusCount> activeOn(AudioBus bus)
{
    std::array<bool, AudioBusCount> a{};
    a[static_cast<std::size_t>(bus)] = true;
    return a;
}
}

// -- Empty router is the identity (no behaviour change) --------------

TEST(AudioDucking, EmptyRouterReturnsAllUnity)
{
    DuckingRouter router;  // no routes
    auto duck = router.advance(activeOn(AudioBus::Voice), 0.1f);
    for (std::size_t i = 0; i < AudioBusCount; ++i)
    {
        EXPECT_NEAR(duck[i], 1.0f, kEps);
    }
}

// -- Single route: active source dips its target -------------------

TEST(AudioDucking, ActiveSourceDucksTargetTowardFloor)
{
    DuckingRoute route;
    route.source = AudioBus::Voice;
    route.target = AudioBus::Music;
    route.params.duckFactor = 0.35f;
    DuckingRouter router;
    router.setRoutes({route});

    // Hold Voice active for well past the attack window.
    std::array<float, AudioBusCount> duck{};
    for (int i = 0; i < 60; ++i)
    {
        duck = router.advance(activeOn(AudioBus::Voice), 1.0f / 60.0f);
    }
    EXPECT_NEAR(duck[static_cast<std::size_t>(AudioBus::Music)], 0.35f, 1e-2f);
    // Untargeted buses stay at unity.
    EXPECT_NEAR(duck[static_cast<std::size_t>(AudioBus::Sfx)], 1.0f, kEps);

    // Release: Voice goes quiet → Music recovers toward 1.0.
    for (int i = 0; i < 120; ++i)
    {
        duck = router.advance(noActivity(), 1.0f / 60.0f);
    }
    EXPECT_NEAR(duck[static_cast<std::size_t>(AudioBus::Music)], 1.0f, 1e-2f);
}

// -- Parity: a route's slew matches standalone updateDucking --------

TEST(AudioDucking, SingleRouteSlewMatchesUpdateDucking)
{
    DuckingRoute route;
    route.source = AudioBus::Voice;
    route.target = AudioBus::Music;
    DuckingRouter router;
    router.setRoutes({route});

    // Reference: the old single-duck path driven by the same trigger.
    DuckingState reference;
    reference.triggered = true;
    DuckingParams params;  // same defaults as the route

    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 30; ++i)
    {
        auto duck = router.advance(activeOn(AudioBus::Voice), dt);
        updateDucking(reference, params, dt);
        EXPECT_NEAR(duck[static_cast<std::size_t>(AudioBus::Music)],
                    reference.currentGain, kEps);
    }
}

// -- Two routes onto one target multiply -----------------------------

TEST(AudioDucking, TwoRoutesSameTargetMultiply)
{
    DuckingRoute a;
    a.source = AudioBus::Voice;
    a.target = AudioBus::Ambient;
    a.params.duckFactor = 0.5f;
    DuckingRoute b;
    b.source = AudioBus::Sfx;
    b.target = AudioBus::Ambient;
    b.params.duckFactor = 0.5f;
    DuckingRouter router;
    router.setRoutes({a, b});

    std::array<bool, AudioBusCount> both{};
    both[static_cast<std::size_t>(AudioBus::Voice)] = true;
    both[static_cast<std::size_t>(AudioBus::Sfx)]   = true;

    std::array<float, AudioBusCount> duck{};
    for (int i = 0; i < 60; ++i)
    {
        duck = router.advance(both, 1.0f / 60.0f);
    }
    // Both routes settle near 0.5 → product near 0.25 on Ambient.
    EXPECT_NEAR(duck[static_cast<std::size_t>(AudioBus::Ambient)], 0.25f, 2e-2f);
}

// -- mix_graph.json parsing + rejection rules ------------------------

TEST(AudioDucking, ParseAcceptsValidRoutes)
{
    json j = {
        {"routes", json::array({
            {{"source", "voice"}, {"target", "music"},
             {"attack", 0.1f}, {"release", 0.4f}, {"duckFactor", 0.2f}},
            {{"source", "sfx"}, {"target", "ambient"}},
        })},
    };
    auto routes = parseDuckingRoutes(j);
    ASSERT_EQ(routes.size(), 2u);
    EXPECT_EQ(routes[0].source, AudioBus::Voice);
    EXPECT_EQ(routes[0].target, AudioBus::Music);
    EXPECT_NEAR(routes[0].params.attackSeconds,  0.1f, kEps);
    EXPECT_NEAR(routes[0].params.releaseSeconds, 0.4f, kEps);
    EXPECT_NEAR(routes[0].params.duckFactor,     0.2f, kEps);
    EXPECT_EQ(routes[1].source, AudioBus::Sfx);
    EXPECT_EQ(routes[1].target, AudioBus::Ambient);
}

TEST(AudioDucking, ParseRejectsUiAndMasterTargets)
{
    json j = {
        {"routes", json::array({
            {{"source", "voice"}, {"target", "ui"}},      // rejected
            {{"source", "voice"}, {"target", "master"}},  // rejected
            {{"source", "voice"}, {"target", "music"}},   // kept
        })},
    };
    auto routes = parseDuckingRoutes(j);
    ASSERT_EQ(routes.size(), 1u);
    EXPECT_EQ(routes[0].target, AudioBus::Music);
}

TEST(AudioDucking, ParseRejectsUnknownBus)
{
    json j = {
        {"routes", json::array({
            {{"source", "dialogue"}, {"target", "music"}},  // bad source
            {{"source", "voice"}, {"target", "reverb"}},    // bad target
        })},
    };
    EXPECT_TRUE(parseDuckingRoutes(j).empty());
}

TEST(AudioDucking, ParseMissingRoutesArrayIsEmpty)
{
    EXPECT_TRUE(parseDuckingRoutes(json::object()).empty());
}
