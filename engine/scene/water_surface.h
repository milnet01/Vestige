// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file water_surface.h
/// @brief Water surface component with configurable waves, colors, and rendering parameters.
#pragma once

#include "scene/component.h"
#include "utils/aabb.h"

#include <glm/glm.hpp>
#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace Vestige
{

/// @brief Water reflection rendering method.
enum class WaterReflectionMode
{
    NONE,     ///< No reflections (Fresnel + color blend only) — cheapest
    PLANAR,   ///< Planar reflection FBO — accurate but re-renders scene
    CUBEMAP   ///< Dynamic cubemap (1 face/frame) — cheaper, lower quality
};

/// @brief Configuration for a water surface — all tweakable parameters.
struct WaterSurfaceConfig
{
    // Geometry
    float width = 10.0f;
    float depth = 10.0f;
    int gridResolution = 128;  ///< NxN vertices (higher = smoother wave displacement)

    // Waves (up to 4 summed sine waves)
    static constexpr int MAX_WAVES = 4;
    struct Wave
    {
        float amplitude = 0.02f;
        float wavelength = 2.0f;
        float speed = 0.5f;
        float direction = 0.0f;  ///< Degrees
    };
    int numWaves = 2;
    Wave waves[MAX_WAVES] = {
        {0.02f, 2.0f, 0.5f, 0.0f},
        {0.01f, 1.5f, 0.3f, 45.0f},
        {0.015f, 3.0f, 0.4f, 90.0f},
        {0.005f, 1.0f, 0.6f, 135.0f}
    };

    // Colors
    glm::vec4 shallowColor = {0.1f, 0.4f, 0.5f, 0.8f};
    glm::vec4 deepColor = {0.0f, 0.1f, 0.2f, 1.0f};
    float depthDistance = 5.0f;

    /// Suspended sediment / organic matter, 0 = clear, 1 = thick pond scum.
    /// Drives how fast light is absorbed through the water column: clear water
    /// is strongly wavelength-selective (red dies within a metre, blue carries
    /// far, which is why open water reads blue), whereas silt and dissolved
    /// organics absorb broadly and hit blue hardest — so a murky pond turns
    /// green-brown and goes opaque within a few centimetres. 0 keeps the
    /// existing clear-water look for every surface that does not opt in.
    /// See waterAbsorptionCoefficients().
    float turbidity = 0.0f;

    // Surface detail
    float refractionStrength = 0.02f;
    float normalStrength = 1.0f;
    float dudvStrength = 0.02f;
    float flowSpeed = 0.3f;
    float specularPower = 128.0f;

    // Wind-driven ripples (design: "still unless very windy" — the meadow pond).
    // When true, the wave amplitude AND the procedural normal/dudv distortion are
    // scaled by the local wind (see waterWindRippleScale) so the surface is a flat
    // mirror in calm air and only ripples once it is genuinely windy. Default false
    // keeps the legacy always-on ripples for every other water surface (stay-in-lane).
    bool windDrivenAmplitude = false;

    // Caustics (animated light patterns on surfaces below water)
    bool causticsEnabled = true;        ///< Project caustic patterns onto submerged geometry.
    float causticsIntensity = 0.15f;    ///< Additive brightness of caustic patterns (0.0 to 1.0).
    float causticsScale = 0.1f;         ///< World-space tiling frequency (smaller = larger patterns).

    // Quality tier (0=Full, 1=Approximate, 2=Simple)
    int qualityTier = 0;  ///< Controls shader complexity: noise octaves, caustic samples

    // Reflection / refraction quality
    WaterReflectionMode reflectionMode = WaterReflectionMode::PLANAR;
    float reflectionResolutionScale = 0.25f;  ///< 0.1 to 1.0 (fraction of window resolution)
    bool refractionEnabled = true;            ///< Beer's law depth coloring (re-renders scene)
};

/// @brief Flat-unless-windy ripple gate for wind-driven water (the meadow pond).
/// Returns the [0,1] fraction of a surface's configured wave amplitude and normal
/// distortion to show at a given wind speed (m/s): 0 — a still mirror — at or below
/// WATER_WIND_CALM, ramping smoothly (smoothstep) to 1 — full configured ripple — at
/// or above WATER_WIND_FULL. Pure + GL-free so it is unit-testable and identical on
/// every call site. The knots sit high on the engine's 0–30 m/s wind scale so light
/// breezes leave the pond mirror-flat and it only stirs when it is genuinely windy.
/// TODO: revisit via Formula Workbench — WATER_WIND_CALM/FULL are art-directed.
inline float waterWindRippleScale(float windSpeedMetersPerSec)
{
    constexpr float WATER_WIND_CALM = 3.0f;   // m/s: at/below → flat mirror
    constexpr float WATER_WIND_FULL = 16.0f;  // m/s: at/above → full ripple
    const float t = std::clamp(
        (windSpeedMetersPerSec - WATER_WIND_CALM) / (WATER_WIND_FULL - WATER_WIND_CALM),
        0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);   // smoothstep
}

/// @brief Per-channel Beer's-law absorption coefficients for a turbidity value.
/// @details The clear-water base (0.4, 0.2, 0.1) is the classic ordering — red
/// absorbed fastest, blue slowest — which is why deep clear water reads blue.
/// Turbidity adds a second, blue-weighted term standing in for silt and
/// dissolved organics, which absorb broadly and strongest at short wavelengths;
/// the result is the green-brown of a real pond, opaque within a short depth.
/// Pure + GL-free so it is unit-testable and identical on every call site.
/// TODO: revisit via Formula Workbench — the turbid term is art-directed, not
/// fitted against measured attenuation spectra.
inline glm::vec3 waterAbsorptionCoefficients(float turbidity)
{
    constexpr glm::vec3 CLEAR_WATER(0.4f, 0.2f, 0.1f);
    // Green is the SMALLEST term: silt and algae absorb blue hardest and red
    // next, leaving a green transmission window around 550 nm. That window is
    // the whole reason a real pond reads green rather than brown or blue — an
    // earlier revision absorbed green harder than red and no amount of tinting
    // the deep colour could make it look like pond water.
    constexpr glm::vec3 TURBID_ADD(2.2f, 1.1f, 2.9f);
    return CLEAR_WATER + std::clamp(turbidity, 0.0f, 1.0f) * TURBID_ADD;
}

/// @brief World-space bounds of a water surface, for frustum culling its
///        reflection/refraction passes (3D_E-0028).
/// @details The mesh is built centred on the component's local origin,
/// spanning ±width/2 in X and ±depth/2 in Z at local Y = 0 (see
/// WaterSurfaceComponent::buildMesh), so the box is that rectangle inflated
/// vertically by the summed wave crest plus a fixed margin and pushed through
/// the world matrix. Deliberately conservative: culling a surface one frame
/// too early would let the water shader sample a stale reflection texture, so
/// the box errs large. Pure + GL-free so it is unit-testable.
inline AABB waterSurfaceWorldBounds(const WaterSurfaceConfig& config,
                                    const glm::mat4& worldMatrix)
{
    /// Vertical slack beyond the wave crest (metres) — absorbs normal/dudv
    /// distortion and any small terrain-follow offset at the shoreline.
    constexpr float WATER_BOUNDS_MARGIN = 0.5f;

    float crest = WATER_BOUNDS_MARGIN;
    const int waveCount = std::clamp(config.numWaves, 0, WaterSurfaceConfig::MAX_WAVES);
    for (int i = 0; i < waveCount; ++i)
    {
        crest += std::abs(config.waves[i].amplitude);
    }

    const float halfW = config.width * 0.5f;
    const float halfD = config.depth * 0.5f;
    const AABB local{glm::vec3(-halfW, -crest, -halfD), glm::vec3(halfW, crest, halfD)};
    return local.transformed(worldMatrix);
}

/// @brief Entity component that manages a water surface mesh and its parameters.
class WaterSurfaceComponent : public Component
{
public:
    WaterSurfaceComponent();
    ~WaterSurfaceComponent() override;

    // Non-copyable (owns OpenGL resources)
    WaterSurfaceComponent(const WaterSurfaceComponent&) = delete;
    WaterSurfaceComponent& operator=(const WaterSurfaceComponent&) = delete;

    /// @brief Creates a deep copy (rebuilds GPU mesh).
    std::unique_ptr<Component> clone() const override;

    /// @brief Gets the water configuration (for editor / serialization).
    WaterSurfaceConfig& getConfig();
    const WaterSurfaceConfig& getConfig() const;

    /// @brief Rebuilds the grid mesh if resolution or dimensions changed.
    /// Safe to call on a const reference (mesh is a mutable GPU cache).
    void rebuildMeshIfNeeded() const;

    /// @brief Gets the OpenGL VAO for rendering.
    GLuint getVao() const;

    /// @brief Gets the number of indices in the grid mesh.
    int getIndexCount() const;

    /// @brief Returns the water plane Y position in local space (always 0).
    float getLocalWaterY() const;

private:
    void buildMesh() const;
    void destroyMesh() const;

    WaterSurfaceConfig m_config;

    // Mutable GPU cache — rebuilt lazily when config dimensions change
    mutable float m_builtWidth = 0.0f;
    mutable float m_builtDepth = 0.0f;
    mutable int m_builtResolution = 0;
    mutable GLuint m_vao = 0;
    mutable GLuint m_vbo = 0;
    mutable GLuint m_ebo = 0;
    mutable int m_indexCount = 0;
};

} // namespace Vestige
