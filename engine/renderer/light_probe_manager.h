/// @file light_probe_manager.h
/// @brief Manages light probes — capture orchestration and per-entity probe assignment.
#pragma once

#include "renderer/light_probe.h"
#include "renderer/shader.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief Result of assigning a probe to a world position.
struct ProbeAssignment
{
    const LightProbe* probe = nullptr;
    float weight = 0.0f;  ///< 0.0 = use global IBL, 1.0 = use probe fully
};

/// @brief Manages all light probes in a scene.
///
/// Handles shader loading (irradiance/prefilter convolution shaders are shared
/// across all probes), probe creation, and per-entity probe assignment.
class LightProbeManager
{
public:
    LightProbeManager();
    ~LightProbeManager();

    /// @brief Loads shared convolution shaders. Call once during renderer init.
    bool initialize(const std::string& assetPath);

    /// @brief Creates a new probe at the given position with an AABB influence volume.
    /// @return Index of the created probe.
    int addProbe(const glm::vec3& position, const AABB& influenceVolume,
                 float fadeDistance = 2.0f);

    /// @brief Generates IBL maps for a probe from a pre-captured cubemap.
    /// @param probeIndex Index returned by addProbe().
    /// @param capturedCubemap HDR cubemap rendered from the probe's position.
    void generateProbe(int probeIndex, GLuint capturedCubemap);

    /// @brief Finds the best probe for a world position and returns the blend weight.
    ProbeAssignment assignProbe(const glm::vec3& worldPos) const;

    /// @brief Number of probes in the manager.
    int getProbeCount() const;

    /// @brief Access a probe by index.
    const LightProbe* getProbe(int index) const;

    /// @brief Removes all probes.
    void clear();

private:
    std::vector<std::unique_ptr<LightProbe>> m_probes;

    // Shared convolution shaders (loaded once, used by all probes)
    Shader m_irradianceShader;
    Shader m_prefilterShader;

    std::string m_assetPath;
    bool m_initialized = false;
};

} // namespace Vestige
