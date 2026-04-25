// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file lip_sync.h
/// @brief Lip sync player: pre-baked track playback and real-time amplitude fallback.
#pragma once

#include "experimental/animation/audio_analyzer.h"
#include "experimental/animation/viseme_map.h"
#include "scene/component.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vestige
{

class FacialAnimator;

/// @brief A single timed mouth cue from a Rhubarb lip sync track.
struct LipSyncCue
{
    float start;     ///< Start time in seconds.
    float end;       ///< End time in seconds.
    Viseme viseme;   ///< Mouth shape for this interval.
};

/// @brief A complete lip sync track: a sequence of timed mouth cues.
struct LipSyncTrack
{
    std::vector<LipSyncCue> cues;   ///< Ordered by start time.
    float duration = 0.0f;          ///< Total duration in seconds.
    std::string soundFile;          ///< Source audio filename (metadata).
};

/// @brief Lip sync playback state.
enum class LipSyncState : uint8_t
{
    STOPPED,
    PLAYING,
    PAUSED
};

/// @brief Component that drives lip sync on a FacialAnimator.
///
/// Two modes of operation:
///   1. Track mode (primary): Loads a pre-baked Rhubarb JSON/TSV file and plays
///      back the timed mouth cues with interpolation and coarticulation smoothing.
///   2. Amplitude mode (fallback): Accepts raw audio samples and uses RMS energy
///      plus spectral analysis to estimate mouth shapes in real time.
///
/// Usage (track mode):
/// @code
///     auto& lipSync = entity.addComponent<LipSyncPlayer>();
///     lipSync.setFacialAnimator(&facialAnimator);
///     lipSync.loadTrack("dialogue.json");
///     lipSync.play();
/// @endcode
///
/// Usage (amplitude mode):
/// @code
///     auto& lipSync = entity.addComponent<LipSyncPlayer>();
///     lipSync.setFacialAnimator(&facialAnimator);
///     lipSync.enableAmplitudeMode();
///     // Per audio callback:
///     lipSync.feedAudioSamples(buffer, count, 44100);
/// @endcode
class LipSyncPlayer : public Component
{
public:
    LipSyncPlayer();
    ~LipSyncPlayer() override;

    /// @brief Per-frame update: advances playback and applies viseme weights.
    void update(float deltaTime) override;

    /// @brief Deep copy for entity duplication.
    std::unique_ptr<Component> clone() const override;

    // --- Setup ---

    /// @brief Sets the target FacialAnimator that receives lip sync weights.
    void setFacialAnimator(FacialAnimator* animator);

    // --- Track mode ---

    /// @brief Loads a Rhubarb lip sync track from a JSON file.
    /// @param jsonPath Path to a Rhubarb-format `.json` (or `.tsv`) file.
    ///                 Subject to the process-wide path-sandbox configured
    ///                 via `setSandboxRoots` (Phase 10.9 Slice 5 D11);
    ///                 paths outside every configured root are rejected
    ///                 before the file is opened.
    /// @return true on success.
    bool loadTrack(const std::string& jsonPath);

    /// @brief Phase 10.9 Slice 5 D11 — installs the process-wide sandbox
    ///        roots used by `loadTrack`. Mirrors `AudioEngine` /
    ///        `ResourceManager`, but stored as a static so callers
    ///        configure it once per process rather than per-component
    ///        instance (LipSyncPlayer is a `Component` and is constructed
    ///        many times per scene).
    ///
    /// Empty roots (the default) means "no sandbox active" — any path
    /// the caller supplies is forwarded to the file reader, preserving
    /// backwards compatibility with the existing test fixtures.
    static void setSandboxRoots(std::vector<std::filesystem::path> roots);

    /// @brief Returns the currently configured sandbox roots.
    static const std::vector<std::filesystem::path>& getSandboxRoots();

    /// @brief Loads a Rhubarb lip sync track from a JSON string (for testing).
    /// @return true on success.
    bool loadTrackFromString(const std::string& json);

    /// @brief Loads a Rhubarb lip sync track from TSV data.
    /// @return true on success.
    bool loadTrackFromTSV(const std::string& tsv);

    /// @brief Returns the loaded track (or nullptr if none loaded).
    const LipSyncTrack* getTrack() const;

    // --- Playback control ---

    /// @brief Starts or resumes playback.
    void play();

    /// @brief Pauses playback at the current time.
    void pause();

    /// @brief Stops playback and resets to the beginning.
    void stop();

    /// @brief Returns the current playback state.
    LipSyncState getState() const;

    /// @brief Sets the playback position in seconds.
    void setTime(float time);

    /// @brief Gets the current playback position in seconds.
    float getTime() const;

    /// @brief Enables or disables looping.
    void setLooping(bool loop);

    /// @brief Returns true if looping is enabled.
    bool isLooping() const;

    // --- Smoothing ---

    /// @brief Sets the exponential smoothing factor [0, 1]. Default 0.15.
    /// Lower = smoother (more lag), higher = snappier (more jitter).
    void setSmoothing(float factor);

    /// @brief Gets the smoothing factor.
    float getSmoothing() const;

    // --- Amplitude mode ---

    /// @brief Switches to amplitude-based lip sync mode (real-time fallback).
    void enableAmplitudeMode();

    /// @brief Switches back to track-based mode.
    void disableAmplitudeMode();

    /// @brief Returns true if amplitude mode is active.
    bool isAmplitudeMode() const;

    /// @brief Feeds raw audio samples for amplitude analysis.
    /// @param samples Normalized float PCM [-1, 1].
    /// @param count   Number of samples.
    /// @param sampleRate Sample rate in Hz.
    void feedAudioSamples(const float* samples, size_t count, int sampleRate);

    /// @brief Gets the audio analyzer (for querying RMS, spectral data, etc.).
    const AudioAnalyzer& getAnalyzer() const;

private:
    /// @brief Finds the cue index active at the given time (binary search).
    int findCueAtTime(float time) const;

    /// @brief Applies viseme weights from track playback to the FacialAnimator.
    void applyTrackWeights();

    /// @brief Applies viseme weights from amplitude analysis to the FacialAnimator.
    void applyAmplitudeWeights();

    /// @brief Smooths the output weights using exponential filtering.
    void smoothWeights(const std::unordered_map<std::string, float>& target);

    FacialAnimator* m_facialAnimator = nullptr;
    LipSyncTrack m_track;
    bool m_hasTrack = false;

    // Playback
    LipSyncState m_state = LipSyncState::STOPPED;
    float m_time = 0.0f;
    bool m_looping = false;

    // Smoothing
    float m_smoothing = 0.15f;
    std::unordered_map<std::string, float> m_currentWeights;

    // Amplitude mode
    bool m_amplitudeMode = false;
    AudioAnalyzer m_analyzer;
};

} // namespace Vestige
