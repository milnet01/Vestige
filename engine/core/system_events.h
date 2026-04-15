// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file system_events.h
/// @brief Typed event structs for cross-system communication via EventBus.
///
/// Rule: events are for discrete occurrences. For continuous position-dependent
/// data (wind, temperature, etc.), systems query EnvironmentForces directly.
#pragma once

#include "core/event.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace Vestige
{

class Scene;

/// @brief Fired after a scene has finished loading and is ready for use.
struct SceneLoadedEvent : public Event
{
    Scene* scene;

    explicit SceneLoadedEvent(Scene* s) : scene(s) {}
};

/// @brief Fired before a scene is about to be unloaded.
struct SceneUnloadedEvent : public Event
{
    Scene* scene;

    explicit SceneUnloadedEvent(Scene* s) : scene(s) {}
};

/// @brief Fired when weather parameters change (e.g. wind direction, precipitation).
/// Systems that cache weather-dependent data should invalidate on this event.
struct WeatherChangedEvent : public Event
{
    float temperature;
    float humidity;
    float precipitation;
    float windStrength;

    WeatherChangedEvent(float temp, float hum, float precip, float wind)
        : temperature(temp), humidity(hum), precipitation(precip), windStrength(wind)
    {
    }
};

/// @brief Fired when an entity is about to be destroyed.
struct EntityDestroyedEvent : public Event
{
    uint32_t entityId;

    explicit EntityDestroyedEvent(uint32_t id) : entityId(id) {}
};

/// @brief Fired when terrain heightfield data is modified (sculpt, flatten, etc.).
/// Listeners should update cached data that depends on terrain geometry.
struct TerrainModifiedEvent : public Event
{
    float minX;
    float minZ;
    float maxX;
    float maxZ;

    TerrainModifiedEvent(float x0, float z0, float x1, float z1)
        : minX(x0), minZ(z0), maxX(x1), maxZ(z1)
    {
    }
};

/// @brief Fired to request a one-shot sound effect at a position.
/// Other systems can trigger sounds without depending on AudioSystem directly.
struct AudioPlayEvent : public Event
{
    std::string clipPath;
    glm::vec3 position;
    float volume;

    AudioPlayEvent(const std::string& path, const glm::vec3& pos, float vol = 1.0f)
        : clipPath(path), position(pos), volume(vol)
    {
    }
};

/// @brief Fired when a navmesh has been baked from scene geometry.
struct NavMeshBakedEvent : public Event
{
    int polyCount;
    float bakeTimeMs;

    NavMeshBakedEvent(int polys, float ms) : polyCount(polys), bakeTimeMs(ms) {}
};

} // namespace Vestige
