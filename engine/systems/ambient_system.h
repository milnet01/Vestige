// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ambient_system.h
/// @brief 3D_E-S0016 — plays each scene ambient zone's looping bed and
///        random one-shots, following distance, priority and time of day.
#pragma once

#include "audio/audio_ambient.h"
#include "core/i_system.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vestige
{

class Engine;
class AudioEngine;

// ----- Pure helpers (tested without a device) --------------------------

/// @brief One zone's looping bed as held across frames. The zone owns the
///        bed only while `source` still carries `ticket` and is playing
///        (`AudioEngine::playbackTicket`, 3D_E-0739).
struct AmbientBed
{
    unsigned int  source       = 0;
    std::uint64_t ticket       = 0;
    std::string   clip;              ///< Clip of the current or last tried start.
    int           failedStarts = 0;  ///< Failed starts in a row.
    float         retryIn      = 0.0f;
};

/// @brief One zone's inputs to the bed planner for this frame.
struct AmbientZoneFrame
{
    std::uint32_t entityId = 0;
    std::string   clip;
    float         weight   = 0.0f;
};

/// @brief One change the planner asks `AmbientSystem` to make.
struct AmbientBedAction
{
    enum class Kind { Start, SetVolume, Stop };
    Kind          kind     = Kind::Start;
    std::uint32_t entityId = 0;
    unsigned int  source   = 0;   ///< SetVolume / Stop: the owned source.
    std::string   clip;           ///< Start: the clip to play.
    float         volume   = 0.0f;
};

/// @brief True while `bed` still owns its source.
using AmbientBedOwnsFn = std::function<bool(const AmbientBed&)>;

/// @brief Plans this frame's bed changes (spec §4.3 step 3) and updates
///        `beds`: a lost bed is forgotten, a zone absent from `zones` is
///        stopped and erased, and a zone waiting out a failed start's
///        back-off is not started. Never returns an action for a source
///        `owns` rejects.
std::vector<AmbientBedAction> planAmbientBeds(
    std::unordered_map<std::uint32_t, AmbientBed>& beds,
    const std::vector<AmbientZoneFrame>& zones,
    const AmbientBedOwnsFn& owns,
    float deltaTime,
    bool deviceAvailable);

/// @brief Records the result of a Start. Source 0 counts a failed start and
///        sets the back-off; anything else is the zone's new bed.
void recordAmbientBedStart(AmbientBed& bed, unsigned int source, std::uint64_t ticket);

/// @brief Wait after the n-th failed start in a row: min(2^(n−1), 60) s.
float ambientRetryDelay(int failedStarts);

/// @brief `hour + deltaTime × hoursPerMinute / 60`, wrapped into [0, 24).
float advanceAmbientHour(float hour, float deltaTime, float hoursPerMinute);

/// @brief Arms a fresh scheduler with an interval drawn as a fire draws it.
void armAmbientOneShot(RandomOneShotScheduler& scheduler, const UniformSampleFn& sampleFn);

/// @brief Advances `scheduler` only while `weight` is above 0; true on a fire.
bool tickAmbientOneShot(RandomOneShotScheduler& scheduler, float weight,
                        float deltaTime, const UniformSampleFn& sampleFn);

/// @brief A point within `coreRadius` of `center` on its horizontal plane,
///        from two uniform samples in [0, 1].
glm::vec3 ambientOneShotPosition(const glm::vec3& center, float coreRadius,
                                 float u1, float u2);

/// @brief Index into a list of `count` clips from a uniform sample in [0, 1].
std::size_t ambientOneShotClipIndex(std::size_t count, float u);

// ----- The system ------------------------------------------------------

/// @brief Drives every `AmbientZoneComponent` in the active scene.
///
/// `UpdatePhase::PostCamera`, registered after `ReverbSystem` and before
/// `AudioSystem`: it reads the settled listener, and the volumes it sets are
/// uploaded by `AudioSystem::update`'s `updateGains` the same frame. It
/// borrows `AudioSystem`'s `AudioEngine` at initialisation; without one,
/// `update` does nothing. Design: docs/specs/3D_E-S0016-ambient-soundscapes.md.
class AmbientSystem : public ISystem
{
public:
    AmbientSystem() = default;

    // -- ISystem interface --
    const std::string& getSystemName() const override { return m_name; }
    bool initialize(Engine& engine) override;
    void shutdown() override;
    void update(float deltaTime) override;
    UpdatePhase getUpdatePhase() const override { return UpdatePhase::PostCamera; }

    // -- Time of day --
    float hourOfDay() const { return m_hour; }
    void setHourOfDay(float hour) { m_hour = advanceAmbientHour(hour, 0.0f, 0.0f); }
    float hoursPerMinute() const { return m_hoursPerMinute; }
    void setHoursPerMinute(float rate) { m_hoursPerMinute = rate; }

private:
    struct OneShotState
    {
        RandomOneShotScheduler scheduler;
        bool                   armed = false;
    };

    static inline const std::string m_name = "Ambient";
    Engine*      m_engine      = nullptr;
    AudioEngine* m_audioEngine = nullptr;

    float m_hour           = 12.0f;
    float m_hoursPerMinute = 0.0f;

    std::unordered_map<std::uint32_t, AmbientBed>   m_beds;
    std::unordered_map<std::uint32_t, OneShotState> m_oneShots;

    std::mt19937                          m_rng;
    std::uniform_real_distribution<float> m_uniform{0.0f, 1.0f};
};

} // namespace Vestige
