// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file caption_map.h
/// @brief Phase 10.7 slice B3 — declarative clip-path → caption map.
///
/// Captions are data-driven: loading `assets/captions.json` populates
/// a `CaptionMap` keyed by audio clip path. When game code plays a
/// clip it asks the map whether a caption should be enqueued into the
/// engine's `SubtitleQueue`. No entry for the clip = silent (no log
/// spam, no default caption).
///
/// Schema (one-per-game):
///
/// ```json
/// {
///   "audio/dialogue/moses_01.wav": {
///     "category": "Dialogue",
///     "speaker": "Moses",
///     "text": "Draw near the mountain.",
///     "duration": 3.5
///   }
/// }
/// ```
///
/// `category` is the stringified `SubtitleCategory` — `"Dialogue"`,
/// `"Narrator"`, or `"SoundCue"`. `speaker` is optional (empty for
/// non-dialogue). `duration` is in seconds; non-positive values fall
/// back to a sensible default so authors do not need to think about
/// timing when writing a placeholder caption.
#pragma once

#include "ui/subtitle.h"

#include <string>
#include <unordered_map>

namespace Vestige
{

/// @brief Fallback duration used when the JSON entry omits `duration`
///        or specifies a non-positive value.
constexpr float DEFAULT_CAPTION_DURATION_SECONDS = 3.0f;

/// @brief A lookup table from audio clip path to caption template.
///
/// The map is pure data — load + lookup + enqueue-into-queue. It does
/// not own a subtitle queue and does not touch playback state.
class CaptionMap
{
public:
    CaptionMap() = default;

    /// @brief Loads entries from a JSON file on disk. Missing file is
    ///        treated as "no captions in this project" (no warning);
    ///        malformed JSON logs a warning and leaves the map empty.
    ///
    /// @return True if the file existed and parsed cleanly. False on
    ///         missing file (empty map) or parse failure.
    bool loadFromFile(const std::string& path);

    /// @brief Loads entries from an in-memory JSON string. Intended
    ///        for tests and for games that ship captions in an
    ///        archive rather than on disk.
    ///
    /// @return True on successful parse (even if the object is empty);
    ///         false on parse failure (map is cleared).
    bool loadFromString(const std::string& jsonText);

    /// @brief Look up a caption template for `clipPath`. Returns
    ///        nullptr if the clip has no mapped caption.
    const Subtitle* lookup(const std::string& clipPath) const;

    /// @brief If `clipPath` is mapped, enqueue the caption into
    ///        `queue`; otherwise no-op. Returns true on enqueue.
    bool enqueueFor(const std::string& clipPath, SubtitleQueue& queue) const;

    /// @brief Number of entries in the map. 0 for an empty map.
    std::size_t size() const { return m_entries.size(); }

    /// @brief Whether the map has no entries.
    bool empty() const { return m_entries.empty(); }

    /// @brief Removes all entries. The map is in the same state as a
    ///        default-constructed instance.
    void clear() { m_entries.clear(); }

private:
    std::unordered_map<std::string, Subtitle> m_entries;
};

/// @brief Parses a `SubtitleCategory` from its stringified form.
///        Unknown strings return `SubtitleCategory::Dialogue` (the
///        least surprising default for an authored caption).
SubtitleCategory parseSubtitleCategory(const std::string& s);

} // namespace Vestige
