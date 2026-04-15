// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file light_probe_manager.cpp
/// @brief Light probe manager implementation.
#include "renderer/light_probe_manager.h"
#include "core/logger.h"

namespace Vestige
{

LightProbeManager::LightProbeManager() = default;
LightProbeManager::~LightProbeManager() = default;

bool LightProbeManager::initialize(const std::string& assetPath)
{
    m_assetPath = assetPath;

    std::string cubeVert = assetPath + "/shaders/cubemap_render.vert.glsl";

    if (!m_irradianceShader.loadFromFiles(cubeVert,
            assetPath + "/shaders/irradiance_convolution.frag.glsl"))
    {
        Logger::error("LightProbeManager: failed to load irradiance shader");
        return false;
    }

    if (!m_prefilterShader.loadFromFiles(cubeVert,
            assetPath + "/shaders/prefilter.frag.glsl"))
    {
        Logger::error("LightProbeManager: failed to load prefilter shader");
        return false;
    }

    m_initialized = true;
    Logger::info("LightProbeManager initialized");
    return true;
}

int LightProbeManager::addProbe(const glm::vec3& position, const AABB& influenceVolume,
                                 float fadeDistance)
{
    auto probe = std::make_unique<LightProbe>();
    probe->setPosition(position);
    probe->setInfluenceAABB(influenceVolume);
    probe->setFadeDistance(fadeDistance);

    if (m_initialized)
    {
        probe->initialize(m_irradianceShader, m_prefilterShader);
    }

    int index = static_cast<int>(m_probes.size());
    m_probes.push_back(std::move(probe));

    Logger::info("Light probe " + std::to_string(index) + " added at ("
        + std::to_string(position.x) + ", "
        + std::to_string(position.y) + ", "
        + std::to_string(position.z) + ")");
    return index;
}

void LightProbeManager::generateProbe(int probeIndex, GLuint capturedCubemap)
{
    if (probeIndex < 0 || probeIndex >= static_cast<int>(m_probes.size()))
    {
        Logger::error("LightProbeManager::generateProbe: invalid index "
            + std::to_string(probeIndex));
        return;
    }

    m_probes[probeIndex]->generateFromCubemap(capturedCubemap);
}

ProbeAssignment LightProbeManager::assignProbe(const glm::vec3& worldPos) const
{
    ProbeAssignment best;

    for (const auto& probe : m_probes)
    {
        if (!probe->isReady())
        {
            continue;
        }

        float weight = probe->getBlendWeight(worldPos);
        if (weight > best.weight)
        {
            best.probe = probe.get();
            best.weight = weight;
        }
    }

    return best;
}

int LightProbeManager::getProbeCount() const
{
    return static_cast<int>(m_probes.size());
}

const LightProbe* LightProbeManager::getProbe(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_probes.size()))
    {
        return nullptr;
    }
    return m_probes[index].get();
}

void LightProbeManager::clear()
{
    m_probes.clear();
}

} // namespace Vestige
