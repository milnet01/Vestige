/// @file environment_forces.h
/// @brief Centralized environmental force queries for wind, weather, and buoyancy.
///
/// All physics/rendering systems query this instead of maintaining their own
/// wind state. Updated once per frame in the main loop. Uses a noise-modulated
/// wind field inspired by Ghost of Tsushima (GDC 2020).
#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace Vestige
{

/// @brief Global weather parameters.
struct WeatherState
{
    float temperature = 20.0f;        ///< Celsius
    float humidity = 0.5f;            ///< [0, 1]
    float precipitation = 0.0f;       ///< [0, 1] (0=none, 1=heavy rain)
    float wetness = 0.0f;             ///< [0, 1] surface wetness accumulation
    float cloudCover = 0.3f;          ///< [0, 1] affects ambient light level
    float airDensity = 1.225f;        ///< kg/m^3 (sea level 15C default)
};

/// @brief Centralized environmental force queries.
///
/// Provides position-dependent wind, weather, and buoyancy queries.
/// Owns a gust state machine that drives blow/calm cycles globally
/// so all consumers (cloth, foliage, particles, water) experience
/// consistent wind.
class EnvironmentForces
{
public:
    EnvironmentForces();

    // --- Wind queries (position-dependent) ---

    /// @brief Wind velocity at a world position (includes gusts + spatial noise).
    /// @param worldPos Position to sample.
    /// @return Wind velocity in m/s.
    glm::vec3 getWindVelocity(const glm::vec3& worldPos) const;

    /// @brief Aerodynamic drag force on a surface panel.
    /// @param worldPos Panel center position.
    /// @param surfaceArea Panel area in m^2.
    /// @param surfaceNormal Panel outward normal (normalized).
    /// @return Force vector in Newtons.
    glm::vec3 getWindForce(const glm::vec3& worldPos, float surfaceArea,
                           const glm::vec3& surfaceNormal) const;

    /// @brief Scalar wind speed at a position (magnitude of getWindVelocity).
    float getWindSpeed(const glm::vec3& worldPos) const;

    /// @brief Global base wind direction (normalized, before noise modulation).
    glm::vec3 getBaseWindDirection() const;

    /// @brief Global base wind strength (m/s, before gusts).
    float getBaseWindStrength() const;

    /// @brief Current gust intensity [0, 1] from the gust state machine.
    float getGustIntensity() const;

    /// @brief Current wind direction offset from gust direction variation.
    glm::vec3 getWindDirectionOffset() const;

    // --- Weather queries ---

    /// @brief Temperature at a position (Celsius). Defaults to 20C.
    float getTemperature(const glm::vec3& worldPos) const;

    /// @brief Relative humidity at a position [0, 1]. Defaults to 0.5.
    float getHumidity(const glm::vec3& worldPos) const;

    /// @brief Surface wetness [0, 1] (0=dry, 1=soaked). Defaults to 0.
    float getWetness(const glm::vec3& worldPos) const;

    /// @brief Current precipitation intensity [0, 1]. Defaults to 0.
    float getPrecipitationIntensity() const;

    /// @brief Air density at a position (kg/m^3). Defaults to 1.225.
    float getAirDensity(const glm::vec3& worldPos) const;

    // --- Fluid queries ---

    /// @brief Buoyancy force for a submerged object (Archimedes' principle).
    /// @param worldPos Object center.
    /// @param submergedVolume Volume below water surface (m^3).
    /// @param objectDensity Object density (kg/m^3).
    /// @return Upward buoyancy force vector.
    glm::vec3 getBuoyancy(const glm::vec3& worldPos, float submergedVolume,
                          float objectDensity) const;

    // --- Configuration ---

    /// @brief Sets the global wind direction (will be normalized).
    void setWindDirection(const glm::vec3& direction);

    /// @brief Sets the global wind strength (m/s).
    void setWindStrength(float strength);

    /// @brief Enables/disables gust state machine.
    void setGustsEnabled(bool enabled);

    /// @brief Sets spatial turbulence scale (larger = slower spatial variation).
    /// Default 10.0. Lower values create more localized wind variation.
    void setTurbulenceScale(float scale);

    /// @brief Sets weather state directly.
    void setWeather(const WeatherState& weather);

    /// @brief Returns the current weather state.
    const WeatherState& getWeather() const;

    /// @brief Sets the water surface Y level (for buoyancy calculations).
    void setWaterLevel(float y);

    /// @brief Returns the water surface Y level.
    float getWaterLevel() const;

    /// @brief Advances gust state machine and weather transitions.
    /// Call once per frame from the main loop.
    void update(float deltaTime);

    /// @brief Returns the elapsed time since last reset.
    float getElapsedTime() const;

    /// @brief Resets all state to defaults (wind, gusts, weather).
    void reset();

private:
    // Base wind
    glm::vec3 m_windDirection = glm::vec3(0.0f, 0.0f, -1.0f);
    float m_windStrength = 0.0f;
    float m_elapsed = 0.0f;

    // Gust state machine (migrated from ClothSimulator)
    bool m_gustsEnabled = true;
    float m_gustCurrent = 0.0f;       ///< Current gust intensity [0, 1]
    float m_gustTarget = 0.0f;        ///< Target gust intensity
    float m_gustTimer = 0.0f;         ///< Time until next target change
    float m_gustRampSpeed = 0.0f;     ///< Interpolation speed toward target

    // Direction variation
    glm::vec3 m_windDirOffset = glm::vec3(0.0f);
    glm::vec3 m_windDirTarget = glm::vec3(0.0f);
    float m_dirTimer = 0.0f;

    // Spatial noise
    float m_turbulenceScale = 10.0f;

    // Weather
    WeatherState m_weather;

    // Water level for buoyancy
    float m_waterLevel = -1000.0f;

    // RNG (LCG: fast, deterministic)
    uint32_t m_rngState = 54321u;

    float randFloat();
    float randRange(float lo, float hi);
    void updateGustState(float dt);

    /// @brief Hash noise for spatial wind variation. Returns [0, 1).
    static float hashNoise(float x, float y);

    /// @brief Computes spatial wind multiplier at a world position.
    float spatialWindFactor(const glm::vec3& worldPos) const;
};

} // namespace Vestige
