// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file sprite_animation.h
/// @brief SpriteAnimation — Aseprite-style per-frame-duration sprite sheet
/// animation with direction and loop control (Phase 9F-1).
///
/// Matches the data model Aseprite's JSON exporter produces: each frame
/// carries an independent duration in milliseconds, and a named clip
/// ("idle", "run", "jump") picks a contiguous range of frames plus a
/// playback direction (forward, reverse, ping-pong). A graph is a map of
/// named clips; calling `play("idle")` switches the active clip.
///
/// Headless: no GL, no ImGui. Tests drive `tick(dt)` directly.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace Vestige
{

/// @brief A single frame in a SpriteAnimationClip — a name (into the atlas)
/// and the duration it should stay on screen before advancing.
struct SpriteAnimationFrame
{
    std::string name;
    float       durationMs = 100.0f;
};

/// @brief Playback direction for a clip.
enum class SpriteAnimationDirection
{
    Forward,     ///< Play first → last, then loop or stop.
    Reverse,     ///< Play last → first.
    PingPong     ///< Forward then reverse, repeat.
};

/// @brief A named clip — a sequence of frames + direction + loop.
struct SpriteAnimationClip
{
    std::string                       name;
    std::vector<SpriteAnimationFrame> frames;
    SpriteAnimationDirection          direction = SpriteAnimationDirection::Forward;
    bool                              loop      = true;
};

/// @brief Sprite-sheet animation state machine — one per sprite component
/// (Aseprite-compatible).
class SpriteAnimation
{
public:
    /// @brief Adds or replaces a named clip.
    void addClip(SpriteAnimationClip clip);

    /// @brief Returns the number of registered clips.
    std::size_t clipCount() const { return m_clips.size(); }

    /// @brief Returns the names of all registered clips (declaration order).
    const std::vector<std::string>& clipNames() const;

    /// @brief Looks up a clip by name; returns nullptr if not found.
    const SpriteAnimationClip* findClip(const std::string& name) const;

    /// @brief Starts playing the named clip from its first frame.
    /// If the clip is already active, calling this resets it to the first
    /// frame; call `isPlayingClip(name)` first if you want to preserve
    /// playback state across identical transitions.
    void play(const std::string& clipName);

    /// @brief Stops playback. The current frame stays latched so renderers
    /// still have a name to render.
    void stop();

    /// @brief Advances playback by the given delta time (seconds).
    /// No-op while stopped or when the active clip has no frames.
    void tick(float deltaTimeSeconds);

    /// @brief Returns the frame name that should be rendered this instant.
    /// Empty string while no clip has ever been played.
    const std::string& currentFrameName() const { return m_currentFrameName; }

    /// @brief Name of the currently playing clip (empty if stopped or never
    /// started).
    const std::string& currentClipName() const { return m_currentClipName; }

    /// @brief Whether playback is active (a clip is selected and not stopped).
    bool isPlaying() const { return m_playing; }

    /// @brief Whether the named clip is the one currently playing.
    bool isPlayingClip(const std::string& clipName) const
    {
        return m_playing && m_currentClipName == clipName;
    }

    /// @brief Zero-based index of the active frame within the active clip.
    /// -1 if no clip is active.
    int currentFrameIndex() const { return m_frameIndex; }

    /// @brief Total milliseconds elapsed since the current frame started.
    /// Useful for cross-fades and per-frame events.
    float currentFrameElapsedMs() const { return m_frameElapsedMs; }

    /// @brief True if the most recent `tick()` advanced to a new frame.
    /// Cleared on the next `tick()`. Useful for gameplay code to detect
    /// footstep frames or attack-hit frames without polling a frame index.
    bool advancedLastTick() const { return m_advancedLastTick; }

private:
    /// @brief Advances m_frameIndex by one step in the active clip's
    /// direction, handling wrap-around for loops and ping-pong reversal.
    void advanceFrame(const SpriteAnimationClip& clip);

    std::unordered_map<std::string, SpriteAnimationClip> m_clips;
    std::vector<std::string> m_clipOrder;  // declaration order

    std::string m_currentClipName;
    std::string m_currentFrameName;
    int         m_frameIndex       = -1;
    float       m_frameElapsedMs   = 0.0f;
    bool        m_playing          = false;
    bool        m_advancedLastTick = false;

    // PingPong direction state: +1 = forward leg, -1 = reverse leg.
    // Only used when the active clip's direction is PingPong.
    int m_pingPongStep = 1;
};

} // namespace Vestige
