// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file rtl.h
/// @brief Lightweight right-to-left text reordering (Phase 10 Localization
///        slice L3). Per-run reversal for Hebrew — NOT the full Unicode
///        Bidirectional Algorithm (UAX#9). Sufficient for the project's
///        audience: pure-Hebrew and pure-Latin runs (biblical plaques + UI
///        strings). See docs/phases/phase_10_localization_design.md § 5.4 / § 6.
#pragma once

#include <cstdint>
#include <vector>

namespace Vestige::rtl
{

/// @brief Reorder logical-order codepoints into visual (left-to-right screen)
///        order. Each maximal run of RTL (Hebrew) codepoints — including the
///        spaces *between* Hebrew words — is reversed in place; LTR runs and
///        leading/trailing spaces are left untouched.
///
/// - Pure-LTR input → returned unchanged (visual == logical).
/// - Pure-RTL input → fully reversed.
/// - Mixed input → only the RTL run(s) reverse; e.g. "Hi שלום" keeps "Hi "
///   intact and reverses the Hebrew. A multi-word Hebrew phrase reverses as
///   one unit so word order is correct, not just per-word letters.
///
/// This is the lightweight reorder, not full UAX#9: mixed Hebrew + embedded
/// numerals/punctuation have a documented wrong-rendering case (§ 6 deferral 1).
std::vector<uint32_t> toVisualOrder(const std::vector<uint32_t>& logical);

/// @brief In-place form of toVisualOrder — reorders @p cps without allocating
///        a result vector. Used by the text renderer's hot path (it reorders a
///        reused scratch buffer).
void toVisualOrderInPlace(std::vector<uint32_t>& cps);

/// @brief True iff @p cps contains any RTL codepoint (Hebrew today; Arabic etc.
///        fold in later by extending the predicate).
bool containsRTL(const std::vector<uint32_t>& cps);

} // namespace Vestige::rtl
