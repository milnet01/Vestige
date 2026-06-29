// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_ducking.h
/// @brief Phase 10 audio quick-wins (AX13) — generalised side-chain
///        ducking: N (sourceBus → targetBus) routes on top of the
///        existing single global manual duck.
///
/// The engine already has a single global duck (the dialogue duck) driven
/// by `Engine::getDuckingState().triggered` and advanced by
/// `updateDucking`. AX13 keeps that **verbatim** — it remains a global,
/// all-bus dip and is the no-config default (so today's behaviour is
/// reproduced exactly, §12-Q3). On top of it, this router adds optional
/// **data-driven** routes from `assets/audio/mix_graph.json`: each route
/// dips one *target* bus while a *source* bus is active this frame
/// (e.g. Sfx dips under Music stingers). The final per-bus duck the
/// engine applies is `manualGlobalDuck × routerDuck[bus]`.
///
/// Pure / data-only — no OpenAL. `AudioSystem` feeds it a per-frame
/// bus-activity bitset and applies the resulting per-bus gains.
#pragma once

#include "audio/audio_mixer.h"  // AudioBus, AudioBusCount, DuckingParams, DuckingState

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <vector>

namespace Vestige
{

/// @brief One side-chain route: while `source` is active, `target` dips.
struct DuckingRoute
{
    AudioBus      source = AudioBus::Voice;  ///< Bus whose activity triggers the dip.
    AudioBus      target = AudioBus::Music;  ///< Bus that gets attenuated.
    DuckingParams params;                    ///< Reuses the existing attack/release/floor.
};

/// @brief Advances a set of routes and accumulates the per-target-bus
///        duck gain (product of all inbound route dips).
///
/// Two distinct dimensions (the original sizing bug): the per-route
/// `DuckingState` vector is sized to `routes.size()` (routes are
/// unbounded), while `advance` *returns* a fixed
/// `std::array<float, AudioBusCount>` indexed by the **target** bus.
class DuckingRouter
{
public:
    /// @brief Replaces the route set and resizes the per-route state to
    ///        match (every state reset to no-duck).
    void setRoutes(std::vector<DuckingRoute> routes);

    const std::vector<DuckingRoute>& routes() const { return m_routes; }
    const std::vector<DuckingState>& stateByRoute() const { return m_stateByRoute; }

    /// @brief Advances every route by @a dt and returns the per-target-bus
    ///        duck gain. A bus targeted by two routes dips by the product.
    /// @param busActive Per-bus "had audible activity this frame" flags.
    std::array<float, AudioBusCount> advance(
        const std::array<bool, AudioBusCount>& busActive, float dt);

private:
    std::vector<DuckingRoute> m_routes;
    std::vector<DuckingState> m_stateByRoute;  // one per route (NOT per bus)
};

/// @brief Parses `mix_graph.json` routes. Each entry is
///        `{ "source": "<bus>", "target": "<bus>",
///           "attack"?, "release"?, "duckFactor"? }`.
///
/// `Ui` and `Master` are **rejected** as targets (accessibility cues must
/// not be ducked; ducking Master would attenuate everything) — such
/// routes, and routes naming an unknown bus, are dropped with a
/// `Logger::warning`. Legal targets: Music, Voice, Sfx, Ambient.
std::vector<DuckingRoute> parseDuckingRoutes(const nlohmann::json& j);

} // namespace Vestige
