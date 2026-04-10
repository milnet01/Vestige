/// @file environment_forces.cpp
/// @brief Centralized environmental force system implementation.
#include "environment/environment_forces.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Vestige
{

// ============================================================================
// Construction / Reset
// ============================================================================

EnvironmentForces::EnvironmentForces()
{
    reset();
}

void EnvironmentForces::reset()
{
    m_windDirection = glm::vec3(0.0f, 0.0f, -1.0f);
    m_windStrength = 0.0f;
    m_elapsed = 0.0f;

    m_gustsEnabled = true;
    m_gustCurrent = 0.0f;
    m_gustTarget = 0.0f;
    m_gustTimer = 0.0f;
    m_gustRampSpeed = 0.0f;

    m_windDirOffset = glm::vec3(0.0f);
    m_windDirTarget = glm::vec3(0.0f);
    m_dirTimer = 0.0f;

    m_turbulenceScale = 10.0f;
    m_weather = WeatherState{};
    m_waterLevel = -1000.0f;
    m_rngState = 54321u;
}

// ============================================================================
// RNG (identical LCG to ClothSimulator for deterministic behavior)
// ============================================================================

float EnvironmentForces::randFloat()
{
    m_rngState = m_rngState * 1664525u + 1013904223u;
    return static_cast<float>(m_rngState & 0x00FFFFFFu) / 16777216.0f;
}

float EnvironmentForces::randRange(float lo, float hi)
{
    return lo + randFloat() * (hi - lo);
}

// ============================================================================
// Hash noise (spatial wind variation)
// ============================================================================

float EnvironmentForces::hashNoise(float x, float y)
{
    // Integer bit-mixing hash — same quality as sin-based but ~6x faster on CPU
    uint32_t ix, iy;
    std::memcpy(&ix, &x, sizeof(uint32_t));
    std::memcpy(&iy, &y, sizeof(uint32_t));

    uint32_t h = ix * 0x45d9f3bu + iy * 0x119de1f3u;
    h ^= h >> 16;
    h *= 0x45d9f3bu;
    h ^= h >> 16;

    return static_cast<float>(h & 0x00FFFFFFu) / 16777216.0f;  // [0, 1)
}

float EnvironmentForces::spatialWindFactor(const glm::vec3& worldPos) const
{
    float invScale = 1.0f / m_turbulenceScale;

    // Two octaves of hash noise for spatial variation
    float n1 = hashNoise(worldPos.x * invScale + m_elapsed * 0.3f,
                          worldPos.z * invScale + m_elapsed * 0.2f);
    float n2 = hashNoise(worldPos.x * invScale * 2.1f + m_elapsed * 0.7f,
                          worldPos.z * invScale * 1.7f + m_elapsed * 0.5f);

    // Combine: range [0.5, 1.5] centered at 1.0
    return 0.5f + n1 * 0.6f + n2 * 0.4f;
}

// ============================================================================
// Gust state machine (migrated from ClothSimulator::updateGustState)
// ============================================================================

void EnvironmentForces::updateGustState(float dt)
{
    if (!m_gustsEnabled)
    {
        m_gustCurrent = 1.0f;  // No gusts = constant full strength
        return;
    }

    // --- Gust strength state machine ---
    m_gustTimer -= dt;
    if (m_gustTimer <= 0.0f)
    {
        if (m_gustTarget < 0.3f)
        {
            // Was calm -> start a gust
            m_gustTarget = randRange(0.5f, 1.0f);
            m_gustTimer = randRange(1.5f, 4.0f);
            m_gustRampSpeed = randRange(1.5f, 4.0f);
        }
        else
        {
            // Was gusting -> go calm
            // Calm periods need 3-7 seconds for pendulum-like fabric
            // to swing back to natural hanging position.
            m_gustTarget = 0.0f;
            m_gustTimer = randRange(3.0f, 7.0f);
            m_gustRampSpeed = randRange(3.0f, 6.0f);
        }
    }

    // Smoothly interpolate toward target
    float diff = m_gustTarget - m_gustCurrent;
    float step = m_gustRampSpeed * dt;
    if (std::abs(diff) < step)
    {
        m_gustCurrent = m_gustTarget;
    }
    else
    {
        m_gustCurrent += (diff > 0.0f ? step : -step);
    }

    // --- Direction shift state machine ---
    m_dirTimer -= dt;
    if (m_dirTimer <= 0.0f)
    {
        float shift = randFloat();
        if (shift < 0.15f)
        {
            // Big direction change (15% chance)
            m_windDirTarget = glm::vec3(
                randRange(-0.8f, 0.8f),
                randRange(-0.3f, 0.3f),
                randRange(-0.5f, 0.4f)
            );
        }
        else if (shift < 0.5f)
        {
            // Medium shift (35% chance)
            m_windDirTarget = glm::vec3(
                randRange(-0.4f, 0.4f),
                randRange(-0.15f, 0.15f),
                randRange(-0.1f, 0.2f)
            );
        }
        else
        {
            // Small drift (50% chance)
            m_windDirTarget = glm::vec3(
                randRange(-0.15f, 0.15f),
                randRange(-0.05f, 0.05f),
                0.0f
            );
        }
        m_dirTimer = randRange(1.0f, 5.0f);
    }

    // Smoothly interpolate direction offset
    glm::vec3 dirDiff = m_windDirTarget - m_windDirOffset;
    float dirLen = glm::length(dirDiff);
    float dirStep = 0.8f * dt;
    if (dirLen <= dirStep || dirLen < 1e-6f)
    {
        m_windDirOffset = m_windDirTarget;
    }
    else
    {
        m_windDirOffset += dirDiff * (dirStep / dirLen);
    }
}

// ============================================================================
// Update (call once per frame)
// ============================================================================

void EnvironmentForces::update(float deltaTime)
{
    m_elapsed += deltaTime;
    updateGustState(deltaTime);

    // Cache flutter value (depends only on m_elapsed — constant for all queries this frame)
    m_cachedFlutter = 1.0f + 0.15f * std::sin(m_elapsed * 7.3f + 1.1f)
                          + 0.08f * std::sin(m_elapsed * 13.7f + 3.2f);

    // Accumulate/drain surface wetness based on precipitation
    if (m_weather.precipitation > 0.0f)
    {
        // Wetting rate: full precipitation saturates in ~30 seconds
        m_weather.wetness = std::min(1.0f,
            m_weather.wetness + m_weather.precipitation * deltaTime / 30.0f);
    }
    else
    {
        // Drying rate: fully wet surface dries in ~120 seconds
        m_weather.wetness = std::max(0.0f,
            m_weather.wetness - deltaTime / 120.0f);
    }
}

// ============================================================================
// Wind queries
// ============================================================================

glm::vec3 EnvironmentForces::getWindVelocity(const glm::vec3& worldPos) const
{
    if (m_windStrength <= 0.0f)
    {
        return glm::vec3(0.0f);
    }

    float gustStrength = m_gustCurrent * m_cachedFlutter;

    // Effective direction with gust offset
    glm::vec3 effectiveDir = m_windDirection + m_windDirOffset;

    // Spatial variation
    float spatial = spatialWindFactor(worldPos);

    return effectiveDir * (m_windStrength * gustStrength * spatial);
}

float EnvironmentForces::getWindSpeed(const glm::vec3& worldPos) const
{
    return glm::length(getWindVelocity(worldPos));
}

glm::vec3 EnvironmentForces::getWindForce(const glm::vec3& worldPos,
                                           float surfaceArea,
                                           const glm::vec3& surfaceNormal) const
{
    glm::vec3 windVel = getWindVelocity(worldPos);
    float vDotN = glm::dot(windVel, surfaceNormal);

    // Aerodynamic drag: F = 0.5 * Cd * rho * A * (v . n) * n
    // Using Cd=0.47 (sphere-like, general purpose) and air density from weather
    return surfaceNormal * (0.5f * 0.47f * m_weather.airDensity * surfaceArea * vDotN);
}

glm::vec3 EnvironmentForces::getBaseWindDirection() const
{
    return m_windDirection;
}

float EnvironmentForces::getBaseWindStrength() const
{
    return m_windStrength;
}

float EnvironmentForces::getGustIntensity() const
{
    return m_gustCurrent;
}

glm::vec3 EnvironmentForces::getWindDirectionOffset() const
{
    return m_windDirOffset;
}

// ============================================================================
// Weather queries
// ============================================================================

float EnvironmentForces::getTemperature(const glm::vec3& /*worldPos*/) const
{
    return m_weather.temperature;
}

float EnvironmentForces::getHumidity(const glm::vec3& /*worldPos*/) const
{
    return m_weather.humidity;
}

float EnvironmentForces::getWetness(const glm::vec3& /*worldPos*/) const
{
    return m_weather.wetness;
}

float EnvironmentForces::getPrecipitationIntensity() const
{
    return m_weather.precipitation;
}

float EnvironmentForces::getAirDensity(const glm::vec3& /*worldPos*/) const
{
    return m_weather.airDensity;
}

// ============================================================================
// Fluid queries
// ============================================================================

glm::vec3 EnvironmentForces::getBuoyancy(const glm::vec3& worldPos,
                                          float submergedVolume,
                                          float /*objectDensity*/) const
{
    // Above water: no buoyancy
    if (worldPos.y >= m_waterLevel)
    {
        return glm::vec3(0.0f);
    }

    // Archimedes: F_buoyancy = rho_fluid * V_submerged * g (upward)
    constexpr float WATER_DENSITY = 997.0f;  // kg/m^3 at 25C
    constexpr float GRAVITY = 9.81f;
    return glm::vec3(0.0f, WATER_DENSITY * submergedVolume * GRAVITY, 0.0f);
}

// ============================================================================
// Configuration
// ============================================================================

void EnvironmentForces::setWindDirection(const glm::vec3& direction)
{
    float len = glm::length(direction);
    if (len > 0.0001f)
    {
        m_windDirection = direction / len;
    }
}

void EnvironmentForces::setWindStrength(float strength)
{
    m_windStrength = std::max(0.0f, strength);
}

void EnvironmentForces::setGustsEnabled(bool enabled)
{
    m_gustsEnabled = enabled;
}

void EnvironmentForces::setTurbulenceScale(float scale)
{
    m_turbulenceScale = std::max(0.1f, scale);
}

void EnvironmentForces::setWeather(const WeatherState& weather)
{
    m_weather = weather;
}

const WeatherState& EnvironmentForces::getWeather() const
{
    return m_weather;
}

void EnvironmentForces::setWaterLevel(float y)
{
    m_waterLevel = y;
}

float EnvironmentForces::getWaterLevel() const
{
    return m_waterLevel;
}

float EnvironmentForces::getElapsedTime() const
{
    return m_elapsed;
}

} // namespace Vestige
