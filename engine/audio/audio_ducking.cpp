// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_ducking.cpp
/// @brief AX13 — side-chain ducking router + mix_graph.json parsing.
#include "audio/audio_ducking.h"

#include "core/logger.h"

#include <nlohmann/json.hpp>

#include <string>

namespace Vestige
{

void DuckingRouter::setRoutes(std::vector<DuckingRoute> routes)
{
    m_routes = std::move(routes);
    // One DuckingState per route, all reset to "released" (no duck).
    m_stateByRoute.assign(m_routes.size(), DuckingState{});
}

std::array<float, AudioBusCount> DuckingRouter::advance(
    const std::array<bool, AudioBusCount>& busActive, float dt)
{
    std::array<float, AudioBusCount> duck;
    duck.fill(1.0f);

    for (std::size_t i = 0; i < m_routes.size(); ++i)
    {
        const DuckingRoute& route = m_routes[i];
        m_stateByRoute[i].triggered =
            busActive[static_cast<std::size_t>(route.source)];
        updateDucking(m_stateByRoute[i], route.params, dt);
        // A target hit by multiple routes ducks by the product of the dips.
        duck[static_cast<std::size_t>(route.target)] *=
            m_stateByRoute[i].currentGain;
    }

    return duck;
}

std::vector<DuckingRoute> parseDuckingRoutes(const nlohmann::json& j)
{
    std::vector<DuckingRoute> routes;
    if (!j.contains("routes") || !j["routes"].is_array())
    {
        return routes;
    }

    for (const auto& entry : j["routes"])
    {
        if (!entry.is_object())
        {
            Logger::warning("[Ducking] mix_graph.json: skipping non-object route");
            continue;
        }

        const std::string sourceName = entry.value("source", std::string{});
        const std::string targetName = entry.value("target", std::string{});

        AudioBus source{};
        AudioBus target{};
        if (!audioBusFromString(sourceName, source))
        {
            Logger::warning("[Ducking] mix_graph.json: unknown source bus '"
                            + sourceName + "' — route dropped");
            continue;
        }
        if (!audioBusFromString(targetName, target))
        {
            Logger::warning("[Ducking] mix_graph.json: unknown target bus '"
                            + targetName + "' — route dropped");
            continue;
        }
        // Ui carries accessibility cues; Master is the global bus — ducking
        // either is forbidden (see header). Reject at parse time.
        if (target == AudioBus::Ui || target == AudioBus::Master)
        {
            Logger::warning("[Ducking] mix_graph.json: '" + targetName
                            + "' is not a legal duck target — route dropped");
            continue;
        }

        DuckingRoute route;
        route.source = source;
        route.target = target;
        route.params.attackSeconds =
            entry.value("attack", route.params.attackSeconds);
        route.params.releaseSeconds =
            entry.value("release", route.params.releaseSeconds);
        route.params.duckFactor =
            entry.value("duckFactor", route.params.duckFactor);
        routes.push_back(route);
    }

    return routes;
}

} // namespace Vestige
