// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file motion_matcher.h
/// @brief Runtime motion matching driver component.
#pragma once

#include "animation/motion_database.h"
#include "animation/trajectory_predictor.h"
#include "animation/inertialization.h"

#include <memory>

namespace Vestige
{

class SkeletonAnimator;

/// @brief Runtime motion matching driver.
///
/// Ties together the motion database, trajectory predictor, and inertialization
/// blending to drive a SkeletonAnimator. Replaces AnimationStateMachine for
/// locomotion by continuously searching the database for the best matching
/// animation frame.
///
/// Usage:
///   1. Build a MotionDatabase from clips
///   2. Set the database and animator
///   3. Call update() each frame with player input
class MotionMatcher
{
public:
    /// @brief Sets the motion database.
    void setDatabase(std::shared_ptr<MotionDatabase> db);

    /// @brief Sets the animator to drive.
    void setAnimator(SkeletonAnimator* animator);

    /// @brief Per-frame update with player input.
    /// @param inputDir Desired movement direction (XZ, camera-relative).
    /// @param inputSpeed Desired speed (0 = stop).
    /// @param cameraYaw Camera Y rotation in radians.
    /// @param dt Delta time in seconds.
    void update(const glm::vec2& inputDir, float inputSpeed,
                float cameraYaw, float dt);

    /// @brief Gets the trajectory predictor for external configuration.
    TrajectoryPredictor& getTrajectoryPredictor();

    /// @brief Gets the inertialization state.
    const Inertialization& getInertialization() const;

    // --- Tuning Parameters ---

    /// @brief Sets how often the search runs (default 0.1s = 10 Hz).
    void setSearchInterval(float seconds);

    /// @brief Sets the cost threshold for triggering a transition (default 0.02).
    void setTransitionCost(float threshold);

    /// @brief Sets the inertialization decay halflife (default 0.1s).
    void setInertializationHalflife(float halflife);

    /// @brief Sets the trajectory spring halflife (default 0.27s).
    void setTrajectoryHalflife(float halflife);

    /// @brief Sets the tag mask for filtering database frames (0 = no filter).
    void setTagMask(uint32_t mask);

    // --- Debug / Query ---

    /// @brief Gets the cost of the last search result.
    float getLastSearchCost() const;

    /// @brief Gets the frame index of the last match.
    int getLastMatchFrame() const;

    /// @brief Gets the clip index of the last match.
    int getLastMatchClip() const;

    /// @brief Gets the time within the clip of the last match.
    float getLastMatchClipTime() const;

    /// @brief Gets the wall time of the last search in microseconds.
    float getSearchTimeMicros() const;

    /// @brief Gets the number of frames since the last transition.
    int getFramesSinceTransition() const;

    /// @brief Whether the matcher is active and has a valid database.
    bool isActive() const;

private:
    void performSearch();
    void buildQueryVector(float* query) const;

    std::shared_ptr<MotionDatabase> m_database;
    SkeletonAnimator* m_animator = nullptr;

    TrajectoryPredictor m_trajectoryPredictor;
    Inertialization m_inertialization;

    // Search timing
    float m_searchInterval = 0.1f;
    float m_searchAccumulator = 0.0f;

    // Transition parameters
    float m_transitionCostThreshold = 0.02f;
    float m_inertializationHalflife = 0.1f;
    uint32_t m_tagMask = 0;

    // Current playback state
    int m_currentFrame = -1;
    float m_currentFrameTime = 0.0f; // Time advanced since last match

    // Debug state
    float m_lastSearchCost = 0.0f;
    int m_lastMatchFrame = -1;
    int m_lastMatchClip = -1;
    float m_lastMatchClipTime = 0.0f;
    float m_searchTimeMicros = 0.0f;
    int m_framesSinceTransition = 0;

    // Reusable query buffer (avoids per-search heap allocation)
    std::vector<float> m_queryBuffer;
};

} // namespace Vestige
