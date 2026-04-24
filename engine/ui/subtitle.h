// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file subtitle.h
/// @brief Phase 10 accessibility — subtitle / closed-caption queue for
///        dialogue, narration, and spatial-audio sound cues.
///
/// Headless model: a fixed-capacity FIFO of `Subtitle` entries that
/// counts down per-frame and drops expired entries. Rendering is
/// deliberately not part of this module — a future UI slice will read
/// `activeSubtitles()` and draw captions via the sprite batch /
/// text renderer. Keeping the queue headless means it can be unit
/// tested without GL context and reused by any future UI register
/// (HUD captions, log overlay, etc.).
///
/// Size presets follow the same Small / Medium / Large / XL ladder
/// that the roadmap bullet specifies, with the same 1.00 / 1.25 / 1.50
/// / 2.00 multipliers the UI scale presets use. Subtitle scale is
/// independent of UI scale so partially-sighted users can pick
/// e.g. XL subtitles with a 1.0× UI, or small subtitles with a 2.0× UI.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief Category tags so future styling / routing can treat
///        dialogue, narrator lines, and spatial sound cues
///        differently (colour, prefix, position on screen).
///
/// The queue itself treats all categories identically; the enum is
/// metadata only.
enum class SubtitleCategory
{
    Dialogue,   ///< A speaking character — speaker name rendered.
    Narrator,   ///< Disembodied narration / voice-over.
    SoundCue,   ///< Spatial sound effect ("[rustling]", "[footsteps behind]").
};

/// @brief Size presets exposed to users via the Settings menu.
///
/// The roadmap's Phase 10 bullet calls for Small / Medium / Large / XL;
/// multipliers mirror `UIScalePreset` so a user who is comfortable with
/// 1.5× UI naturally understands Large subtitles = 1.5× caption text.
enum class SubtitleSizePreset
{
    Small,   ///< 1.00× — baseline (typeCaption).
    Medium,  ///< 1.25× — mild boost.
    Large,   ///< 1.50× — minimum recommended for partially-sighted users.
    XL,      ///< 2.00× — large-text register.
};

/// @brief Returns the caption-text multiplier for a `SubtitleSizePreset`.
///
/// Consumers typically multiply `UITheme::typeCaption` by this factor to
/// get the final pixel size (and then additionally by any UI-wide scale
/// preset, since the two compose).
float subtitleScaleFactorOf(SubtitleSizePreset preset);

/// @brief Soft-wrap width for captions (Phase 10.7 §4.2 / P1).
///
/// Matches FCC / Game Accessibility Guidelines recommendations for
/// screen-comfortable line lengths (32–40 chars). 40 keeps short
/// dialogue lines on one row while capping longer lines at the
/// readability boundary.
inline constexpr std::size_t SUBTITLE_SOFT_WRAP_CHARS = 40;

/// @brief Hard line-count cap per caption (Phase 10.7 §4.2 / P1).
///
/// Research cites 2 on-screen lines as the readable ceiling (BBC
/// caption guidelines, Romero-Fresco 2019). Overflowing captions get
/// the last line truncated with an ellipsis so the user knows a
/// tail was cut rather than silently dropped.
inline constexpr std::size_t SUBTITLE_MAX_LINES = 2;

/// @brief Word-boundary soft-wrap with a line-count hard cap.
///
/// Greedy packing: each line accumulates words until appending the
/// next word (with a leading space, after the first word) would exceed
/// `maxCharsPerLine`; at that point a new line is started. Overlong
/// tokens — a single word longer than `maxCharsPerLine` — are
/// hard-broken at the limit rather than pushed whole onto a new line,
/// which would otherwise guarantee plate overflow.
///
/// `maxLines` caps the output vector length. If the full input would
/// produce more than `maxLines` lines, the last emitted line is
/// truncated and suffixed with a UTF-8 ellipsis ("…", `U+2026`) so
/// the user sees that content was trimmed, not silently lost. Empty
/// input returns an empty vector.
std::vector<std::string> wrapSubtitleText(
    const std::string& text,
    std::size_t maxCharsPerLine = SUBTITLE_SOFT_WRAP_CHARS,
    std::size_t maxLines = SUBTITLE_MAX_LINES);

