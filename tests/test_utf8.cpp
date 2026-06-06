// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

// Phase 10 Localization, slice L1 — UTF-8 decoder conformance.
// Invariants UTF8-INV-1..5 + verify-step tests 1-4 from
// docs/phases/phase_10_localization_design.md S 5.1 / S 8.

#include "utils/utf8.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace Vestige;

namespace
{
// Test-side convenience for UTF8-INV-5: re-encode every codepoint.
std::string encodeAll(const std::vector<uint32_t>& cps)
{
    std::string out;
    for (uint32_t cp : cps)
    {
        utf8::encode(cp, out);
    }
    return out;
}
} // namespace

// Test 1 — ASCII round-trips, one byte per glyph.
TEST(Utf8Decode, AsciiRoundTrip)
{
    const std::vector<uint32_t> expected = {72, 101, 108, 108, 111}; // "Hello"
    EXPECT_EQ(utf8::decode("Hello"), expected);

    // Each ASCII codepoint advances exactly one byte.
    std::string_view s = "Hello";
    for (size_t pos = 0; pos < s.size(); ++pos)
    {
        EXPECT_EQ(utf8::decodeAt(s, pos).bytesRead, 1);
    }
}

// Test 2 — Hebrew "shalom" U+05E9 U+05DC U+05D5 U+05DD, two bytes each.
TEST(Utf8Decode, HebrewRoundTrip)
{
    const std::string shalom = "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D";
    const std::vector<uint32_t> expected = {1513, 1500, 1493, 1501};
    EXPECT_EQ(utf8::decode(shalom), expected);
    EXPECT_EQ(utf8::decodeAt(shalom, 0).bytesRead, 2);
}

// Test 3 (representative) — basic + polytonic Greek codepoints.
TEST(Utf8Decode, GreekCodepoints)
{
    // U+03A0 (capital Pi, 2 bytes) then U+1F00 (polytonic alpha, 3 bytes).
    const std::string greek = "\xCE\xA0\xE1\xBC\x80";
    const std::vector<uint32_t> expected = {0x03A0, 0x1F00};
    EXPECT_EQ(utf8::decode(greek), expected);
    EXPECT_EQ(utf8::decodeAt(greek, 0).bytesRead, 2);
    EXPECT_EQ(utf8::decodeAt(greek, 2).bytesRead, 3);
}

// 4-byte sequence (emoji U+1F600) decodes whole.
TEST(Utf8Decode, FourByteCodepoint)
{
    const std::string grin = "\xF0\x9F\x98\x80";
    const auto r = utf8::decodeAt(grin, 0);
    EXPECT_EQ(r.codepoint, 0x1F600u);
    EXPECT_EQ(r.bytesRead, 4);
}

// Test 4 / UTF8-INV-4 — invalid second byte emits U+FFFD, advances 1.
TEST(Utf8Decode, InvalidByteEmitsReplacement)
{
    const std::string bad = "\xC3\x28"; // 0xC3 starts a 2-byte seq; 0x28 is not a continuation
    const auto r = utf8::decodeAt(bad, 0);
    EXPECT_EQ(r.codepoint, 0xFFFDu);
    EXPECT_EQ(r.bytesRead, 1);
}

// UTF8-INV-1 — forward progress: bytesRead >= 1 for any non-empty input.
TEST(Utf8Decode, ForwardProgress)
{
    for (const std::string& s : {std::string("A"),
                                 std::string("\xD7\xA9"),
                                 std::string("\xFF"),      // lone invalid byte
                                 std::string("\x80")})     // stray continuation
    {
        EXPECT_GE(utf8::decodeAt(s, 0).bytesRead, 1) << "input: " << s;
    }
}

// UTF8-INV-2 — iterating pos += bytesRead visits every codepoint exactly once.
TEST(Utf8Decode, VisitsEachCodepointOnce)
{
    const std::string mixed = "A\xD7\xA9\xCE\xA0!"; // 'A', shin, Pi, '!'
    std::vector<uint32_t> walked;
    for (size_t pos = 0; pos < mixed.size();)
    {
        const auto r = utf8::decodeAt(mixed, pos);
        walked.push_back(r.codepoint);
        pos += r.bytesRead;
    }
    EXPECT_EQ(walked, utf8::decode(mixed));
    EXPECT_EQ(walked.size(), 4u);
}

// UTF8-INV-3 — decode("") is empty.
TEST(Utf8Decode, EmptyStringIsEmpty)
{
    EXPECT_TRUE(utf8::decode("").empty());
}

// UTF8-INV-5 — decode(encodeAll(decode(s))) == decode(s) for valid input.
TEST(Utf8Decode, RoundTripViaEncode)
{
    const std::string s = "Hi \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D \xCE\xA0\xE1\xBC\x80 \xF0\x9F\x98\x80";
    const auto cps = utf8::decode(s);
    EXPECT_EQ(utf8::decode(encodeAll(cps)), cps);
}

// encode clamps out-of-range / surrogate codepoints to U+FFFD.
TEST(Utf8Encode, ClampsInvalidCodepoints)
{
    std::string out;
    EXPECT_EQ(utf8::encode(0x110000, out), 3); // above U+10FFFF -> U+FFFD (3 bytes)
    EXPECT_EQ(utf8::decodeAt(out, 0).codepoint, 0xFFFDu);

    out.clear();
    utf8::encode(0xD800, out); // lone surrogate -> U+FFFD
    EXPECT_EQ(utf8::decodeAt(out, 0).codepoint, 0xFFFDu);
}

// Script classification used by the FontStack routing + RTL detection.
TEST(Utf8Classify, HebrewAndGreek)
{
    EXPECT_TRUE(utf8::isHebrew(0x05D0));  // aleph
    EXPECT_TRUE(utf8::isHebrew(0xFB2A));  // presentation form
    EXPECT_FALSE(utf8::isHebrew(0x0041)); // 'A'

    EXPECT_TRUE(utf8::isGreek(0x03A0));   // capital Pi
    EXPECT_TRUE(utf8::isGreek(0x1F00));   // polytonic alpha
    EXPECT_FALSE(utf8::isGreek(0x0041));  // 'A'
}
