/// @file skeleton_animator.h
/// @brief Component that plays skeletal animations and computes bone matrices.
#pragma once

#include "animation/skeleton.h"
#include "animation/animation_clip.h"
#include "scene/component.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief Root motion application mode.
enum class RootMotionMode
{
    IGNORE,              ///< Root motion discarded; game code controls movement
    APPLY_TO_TRANSFORM   ///< Delta applied directly to entity position/rotation
};

/// @brief Component that drives skeletal animation playback on an entity.
/// Attach to the root entity of a skinned model. Each frame, update() advances
/// playback, samples keyframes, walks the joint hierarchy, and computes the
/// final bone matrices for GPU upload.
///
/// Supports crossfade blending between two clips and root motion extraction.
class SkeletonAnimator : public Component
{
public:
    SkeletonAnimator();
    ~SkeletonAnimator() override;

    /// @brief Per-frame update — advances playback and recomputes bone matrices.
    void update(float deltaTime) override;

    /// @brief Deep copy for entity duplication.
    std::unique_ptr<Component> clone() const override;

    // --- Skeleton ---

    /// @brief Sets the skeleton (shared — multiple instances can share one).
    void setSkeleton(std::shared_ptr<Skeleton> skeleton);

    /// @brief Gets the skeleton.
    const std::shared_ptr<Skeleton>& getSkeleton() const;

    // --- Clip Management ---

    /// @brief Adds an animation clip (shared between instances).
    void addClip(std::shared_ptr<AnimationClip> clip);

    /// @brief Gets the number of available clips.
    int getClipCount() const;

    /// @brief Gets a clip by index.
    const std::shared_ptr<AnimationClip>& getClip(int index) const;

    /// @brief Plays a clip by name. Restarts from the beginning (instant switch).
    void play(const std::string& clipName);

    /// @brief Plays a clip by index. Restarts from the beginning (instant switch).
    void playIndex(int index);

    /// @brief Stops playback.
    void stop();

    /// @brief Pauses or unpauses.
    void setPaused(bool paused);

    /// @brief Returns true if paused.
    bool isPaused() const;

    /// @brief Sets looping.
    void setLooping(bool loop);

    /// @brief Returns true if looping.
    bool isLooping() const;

    /// @brief Sets playback speed multiplier (1.0 = normal).
    void setSpeed(float speed);

    /// @brief Gets playback speed.
    float getSpeed() const;

    /// @brief Returns true if currently playing.
    bool isPlaying() const;

    /// @brief Gets the current playback time.
    float getCurrentTime() const;

    /// @brief Gets the active clip index (-1 if none).
    int getActiveClipIndex() const;

    // --- Crossfade Blending ---

    /// @brief Crossfade from the current clip to a new clip by name.
    /// @param clipName Name of the target clip.
    /// @param duration Crossfade duration in seconds (0 = instant switch).
    void crossfadeTo(const std::string& clipName, float duration);

    /// @brief Crossfade from the current clip to a new clip by index.
    void crossfadeToIndex(int index, float duration);

    /// @brief Returns true if currently crossfading between two clips.
    bool isCrossfading() const;

    // --- Root Motion ---

    /// @brief Sets the root motion mode.
    void setRootMotionMode(RootMotionMode mode);

    /// @brief Gets the current root motion mode.
    RootMotionMode getRootMotionMode() const;

    /// @brief Sets which joint is the root motion bone (default: 0).
    void setRootMotionBone(int jointIndex);

    /// @brief Gets the root motion delta position accumulated this frame.
    glm::vec3 getRootMotionDeltaPosition() const;

    /// @brief Gets the root motion delta rotation accumulated this frame.
    glm::quat getRootMotionDeltaRotation() const;

    // --- Output ---

    /// @brief Gets the final bone matrices for GPU upload.
    /// Each matrix is: jointGlobalTransform * inverseBindMatrix.
    const std::vector<glm::mat4>& getBoneMatrices() const;

    /// @brief Whether this animator has valid bone data to render.
    bool hasBones() const;

private:
    /// @brief Evaluates all channels of the current clip at the current time,
    /// then walks the joint hierarchy to compute global transforms and bone matrices.
    void computeBoneMatrices();

    /// @brief Advances a clip's time and samples all channels into the given pose buffers.
    void advanceAndSample(int clipIndex, float& time, float deltaTime,
                          std::vector<glm::vec3>& translations,
                          std::vector<glm::quat>& rotations,
                          std::vector<glm::vec3>& scales);

    /// @brief Extracts root motion delta and zeroes the root bone's horizontal motion.
    void extractRootMotion();

    /// @brief Allocates pose buffers and initializes from bind pose.
    void initializeBuffers();

    std::shared_ptr<Skeleton> m_skeleton;
    std::vector<std::shared_ptr<AnimationClip>> m_clips;
    int m_activeClipIndex = -1;

    float m_currentTime = 0.0f;
    float m_speed = 1.0f;
    bool m_looping = true;
    bool m_paused = false;
    bool m_playing = false;

    // Per-joint local transforms (written by sampler each frame)
    std::vector<glm::vec3> m_localTranslations;
    std::vector<glm::quat> m_localRotations;
    std::vector<glm::vec3> m_localScales;

    // Intermediate: global transform per joint (before inverse bind)
    std::vector<glm::mat4> m_globalTransforms;

    // Final output: jointGlobalTransform * inverseBindMatrix
    std::vector<glm::mat4> m_boneMatrices;

    // --- Crossfade state ---
    bool m_crossfading = false;
    float m_crossfadeTime = 0.0f;
    float m_crossfadeDuration = 0.0f;
    int m_sourceClipIndex = -1;         ///< -1 = frozen pose (no clip to advance)
    float m_sourceTime = 0.0f;
    std::vector<glm::vec3> m_sourceTranslations;
    std::vector<glm::quat> m_sourceRotations;
    std::vector<glm::vec3> m_sourceScales;

    // --- Root motion state ---
    RootMotionMode m_rootMotionMode = RootMotionMode::IGNORE;
    int m_rootMotionBone = 0;
    glm::vec3 m_rootMotionDeltaPos = glm::vec3(0.0f);
    glm::quat m_rootMotionDeltaRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 m_prevRootPos = glm::vec3(0.0f);
    glm::quat m_prevRootRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    bool m_rootMotionInitialized = false;
};

} // namespace Vestige
