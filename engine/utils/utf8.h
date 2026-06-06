// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file utf8.h
/// @brief Pure-function UTF-8 decoding for the text renderer (Phase 10
///        Localization, slice L1). No allocation on the hot decode path;
///        `decode()` is the convenience wrapper for random-access callers.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Vestige::utf8
{

/// @brief Decode result for one codepoint.
struct DecodeResult
{
    uint32_t codepoint;  // 0xFFFD on invalid sequence
    int      bytesRead;  // 1..4; always >= 1 so callers can advance
};

/// @brief Decode the codepoint at byte offset @p pos in @p s.
/// On invalid input, returns {0xFFFD (replacement char), 1}.
/// @pre pos < s.size().
DecodeResult decodeAt(std::string_view s, size_t pos);

/// @brief Decode an entire string into a codepoint vector.
/// Convenience wrapper around decodeAt for callers needing random access
/// (e.g. RTL reversal) rather than a stream-style walk.
std::vector<uint32_t> decode(std::string_view s);

/// @brief Encode a single codepoint into 1-4 bytes appended to @p out.
/// @return The number of bytes written. Codepoints > U+10FFFF (and UTF-16
///         surrogates U+D800..U+DFFF) are clamped to U+FFFD.
int encode(uint32_t codepoint, std::string& out);

/// @brief True iff @p cp is in the Hebrew block (U+0590..U+05FF) or the
///        Hebrew Presentation Forms block (U+FB1D..U+FB4F).
bool isHebrew(uint32_t cp);

/// @brief True iff @p cp is in the Greek block (U+0370..U+03FF) or Greek
///        Extended (U+1F00..U+1FFF) — polytonic glyphs live in the latter.
bool isGreek(uint32_t cp);

} // namespace Vestige::utf8
