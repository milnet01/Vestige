// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_nav_mesh_query.cpp
/// @brief Unit tests for NavMeshQuery's partial-result surface.
///
/// Phase 10.9 Slice 3 S8 — `findPath` used to collapse the Detour
/// `DT_PARTIAL_RESULT` flag into an "either full waypoints or empty"
/// return value, so agents silently arrived short of unreachable
/// targets with no hook for AI to notice or replan. The new
/// `findPathWithStatus` surfaces the partial flag via `PathResult`,
/// and a detail helper `isPartialPathStatus` makes the
/// flag-extraction logic independently testable without a live
/// Detour nav mesh.
///
/// These tests deliberately do not build a full Recast/Detour nav
/// mesh — that would require a Scene with triangle geometry and
/// couples unit tests to the heavy editor-time build path. Instead
/// we pin the two things that were missing:
///
///   1. the bit-extraction helper treats DT_FAILURE as "not partial"
///      even when DT_PARTIAL_RESULT is also set, so a failed query
///      never masquerades as a partial path;
///   2. the uninitialised-query path returns an empty `PathResult`
///      with `partial == false`, matching the legacy `findPath`
///      contract that empty means "no path".
///
/// Integration verification (real Detour nav mesh producing a
/// partial result) is covered by manual editor-launch testing of
/// the upcoming AI replanning consumer.

#include <gtest/gtest.h>

#include "navigation/nav_mesh_query.h"

// DT_* status bits — mirror DetourStatus.h. Re-declared here (rather
// than including the Detour header) so the test compiles without
// dragging Detour into the test's include chain.
namespace
{
constexpr unsigned int kDtFailure       = 1u << 31;
constexpr unsigned int kDtSuccess       = 1u << 30;
constexpr unsigned int kDtPartialResult = 1u << 6;
constexpr unsigned int kDtOutOfNodes    = 1u << 5;
}

using namespace Vestige;

// =============================================================================
// Phase 10.9 Slice 3 S8 — isPartialPathStatus helper.
//
// The helper exists so the Detour-status-to-bool translation is
// testable without a live nav mesh. Before S8 there was no helper
// and no test — the partial flag was silently discarded at the
// findPath call site.
// =============================================================================

TEST(NavMeshPartialStatus, SuccessWithoutPartialFlagIsNotPartial_S8)
{
    EXPECT_FALSE(detail::isPartialPathStatus(kDtSuccess));
}

TEST(NavMeshPartialStatus, SuccessWithPartialFlagIsPartial_S8)
{
    EXPECT_TRUE(detail::isPartialPathStatus(kDtSuccess | kDtPartialResult))
        << "A successful query that only reached a best-guess polygon must "
           "report partial=true so AI consumers can notice the target was "
           "unreachable instead of silently stopping short.";
}

TEST(NavMeshPartialStatus, FailureWithPartialFlagIsNotPartial_S8)
{
    // DT_FAILURE dominates: a failed query never produces a valid
    // path, so the partial flag on a failed status must not surface
    // as "partial path".
    EXPECT_FALSE(detail::isPartialPathStatus(kDtFailure | kDtPartialResult))
        << "A failed query must not masquerade as a partial path even when "
           "the partial bit is incidentally set — failure means no path at "
           "all, and the returned waypoint list is empty.";
}

TEST(NavMeshPartialStatus, SuccessWithUnrelatedDetailBitsIsNotPartial_S8)
{
    // Out-of-nodes is a separate detail bit; it must not be confused
    // with DT_PARTIAL_RESULT.
    EXPECT_FALSE(detail::isPartialPathStatus(kDtSuccess | kDtOutOfNodes));
}

TEST(NavMeshPartialStatus, BarePartialBitWithoutSuccessIsNotPartial_S8)
{
    // A status with only the partial bit set (no DT_SUCCESS) is not
    // a meaningful success — reject it rather than report partial.
    EXPECT_FALSE(detail::isPartialPathStatus(kDtPartialResult));
}

// =============================================================================
// Phase 10.9 Slice 3 S8 — findPathWithStatus on an uninitialised query.
//
// The legacy `findPath` returned an empty vector when the query was
// not initialised. The new `findPathWithStatus` must honour the
// same contract: empty waypoints + partial=false.
// =============================================================================

TEST(NavMeshQueryWithStatus, UninitialisedQueryReturnsEmptyWaypoints_S8)
{
    NavMeshQuery query;
    ASSERT_FALSE(query.isReady());

    const PathResult result =
        query.findPathWithStatus(glm::vec3(0.0f), glm::vec3(10.0f, 0.0f, 0.0f));

    EXPECT_TRUE(result.waypoints.empty());
}

TEST(NavMeshQueryWithStatus, UninitialisedQueryReportsNotPartial_S8)
{
    NavMeshQuery query;
    ASSERT_FALSE(query.isReady());

    const PathResult result =
        query.findPathWithStatus(glm::vec3(0.0f), glm::vec3(10.0f, 0.0f, 0.0f));

    EXPECT_FALSE(result.partial)
        << "An uninitialised query has no nav mesh at all, so the result "
           "is not a partial path — it's no path. Only a Detour success "
           "with DT_PARTIAL_RESULT should set partial=true.";
}

TEST(NavMeshQueryWithStatus, LegacyFindPathStillReturnsEmptyWhenUninitialised_S8)
{
    // Backward-compat: the original `findPath` call site in
    // NavigationSystem must keep working unchanged.
    NavMeshQuery query;
    const std::vector<glm::vec3> waypoints =
        query.findPath(glm::vec3(0.0f), glm::vec3(10.0f, 0.0f, 0.0f));
    EXPECT_TRUE(waypoints.empty());
}
