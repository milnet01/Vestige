/// @file audio_system.cpp
/// @brief AudioSystem implementation.
#include "systems/audio_system.h"
#include "core/engine.h"
#include "core/logger.h"
#include "audio/audio_source_component.h"
#include "scene/component.h"

namespace Vestige
{

bool AudioSystem::initialize(Engine& engine)
{
    m_engine = &engine;

    if (!m_audioEngine.initialize())
    {
        Logger::warning("[AudioSystem] Audio engine initialization failed "
                        "— audio will be unavailable");
        // Non-fatal: engine continues without audio
    }

    Logger::info("[AudioSystem] Initialized");
    return true;
}

void AudioSystem::shutdown()
{
    m_audioEngine.shutdown();
    m_engine = nullptr;
    Logger::info("[AudioSystem] Shut down");
}

void AudioSystem::update(float /*deltaTime*/)
{
    if (!m_audioEngine.isAvailable() || !m_engine)
    {
        return;
    }

    // Sync listener to camera position
    Camera& camera = m_engine->getCamera();
    m_audioEngine.updateListener(
        camera.getPosition(),
        camera.getFront(),
        glm::vec3(0.0f, 1.0f, 0.0f));  // World up
}

std::vector<uint32_t> AudioSystem::getOwnedComponentTypes() const
{
    return {
        ComponentTypeId::get<AudioSourceComponent>()
    };
}

} // namespace Vestige
