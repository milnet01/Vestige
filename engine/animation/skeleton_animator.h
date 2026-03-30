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

/// @brief Component that drives skeletal animation playback on an entity.
/// Attach to the root entity of a skinned model. Each frame, update() advances
/// playback, samples keyframes, walks the joint hierarchy, and computes the
/// final bone matrices for GPU upload.
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

    /// @brief Plays a clip by name. Restarts from the beginning.
    void play(const std::string& clipName);

    /// @brief Plays a clip by index. Restarts from the beginning.
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
};

} // namespace Vestige
