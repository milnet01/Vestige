// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file lip_sync.cpp
/// @brief Lip sync player: Rhubarb track playback and amplitude fallback.
///
/// Supports two input modes:
///   1. Pre-baked track (Rhubarb JSON/TSV) with interpolation and coarticulation
///   2. Real-time amplitude analysis with RMS + spectral centroid
///
/// Output is fed to FacialAnimator::setLipSyncWeight() which handles the
/// emotion/lip-sync merge in mergeAndApply().
#include "animation/lip_sync.h"

#include "animation/facial_animation.h"
#include "core/logger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Vestige
{

LipSyncPlayer::LipSyncPlayer() = default;
LipSyncPlayer::~LipSyncPlayer() = default;

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

void LipSyncPlayer::update(float deltaTime)
{
    if (m_facialAnimator == nullptr)
    {
        return;
    }

    if (m_amplitudeMode)
    {
        applyAmplitudeWeights();
        return;
    }

    // Track mode
    if (m_state != LipSyncState::PLAYING || !m_hasTrack)
    {
        return;
    }

    m_time += deltaTime;

    // End of track
    if (m_time >= m_track.duration)
    {
        if (m_looping && m_track.duration > 0.0f)
        {
            m_time = std::fmod(m_time, m_track.duration);
        }
        else
        {
            m_time = m_track.duration;
            m_state = LipSyncState::STOPPED;
            m_facialAnimator->clearLipSync();
            return;
        }
    }

    applyTrackWeights();
}

std::unique_ptr<Component> LipSyncPlayer::clone() const
{
    auto copy = std::make_unique<LipSyncPlayer>();
    copy->m_track = m_track;
    copy->m_hasTrack = m_hasTrack;
    copy->m_looping = m_looping;
    copy->m_smoothing = m_smoothing;
    copy->m_amplitudeMode = m_amplitudeMode;
    // Don't copy playback state or animator pointer
    return copy;
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void LipSyncPlayer::setFacialAnimator(FacialAnimator* animator)
{
    m_facialAnimator = animator;
}

// ---------------------------------------------------------------------------
// Track loading — Rhubarb JSON
// ---------------------------------------------------------------------------

bool LipSyncPlayer::loadTrack(const std::string& jsonPath)
{
    // Rhubarb tracks are small (seconds of audio → KB of JSON); cap at 16 MB
    // to reject pathological inputs without eliminating legitimate tracks.
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::uintmax_t sz = fs::file_size(jsonPath, ec);
    if (ec)
    {
        Logger::error("LipSyncPlayer: Failed to stat track file: " + jsonPath);
        return false;
    }
    constexpr std::uintmax_t MAX_LIPSYNC_BYTES = 16ULL * 1024ULL * 1024ULL;
    if (sz > MAX_LIPSYNC_BYTES)
    {
        Logger::error("LipSyncPlayer: Track file exceeds "
            + std::to_string(MAX_LIPSYNC_BYTES) + "-byte cap: " + jsonPath
            + " (" + std::to_string(sz) + " bytes)");
        return false;
    }

    std::ifstream file(jsonPath);
    if (!file.is_open())
    {
        Logger::error("LipSyncPlayer: Failed to open track file: " + jsonPath);
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return loadTrackFromString(content);
}

bool LipSyncPlayer::loadTrackFromString(const std::string& json)
{
    try
    {
        auto doc = nlohmann::json::parse(json);

        m_track.cues.clear();
        m_track.duration = 0.0f;
        m_track.soundFile.clear();

        // Read metadata
        if (doc.contains("metadata"))
        {
            const auto& meta = doc["metadata"];
            if (meta.contains("soundFile"))
            {
                m_track.soundFile = meta["soundFile"].get<std::string>();
            }
            if (meta.contains("duration"))
            {
                m_track.duration = meta["duration"].get<float>();
            }
        }

        // Read mouth cues
        if (!doc.contains("mouthCues") || !doc["mouthCues"].is_array())
        {
            Logger::error("LipSyncPlayer: JSON missing 'mouthCues' array");
            return false;
        }

        for (const auto& cue : doc["mouthCues"])
        {
            LipSyncCue lsc;
            lsc.start = cue["start"].get<float>();
            lsc.end = cue["end"].get<float>();

            std::string value = cue["value"].get<std::string>();
            if (!value.empty())
            {
                lsc.viseme = VisemeMap::fromRhubarbChar(value[0]);
            }
            else
            {
                lsc.viseme = Viseme::X;
            }

            m_track.cues.push_back(lsc);

            // Track duration from last cue if not in metadata
            if (lsc.end > m_track.duration)
            {
                m_track.duration = lsc.end;
            }
        }

        // Sort by start time (should already be sorted, but be safe)
        std::sort(m_track.cues.begin(), m_track.cues.end(),
                  [](const LipSyncCue& a, const LipSyncCue& b)
                  {
                      return a.start < b.start;
                  });

        m_hasTrack = true;
        m_time = 0.0f;
        m_state = LipSyncState::STOPPED;

        Logger::info("LipSyncPlayer: Loaded track with " +
                     std::to_string(m_track.cues.size()) + " cues, duration " +
                     std::to_string(m_track.duration) + "s");
        return true;
    }
    catch (const std::exception& e)
    {
        Logger::error(std::string("LipSyncPlayer: Failed to parse JSON: ") + e.what());
        return false;
    }
}

// ---------------------------------------------------------------------------
// Track loading — Rhubarb TSV
// ---------------------------------------------------------------------------

bool LipSyncPlayer::loadTrackFromTSV(const std::string& tsv)
{
    m_track.cues.clear();
    m_track.duration = 0.0f;
    m_track.soundFile.clear();

    std::istringstream stream(tsv);
    std::string line;

    struct RawCue
    {
        float time;
        char shape;
    };
    std::vector<RawCue> rawCues;

    while (std::getline(stream, line))
    {
        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        std::istringstream ls(line);
        float time = 0.0f;
        std::string shape;
        if (ls >> time >> shape && !shape.empty())
        {
            rawCues.push_back({time, shape[0]});
        }
    }

    if (rawCues.empty())
    {
        Logger::error("LipSyncPlayer: TSV contains no cues");
        return false;
    }

    // Convert raw cues (timestamp + shape) to LipSyncCue (start/end/viseme).
    // Each Rhubarb TSV line is a start time — the cue runs until the next one.
    for (size_t i = 0; i < rawCues.size(); ++i)
    {
        LipSyncCue lsc;
        lsc.start = rawCues[i].time;
        lsc.end = (i + 1 < rawCues.size()) ? rawCues[i + 1].time : rawCues[i].time + 0.1f;
        lsc.viseme = VisemeMap::fromRhubarbChar(rawCues[i].shape);
        m_track.cues.push_back(lsc);

        if (lsc.end > m_track.duration)
        {
            m_track.duration = lsc.end;
        }
    }

    m_hasTrack = true;
    m_time = 0.0f;
    m_state = LipSyncState::STOPPED;

    Logger::info("LipSyncPlayer: Loaded TSV track with " +
                 std::to_string(m_track.cues.size()) + " cues, duration " +
                 std::to_string(m_track.duration) + "s");
    return true;
}

const LipSyncTrack* LipSyncPlayer::getTrack() const
{
    return m_hasTrack ? &m_track : nullptr;
}

// ---------------------------------------------------------------------------
// Playback control
// ---------------------------------------------------------------------------

void LipSyncPlayer::play()
{
    if (!m_hasTrack && !m_amplitudeMode)
    {
        Logger::warning("LipSyncPlayer: No track loaded and amplitude mode not enabled");
        return;
    }
    m_state = LipSyncState::PLAYING;
}

void LipSyncPlayer::pause()
{
    if (m_state == LipSyncState::PLAYING)
    {
        m_state = LipSyncState::PAUSED;
    }
}

void LipSyncPlayer::stop()
{
    m_state = LipSyncState::STOPPED;
    m_time = 0.0f;
    m_currentWeights.clear();
    if (m_facialAnimator != nullptr)
    {
        m_facialAnimator->clearLipSync();
    }
}

LipSyncState LipSyncPlayer::getState() const
{
    return m_state;
}

void LipSyncPlayer::setTime(float time)
{
    m_time = std::max(0.0f, time);
}

float LipSyncPlayer::getTime() const
{
    return m_time;
}

void LipSyncPlayer::setLooping(bool loop)
{
    m_looping = loop;
}

bool LipSyncPlayer::isLooping() const
{
    return m_looping;
}

// ---------------------------------------------------------------------------
// Smoothing
// ---------------------------------------------------------------------------

void LipSyncPlayer::setSmoothing(float factor)
{
    m_smoothing = std::clamp(factor, 0.0f, 1.0f);
}

float LipSyncPlayer::getSmoothing() const
{
    return m_smoothing;
}

// ---------------------------------------------------------------------------
// Amplitude mode
// ---------------------------------------------------------------------------

void LipSyncPlayer::enableAmplitudeMode()
{
    m_amplitudeMode = true;
}

void LipSyncPlayer::disableAmplitudeMode()
{
    m_amplitudeMode = false;
    m_analyzer.reset();
}

bool LipSyncPlayer::isAmplitudeMode() const
{
    return m_amplitudeMode;
}

void LipSyncPlayer::feedAudioSamples(const float* samples, size_t count, int sampleRate)
{
    m_analyzer.feedSamples(samples, count, sampleRate);
}

const AudioAnalyzer& LipSyncPlayer::getAnalyzer() const
{
    return m_analyzer;
}

// ---------------------------------------------------------------------------
// Track weight application
// ---------------------------------------------------------------------------

int LipSyncPlayer::findCueAtTime(float time) const
{
    if (m_track.cues.empty())
    {
        return -1;
    }

    // Binary search for the cue containing 'time'
    int lo = 0;
    int hi = static_cast<int>(m_track.cues.size()) - 1;

    while (lo <= hi)
    {
        int mid = lo + (hi - lo) / 2;
        if (time < m_track.cues[static_cast<size_t>(mid)].start)
        {
            hi = mid - 1;
        }
        else if (time >= m_track.cues[static_cast<size_t>(mid)].end)
        {
            lo = mid + 1;
        }
        else
        {
            return mid;
        }
    }

    return -1;
}

void LipSyncPlayer::applyTrackWeights()
{
    int idx = findCueAtTime(m_time);
    if (idx < 0)
    {
        // No cue at current time — smoothly decay all weights to rest pose.
        // Pass an empty map so smoothWeights decays every active weight
        // toward zero and eventually erases them, then applies the cleared
        // state to the FacialAnimator.
        std::unordered_map<std::string, float> restWeights;
        smoothWeights(restWeights);

        // Force-clear any residual sub-threshold weights that smoothing
        // has not yet erased, so the face fully returns to rest pose.
        if (m_facialAnimator != nullptr && m_currentWeights.empty())
        {
            m_facialAnimator->clearLipSync();
        }
        return;
    }

    const auto& currentCue = m_track.cues[static_cast<size_t>(idx)];
    Viseme currentViseme = currentCue.viseme;

    // Check for transition blending with next cue (last 20% of current cue)
    float cueDuration = currentCue.end - currentCue.start;
    float cueProgress = (m_time - currentCue.start) / std::max(cueDuration, 0.001f);
    float transitionZone = 0.8f;  // Start blending at 80% through the cue

    std::unordered_map<std::string, float> targetWeights;

    if (cueProgress > transitionZone &&
        static_cast<size_t>(idx) + 1 < m_track.cues.size())
    {
        // Blend with next cue for coarticulation
        Viseme nextViseme = m_track.cues[static_cast<size_t>(idx) + 1].viseme;
        float blendT = (cueProgress - transitionZone) / (1.0f - transitionZone);
        // Smoothstep for natural transition
        blendT = blendT * blendT * (3.0f - 2.0f * blendT);
        VisemeMap::blendWeights(currentViseme, nextViseme, blendT, targetWeights);
    }
    else
    {
        // Pure current viseme
        VisemeMap::blendWeights(currentViseme, currentViseme, 0.0f, targetWeights);
    }

    smoothWeights(targetWeights);
}

// ---------------------------------------------------------------------------
// Amplitude weight application
// ---------------------------------------------------------------------------

void LipSyncPlayer::applyAmplitudeWeights()
{
    Viseme estimated = m_analyzer.getEstimatedViseme();

    std::unordered_map<std::string, float> targetWeights;
    const auto& shape = VisemeMap::get(estimated);
    for (const auto& entry : shape.entries)
    {
        // Scale by amplitude so quiet speech has subtler movement
        float amplitudeScale = std::clamp(m_analyzer.getRMS() / 0.3f, 0.0f, 1.0f);
        targetWeights[entry.shapeName] = entry.weight * amplitudeScale;
    }

    smoothWeights(targetWeights);
}

// ---------------------------------------------------------------------------
// Exponential smoothing
// ---------------------------------------------------------------------------

void LipSyncPlayer::smoothWeights(const std::unordered_map<std::string, float>& target)
{
    // Decay existing weights toward zero for shapes not in target
    for (auto it = m_currentWeights.begin(); it != m_currentWeights.end();)
    {
        if (target.find(it->first) == target.end())
        {
            it->second += m_smoothing * (0.0f - it->second);
            if (it->second < 0.001f)
            {
                it = m_currentWeights.erase(it);
                continue;
            }
        }
        ++it;
    }

    // Blend target weights
    for (const auto& [name, weight] : target)
    {
        float current = 0.0f;
        auto it = m_currentWeights.find(name);
        if (it != m_currentWeights.end())
        {
            current = it->second;
        }
        m_currentWeights[name] = current + m_smoothing * (weight - current);
    }

    // Apply to FacialAnimator
    if (m_facialAnimator != nullptr)
    {
        m_facialAnimator->clearLipSync();
        for (const auto& [name, weight] : m_currentWeights)
        {
            if (weight > 0.001f)
            {
                m_facialAnimator->setLipSyncWeight(name, weight);
            }
        }
    }
}

} // namespace Vestige
