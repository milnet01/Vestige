// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_rtl.cpp
/// @brief Tests for the lightweight RTL reorder (Phase 10 Localization L3).
///        See docs/phases/phase_10_localization_design.md § 5.4 (RTL-INV-1..4)
///        and § 8 tests 10-12.
#include <gtest/gtest.h>

#include "utils/rtl.h"

#include <cstdint>
#include <vector>

using namespace Vestige;

// Codepoints used below (decimal):
//   72 'H', 105 'i', 32 ' '
//   שלום = 1513 ש, 1500 ל, 1493 ו, 1501 ם
//   עולם = 1506 ע, 1493 ו, 1500 ל, 1501 ם

// Test 11 / RTL-INV-1 — pure-Latin passes through unchanged.
TEST(Rtl, LatinPassthrough)
{
    const std::vector<uint32_t> in = {72, 105};
    EXPECT_EQ(rtl::toVisualOrder(in), in);
}

// Test 10 / RTL-INV-2 — pure-Hebrew "שלום" is reversed.
TEST(Rtl, HebrewReverse)
{
    const std::vector<uint32_t> in = {1513, 1500, 1493, 1501};
    const std::vector<uint32_t> expected = {1501, 1493, 1500, 1513};
    EXPECT_EQ(rtl::toVisualOrder(in), expected);
}

// Test 12 / RTL-INV-3 — mixed "Hi שלום": "Hi " stays LTR, Hebrew run reverses.
// Documents the lightweight semantics (not full UAX#9 BiDi).
TEST(Rtl, MixedScriptRunReversal)
{
    const std::vector<uint32_t> in       = {72, 105, 32, 1513, 1500, 1493, 1501};
    const std::vector<uint32_t> expected = {72, 105, 32, 1501, 1493, 1500, 1513};
    EXPECT_EQ(rtl::toVisualOrder(in), expected);
}

// RTL-INV-4 — empty input → empty output, no crash.
TEST(Rtl, EmptyInput)
{
    const std::vector<uint32_t> in;
    EXPECT_TRUE(rtl::toVisualOrder(in).empty());
}

// Multi-word Hebrew "שלום עולם" reverses as ONE unit (interior space joins the
// run) so word order is correct — not just letters reversed within each word.
// Goes beyond the literal design test; essential for real Hebrew plaque text.
TEST(Rtl, MultiWordHebrewReversesAsUnit)
{
    const std::vector<uint32_t> in =
        {1513, 1500, 1493, 1501, 32, 1506, 1493, 1500, 1501};
    const std::vector<uint32_t> expected =
        {1501, 1500, 1493, 1506, 32, 1501, 1493, 1500, 1513};
    EXPECT_EQ(rtl::toVisualOrder(in), expected);
}

// A trailing space before an LTR char stays with the LTR side, not the RTL run.
TEST(Rtl, TrailingSpaceBeforeLatinStaysLtr)
{
    // "ש a" = aleph-bet?, no — single Hebrew shin, space, 'a'.
    const std::vector<uint32_t> in       = {1513, 32, 97};
    // Single-letter run → reversal is a no-op; space + 'a' untouched.
    const std::vector<uint32_t> expected = {1513, 32, 97};
    EXPECT_EQ(rtl::toVisualOrder(in), expected);
}

// containsRTL detects Hebrew presence.
TEST(Rtl, ContainsRtlDetection)
{
    EXPECT_FALSE(rtl::containsRTL({72, 105}));      // "Hi"
    EXPECT_TRUE(rtl::containsRTL({72, 1513}));       // "H" + shin
    EXPECT_FALSE(rtl::containsRTL({}));              // empty
}

// In-place form matches the value-returning form.
TEST(Rtl, InPlaceMatchesValueForm)
{
    std::vector<uint32_t> cps = {72, 105, 32, 1513, 1500, 1493, 1501};
    const std::vector<uint32_t> expected = rtl::toVisualOrder(cps);
    rtl::toVisualOrderInPlace(cps);
    EXPECT_EQ(cps, expected);
}
