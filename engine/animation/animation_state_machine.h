/// @file animation_state_machine.h
/// @brief Data-driven animation state machine with parameter-based transitions.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace Vestige
{

class SkeletonAnimator;

/// @brief Comparison operators for transition conditions.
enum class AnimCompareOp
{
    GREATER,      ///< param > threshold
    LESS,         ///< param < threshold
    GREATER_EQ,   ///< param >= threshold
    LESS_EQ,      ///< param <= threshold
    EQUAL,        ///< |param - threshold| < epsilon
    NOT_EQUAL     ///< |param - threshold| >= epsilon
};

/// @brief A condition that must be satisfied for a transition to fire.
struct AnimTransitionCondition
{
    std::string paramName;
    AnimCompareOp op = AnimCompareOp::GREATER;
    float threshold = 0.0f;
};

/// @brief A state in the animation state machine.
struct AnimState
{
    std::string name;
    int clipIndex = -1;        ///< Index into SkeletonAnimator::m_clips
    float playbackSpeed = 1.0f;
    bool loop = true;
};

/// @brief A transition between two states.
struct AnimTransition
{
    int fromState = -1;        ///< Source state index (-1 = "any state" wildcard)
    int toState = -1;          ///< Target state index
    float crossfadeDuration = 0.2f;  ///< Crossfade duration in seconds
    float exitTime = 0.0f;    ///< Minimum normalized time (0–1) in source clip before transition can fire
    std::vector<AnimTransitionCondition> conditions;  ///< ALL must be true (AND logic)
};

/// @brief Data-driven animation state machine.
///
/// States represent animation clips. Transitions define how/when to switch
/// between states based on named parameters (floats, bools, triggers).
/// The state machine drives a SkeletonAnimator, calling crossfadeTo() when
/// transitions fire.
class AnimationStateMachine
{
public:
    // --- Configuration ---

    /// @brief Adds a state. Returns the state index.
    int addState(const AnimState& state);

    /// @brief Adds a transition between states.
    void addTransition(const AnimTransition& transition);

    /// @brief Gets the number of states.
    int getStateCount() const;

    /// @brief Gets a state by index.
    const AnimState& getState(int index) const;

    // --- Parameters ---

    /// @brief Sets a float parameter.
    void setFloat(const std::string& name, float value);

    /// @brief Sets a bool parameter (stored as 1.0/0.0).
    void setBool(const std::string& name, bool value);

    /// @brief Sets a trigger (one-shot bool, auto-resets after consumed by a transition).
    void setTrigger(const std::string& name);

    /// @brief Gets a float parameter (0.0 if not set).
    float getFloat(const std::string& name) const;

    /// @brief Gets a bool parameter (false if not set).
    bool getBool(const std::string& name) const;

    // --- Runtime ---

    /// @brief Enters the initial state (index 0) and starts playback.
    void start(SkeletonAnimator& animator);

    /// @brief Evaluates transitions and drives the animator. Call once per frame.
    void update(SkeletonAnimator& animator, float deltaTime);

    // --- Query ---

    /// @brief Gets the current state index (-1 if not started).
    int getCurrentStateIndex() const;

    /// @brief Gets the current state name (empty if not started).
    const std::string& getCurrentStateName() const;

    /// @brief Returns true if the state machine has been started.
    bool isRunning() const;

private:
    bool evaluateCondition(const AnimTransitionCondition& cond) const;
    bool evaluateTransition(const AnimTransition& transition,
                            const SkeletonAnimator& animator) const;

    std::vector<AnimState> m_states;
    std::vector<AnimTransition> m_transitions;
    std::unordered_map<std::string, float> m_params;
    std::unordered_map<std::string, bool> m_triggers;
    int m_currentState = -1;

    static const std::string s_emptyString;
};

} // namespace Vestige
