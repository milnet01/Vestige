// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ambient_system.cpp
/// @brief 3D_E-S0016 — ambient zone beds and one-shots.
#include "systems/ambient_system.h"

#include "audio/ambient_zone_component.h"
#include "audio/audio_attenuation.h"
#include "audio/audio_engine.h"
#include "audio/audio_mixer.h"
#include "core/engine.h"
#include "core/system_registry.h"
#include "systems/audio_system.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "scene/scene_manager.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Vestige
{

// ----- Pure helpers ----------------------------------------------------

std::vector<AmbientBedAction> planAmbientBeds(
    std::unordered_map<std::uint32_t, AmbientBed>& beds,
    const std::vector<AmbientZoneFrame>& zones,
    const AmbientBedOwnsFn& owns,
    float deltaTime,
    bool deviceAvailable)
{
    std::vector<AmbientBedAction> actions;
    std::unordered_set<std::uint32_t> seen;

    for (const AmbientZoneFrame& zone : zones)
    {
        seen.insert(zone.entityId);
        AmbientBed& bed = beds[zone.entityId];

        bool owned = bed.source != 0 && owns(bed);
        if (bed.source != 0 && !owned)
        {
            // Stopped, or evicted and its source handed to another sound.
            bed.source = 0;
            bed.ticket = 0;
        }

        if (zone.weight <= 0.0f || zone.clip.empty())
        {
            if (owned)
            {
                actions.push_back({AmbientBedAction::Kind::Stop, zone.entityId,
                                   bed.source, {}, 0.0f});
            }
            bed = AmbientBed{};  // also resets the failed-start back-off
            continue;
        }

        if (bed.clip != zone.clip)
        {
            if (owned)
            {
                actions.push_back({AmbientBedAction::Kind::Stop, zone.entityId,
                                   bed.source, {}, 0.0f});
                owned = false;
            }
            bed = AmbientBed{};
            bed.clip = zone.clip;
        }

        if (owned)
        {
            actions.push_back({AmbientBedAction::Kind::SetVolume, zone.entityId,
                               bed.source, {}, zone.weight});
            continue;
        }

        bed.retryIn = std::max(0.0f, bed.retryIn - deltaTime);
        if (deviceAvailable && bed.retryIn <= 0.0f)
        {
            actions.push_back({AmbientBedAction::Kind::Start, zone.entityId,
                               0, zone.clip, zone.weight});
        }
    }

    for (auto it = beds.begin(); it != beds.end(); )
    {
        if (seen.count(it->first) != 0)
        {
            ++it;
            continue;
        }
        // Entity gone or component removed.
        if (it->second.source != 0 && owns(it->second))
        {
            actions.push_back({AmbientBedAction::Kind::Stop, it->first,
                               it->second.source, {}, 0.0f});
        }
        it = beds.erase(it);
    }
    return actions;
}

void recordAmbientBedStart(AmbientBed& bed, unsigned int source, std::uint64_t ticket)
{
    if (source == 0)
    {
        ++bed.failedStarts;
        bed.retryIn = ambientRetryDelay(bed.failedStarts);
        return;
    }
    bed.source       = source;
    bed.ticket       = ticket;
    bed.failedStarts = 0;
    bed.retryIn      = 0.0f;
}

float ambientRetryDelay(int failedStarts)
{
    if (failedStarts <= 0)
    {
        return 0.0f;
    }
    return std::min(60.0f, std::ldexp(1.0f, std::min(failedStarts, 16) - 1));
}

float advanceAmbientHour(float hour, float deltaTime, float hoursPerMinute)
{
    float h = std::fmod(hour + deltaTime * hoursPerMinute / 60.0f, 24.0f);
    if (h < 0.0f)
    {
        h += 24.0f;
    }
    return h;
}

void armAmbientOneShot(RandomOneShotScheduler& scheduler, const UniformSampleFn& sampleFn)
{
    float sample = sampleFn ? sampleFn() : 0.5f;
    sample = std::clamp(sample, 0.0f, 1.0f);
    const float minI = std::max(0.0f, scheduler.minIntervalSeconds);
    const float maxI = std::max(minI, scheduler.maxIntervalSeconds);
    scheduler.timeUntilNextFire = minI + (maxI - minI) * sample;
}

bool tickAmbientOneShot(RandomOneShotScheduler& scheduler, float weight,
                        float deltaTime, const UniformSampleFn& sampleFn)
{
    if (weight <= 0.0f)
    {
        return false;
    }
    return tickRandomOneShot(scheduler, deltaTime, sampleFn);
}

glm::vec3 ambientOneShotPosition(const glm::vec3& center, float coreRadius,
                                 float u1, float u2)
{
    constexpr float kTwoPi = 6.2831853f;
    const float r     = std::max(0.0f, coreRadius) * std::sqrt(std::clamp(u1, 0.0f, 1.0f));
    const float theta = kTwoPi * std::clamp(u2, 0.0f, 1.0f);
    return center + glm::vec3(r * std::cos(theta), 0.0f, r * std::sin(theta));
}

std::size_t ambientOneShotClipIndex(std::size_t count, float u)
{
    if (count == 0)
    {
        return 0;
    }
    const auto index = static_cast<std::size_t>(
        std::clamp(u, 0.0f, 1.0f) * static_cast<float>(count));
    return std::min(index, count - 1);
}

// ----- AmbientSystem ---------------------------------------------------

bool AmbientSystem::initialize(Engine& engine)
{
    m_engine = &engine;
    // All systems are registered before initializeAll(), so AudioSystem
    // resolves here, as it does for ReverbSystem. Null only in test
    // harnesses with no AudioSystem, where update() then no-ops.
    if (AudioSystem* audioSys = engine.getSystemRegistry().getSystem<AudioSystem>())
    {
        m_audioEngine = &audioSys->getAudioEngine();
    }
    m_rng.seed(std::random_device{}());
    return true;
}

void AmbientSystem::shutdown()
{
    m_beds.clear();
    m_oneShots.clear();
    m_engine      = nullptr;
    m_audioEngine = nullptr;
}

void AmbientSystem::update(float deltaTime)
{
    if (m_engine == nullptr || m_audioEngine == nullptr)
    {
        return;
    }
    m_hour = advanceAmbientHour(m_hour, deltaTime, m_hoursPerMinute);

    struct GatheredZone
    {
        std::uint32_t               entityId = 0;
        glm::vec3                   center{0.0f};
        const AmbientZoneComponent* comp     = nullptr;
    };
    std::vector<GatheredZone>        gathered;
    std::vector<AmbientZoneMixInput> inputs;

    Scene* scene = m_engine->getSceneManager().getActiveScene();
    if (scene != nullptr)
    {
        const glm::vec3 listenerPos = m_engine->getCamera().getPosition();
        scene->forEachEntity([&](Entity& entity)
        {
            const auto* comp = entity.getComponent<AmbientZoneComponent>();
            if (comp == nullptr)
            {
                return;
            }
            const glm::vec3 center = entity.getWorldPosition();
            gathered.push_back({entity.getId(), center, comp});
            inputs.push_back({comp->zone, comp->windows,
                              glm::length(center - listenerPos)});
        });
    }
    const std::vector<float> weights = computeAmbientZoneWeights(inputs, m_hour);

    // --- Beds (spec §4.3 step 3).
    std::vector<AmbientZoneFrame> frames;
    frames.reserve(gathered.size());
    for (std::size_t i = 0; i < gathered.size(); ++i)
    {
        frames.push_back({gathered[i].entityId, gathered[i].comp->zone.clipPath, weights[i]});
    }
    AudioEngine& audio = *m_audioEngine;
    const auto owns = [&audio](const AmbientBed& bed)
    {
        return bed.ticket != 0
            && audio.playbackTicket(bed.source) == bed.ticket
            && audio.isSourcePlaying(bed.source);
    };
    const std::vector<AmbientBedAction> actions =
        planAmbientBeds(m_beds, frames, owns, deltaTime, audio.isAvailable());
    for (const AmbientBedAction& a : actions)
    {
        switch (a.kind)
        {
        case AmbientBedAction::Kind::Start:
        {
            const unsigned int source = audio.playSound2D(
                a.clip, a.volume, AudioBus::Ambient, SoundPriority::Low, true);
            recordAmbientBedStart(m_beds[a.entityId], source, audio.playbackTicket(source));
            break;
        }
        case AmbientBedAction::Kind::SetVolume:
            audio.setSourceVolume(a.source, a.volume);
            break;
        case AmbientBedAction::Kind::Stop:
            audio.stopSound(a.source);
            break;
        }
    }

    // --- One-shots (spec §4.3 step 4).
    const UniformSampleFn sample = [this]() { return m_uniform(m_rng); };
    std::unordered_set<std::uint32_t> withOneShots;
    for (std::size_t i = 0; i < gathered.size(); ++i)
    {
        const AmbientZoneComponent& comp = *gathered[i].comp;
        if (comp.oneShotClips.empty())
        {
            continue;
        }
        const std::uint32_t id = gathered[i].entityId;
        withOneShots.insert(id);
        OneShotState& state = m_oneShots[id];
        state.scheduler.minIntervalSeconds = comp.minIntervalSeconds;
        state.scheduler.maxIntervalSeconds = comp.maxIntervalSeconds;
        if (!state.armed)
        {
            armAmbientOneShot(state.scheduler, sample);
            state.armed = true;
        }
        if (!tickAmbientOneShot(state.scheduler, weights[i], deltaTime, sample))
        {
            continue;
        }
        const std::string& clip =
            comp.oneShotClips[ambientOneShotClipIndex(comp.oneShotClips.size(), sample())];
        const glm::vec3 position = ambientOneShotPosition(
            gathered[i].center, comp.zone.coreRadius, sample(), sample());
        const float volume =
            comp.oneShotVolume * timeOfDayWindowWeight(comp.windows, m_hour);
        audio.playSoundSpatial(clip, position, AttenuationParams{}, volume, false,
                               AudioBus::Ambient, SoundPriority::Low);
    }
    for (auto it = m_oneShots.begin(); it != m_oneShots.end(); )
    {
        if (withOneShots.count(it->first) == 0)
        {
            it = m_oneShots.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

} // namespace Vestige
