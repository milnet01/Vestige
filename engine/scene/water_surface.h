/// @file water_surface.h
/// @brief Water surface component with configurable waves, colors, and rendering parameters.
#pragma once

#include "scene/component.h"

#include <glm/glm.hpp>
#include <glad/gl.h>

#include <memory>

namespace Vestige
{

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

    // Surface detail
    float refractionStrength = 0.02f;
    float normalStrength = 1.0f;
    float dudvStrength = 0.02f;
    float flowSpeed = 0.3f;
    float specularPower = 128.0f;

    // Reflection
    float reflectionResolutionScale = 0.5f;  ///< 0.25 to 1.0
};

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
