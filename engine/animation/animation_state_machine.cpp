// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file animation_state_machine.cpp
/// @brief AnimationStateMachine implementation.
#include "animation/animation_state_machine.h"
#include "animation/skeleton_animator.h"

#include <cmath>

namespace Vestige
{

const std::string AnimationStateMachine::s_emptyString;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
int AnimationStateMachine::addState(const AnimState& state)
{
    int index = static_cast<int>(m_states.size());
    m_states.push_back(state);
    return index;
}

void AnimationStateMachine::addTransition(const AnimTransition& transition)
{
    m_transitions.push_back(transition);
}

int AnimationStateMachine::getStateCount() const
{
    return static_cast<int>(m_states.size());
}

const AnimState& AnimationStateMachine::getState(int index) const
{
    static const AnimState s_empty;
    if (index < 0 || index >= static_cast<int>(m_states.size()))
    {
        return s_empty;
    }
    return m_states[static_cast<size_t>(index)];
}

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------
void AnimationStateMachine::setFloat(const std::string& name, float value)
{
    m_params[name] = value;
}

void AnimationStateMachine::setBool(const std::string& name, bool value)
{
    m_params[name] = value ? 1.0f : 0.0f;
}

void AnimationStateMachine::setTrigger(const std::string& name)
{
    m_triggers[name] = true;
    m_params[name] = 1.0f;
}

float AnimationStateMachine::getFloat(const std::string& name) const
{
    auto it = m_params.find(name);
    return (it != m_params.end()) ? it->second : 0.0f;
}

bool AnimationStateMachine::getBool(const std::string& name) const
{
    auto it = m_params.find(name);
    return (it != m_params.end()) && (it->second > 0.5f);
}

// ---------------------------------------------------------------------------
// Runtime
// ---------------------------------------------------------------------------
void AnimationStateMachine::start(SkeletonAnimator& animator)
{
    if (m_states.empty())
    {
        return;
    }

    m_currentState = 0;
    const auto& state = m_states[0];
    animator.setSpeed(state.playbackSpeed);
    animator.setLooping(state.loop);
    animator.playIndex(state.clipIndex);
}

void AnimationStateMachine::update(SkeletonAnimator& animator, float /*deltaTime*/)
{
    if (m_currentState < 0 || m_states.empty())
    {
        return;
    }

    // Evaluate transitions from current state + "any state" (-1) transitions
    for (const auto& transition : m_transitions)
    {
        if (transition.fromState != m_currentState && transition.fromState != -1)
        {
            continue;
        }

        // Don't transition to current state
        if (transition.toState == m_currentState)
        {
            continue;
        }

        if (!evaluateTransition(transition, animator))
        {
            continue;
        }

        // Transition fires!
        if (transition.toState < 0 || transition.toState >= getStateCount())
            continue;
        m_currentState = transition.toState;
        const auto& targetState = m_states[static_cast<size_t>(m_currentState)];
        animator.setSpeed(targetState.playbackSpeed);
        animator.setLooping(targetState.loop);
        animator.crossfadeToIndex(targetState.clipIndex, transition.crossfadeDuration);

        // Consume triggers used by this transition
        for (const auto& cond : transition.conditions)
        {
            auto it = m_triggers.find(cond.paramName);
            if (it != m_triggers.end() && it->second)
            {
                it->second = false;
                m_params[cond.paramName] = 0.0f;
            }
        }

        break;  // Only fire one transition per frame
    }
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------
int AnimationStateMachine::getCurrentStateIndex() const
{
    return m_currentState;
}

const std::string& AnimationStateMachine::getCurrentStateName() const
{
    if (m_currentState >= 0 && m_currentState < static_cast<int>(m_states.size()))
    {
        return m_states[static_cast<size_t>(m_currentState)].name;
    }
    return s_emptyString;
}

bool AnimationStateMachine::isRunning() const
{
    return m_currentState >= 0;
}

// ---------------------------------------------------------------------------
// Condition evaluation
// ---------------------------------------------------------------------------
bool AnimationStateMachine::evaluateCondition(const AnimTransitionCondition& cond) const
{
    float value = getFloat(cond.paramName);

    // Triggers: treat as bool (check trigger map)
    auto trigIt = m_triggers.find(cond.paramName);
    if (trigIt != m_triggers.end())
    {
        // For triggers, "GREATER than 0.5" means "trigger is set"
        value = trigIt->second ? 1.0f : 0.0f;
    }

    constexpr float EPSILON = 0.0001f;

    switch (cond.op)
    {
    case AnimCompareOp::GREATER:    return value > cond.threshold;
    case AnimCompareOp::LESS:       return value < cond.threshold;
    case AnimCompareOp::GREATER_EQ: return value >= cond.threshold;
    case AnimCompareOp::LESS_EQ:    return value <= cond.threshold;
    case AnimCompareOp::EQUAL:      return std::fabs(value - cond.threshold) < EPSILON;
    case AnimCompareOp::NOT_EQUAL:  return std::fabs(value - cond.threshold) >= EPSILON;
    }

    return false;
}

bool AnimationStateMachine::evaluateTransition(
    const AnimTransition& transition,
    const SkeletonAnimator& animator) const
{
    // Check exit time constraint
    if (transition.exitTime > 0.0f)
    {
        int activeClip = animator.getActiveClipIndex();
        if (activeClip >= 0)
        {
            float duration = animator.getClip(activeClip)->getDuration();
            float normalized = (duration > 0.0f)
                ? animator.getCurrentTime() / duration : 1.0f;
            if (normalized < transition.exitTime)
            {
                return false;
            }
        }
    }

    // Check all conditions (AND logic)
    for (const auto& cond : transition.conditions)
    {
        if (!evaluateCondition(cond))
        {
            return false;
        }
    }

    return true;
}

} // namespace Vestige