/// @brief Authored caption entry passed to `SubtitleQueue::enqueue`.
struct Subtitle
{
    /// The caption text as it should appear on screen. For
    /// `SoundCue` entries, this is typically bracketed
    /// ("[thunder in the distance]") by convention.
    std::string text;

    /// Speaker name, rendered as a prefix for `Dialogue` entries
    /// and ignored for `Narrator` / `SoundCue`. Empty is valid.
    std::string speaker;

    /// How long the caption should remain visible, in seconds.
    /// Must be > 0. The queue counts this down per-frame and drops
    /// the entry when it reaches 0.
    float durationSeconds = 3.0f;

    /// Category tag — metadata for future styling.
    SubtitleCategory category = SubtitleCategory::Dialogue;

    /// Direction of the sound relative to the listener, in degrees
    /// (0 = front, 90 = right, 180 = behind, 270 = left). Set to
    /// `-1.0f` for non-positional captions. Spatial audio integration
    /// (deferred) can draw a direction hint / arrow glyph from this.
    float directionDegrees = -1.0f;
};

/// @brief An active entry tracked by the queue — adds a remaining-time
///        countdown on top of the authored fields.
struct ActiveSubtitle
{
    Subtitle subtitle;
    float remainingSeconds = 0.0f;
};

/// @brief FIFO queue of active captions with per-tick expiry.
///
/// Usage pattern: audio-event handlers (dialogue triggers, sound-cue
/// emitters) call `enqueue(...)`; the game loop calls `tick(dt)` each
/// frame; the UI renderer reads `activeSubtitles()` and draws each
/// entry. Clear with `clear()` on scene change.
class SubtitleQueue
{
public:
    /// Default cap on simultaneously-visible captions. Research on
    /// reading speed (Romero-Fresco 2019, BBC caption guidelines)
    /// converges on 2–3 lines max on screen at once for comfortable
    /// comprehension; 3 is the middle of that range.
    static constexpr int DEFAULT_MAX_CONCURRENT = 3;

    SubtitleQueue() = default;

    /// @brief Adds a caption to the queue. If the queue is already at
    ///        `maxConcurrent`, the oldest active entry is evicted
    ///        ("push newest, drop oldest") so dialogue that's actually
    ///        happening now wins over stale lines.
    void enqueue(const Subtitle& subtitle);

    /// @brief Advances every active entry's countdown by `deltaTime`
    ///        and removes entries whose remaining time has reached 0.
    ///        Call once per frame from the game loop.
    void tick(float deltaTime);

    /// @brief Returns the current set of active captions, oldest
    ///        first. The UI renderer iterates this list.
    const std::vector<ActiveSubtitle>& activeSubtitles() const { return m_active; }

    /// @brief Number of active captions right now.
    std::size_t size() const { return m_active.size(); }

    /// @brief True when nothing is showing.
    bool empty() const { return m_active.empty(); }

    /// @brief Drops every active caption — use on scene transitions so
    ///        lines from the previous scene don't bleed into the next.
    void clear() { m_active.clear(); }

    /// @brief Current cap on concurrent captions.
    int maxConcurrent() const { return m_maxConcurrent; }

    /// @brief Changes the concurrent cap. If the new cap is smaller
    ///        than the current active count, the oldest entries are
    ///        evicted immediately to bring the list within bounds.
    void setMaxConcurrent(int n);

    /// @brief Current size preset (defaults to Medium).
    SubtitleSizePreset sizePreset() const { return m_size; }

    /// @brief Selects the size preset. Readers consult `sizePreset()`
    ///        and `subtitleScaleFactorOf()` for pixel sizing.
    void setSizePreset(SubtitleSizePreset preset) { m_size = preset; }

private:
    std::vector<ActiveSubtitle> m_active;
    int m_maxConcurrent = DEFAULT_MAX_CONCURRENT;
    SubtitleSizePreset m_size = SubtitleSizePreset::Medium;
};

} // namespace Vestige
