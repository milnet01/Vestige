// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file model_viewer_panel.h
/// @brief Model viewer panel — 3D model preview with orbit camera, animation playback,
///        skeleton visualization, and drag-to-place.
#pragma once

#include "renderer/framebuffer.h"
#include "renderer/shader.h"
#include "renderer/texture.h"
#include "resource/model.h"
#include "animation/skeleton_animator.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <memory>
#include <string>

namespace Vestige
{

class ResourceManager;
class Scene;

/// @brief Panel for previewing 3D models with orbiting camera, PBR rendering,
///        animation playback, skeleton visualization, and drag-to-place support.
class ModelViewerPanel
{
public:
    /// @brief Initializes shaders and FBO for model rendering.
    /// @param assetPath Base path for shader loading.
    /// @return True if initialization succeeded.
    bool initialize(const std::string& assetPath);

    /// @brief Draws the model viewer panel.
    /// @param scene Scene for drag-to-place instantiation (may be nullptr).
    /// @param resources ResourceManager for model loading (may be nullptr).
    /// @param deltaTime Time since last frame (for animation).
    void draw(Scene* scene, ResourceManager* resources, float deltaTime);

    /// @brief Opens a model for viewing.
    /// @param model Shared pointer to the loaded model.
    /// @param path File path of the model (for drag-drop payload).
    void openModel(std::shared_ptr<Model> model, const std::string& path);

    /// @brief Releases GPU resources.
    void cleanup();

    bool isOpen() const { return m_open; }
    void setOpen(bool open) { m_open = open; }

    // --- Accessors for testing ---

    float getOrbitYaw() const { return m_yaw; }
    float getOrbitPitch() const { return m_pitch; }
    float getOrbitDistance() const { return m_distance; }
    float getPlaybackSpeed() const { return m_playbackSpeed; }
    void setPlaybackSpeed(float speed);

    const std::string& getModelPath() const { return m_modelPath; }

private:
    void drawViewport();
    void drawModelInfo();
    void drawAnimationControls(float deltaTime);
    void renderPreview(float deltaTime);
    void autoFitCamera();

    // Orbit camera math
    glm::vec3 computeCameraPosition() const;
    glm::mat4 computeViewMatrix() const;
    glm::mat4 computeProjectionMatrix(float aspectRatio) const;

    bool m_open = false;
    bool m_initialized = false;
    bool m_dirty = true;

    // Model being viewed
    std::shared_ptr<Model> m_model;
    std::string m_modelPath;

    // Orbit camera
    glm::vec3 m_focusPoint = glm::vec3(0.0f);
    float m_distance = 3.0f;
    float m_yaw = 30.0f;      ///< Degrees
    float m_pitch = 20.0f;    ///< Degrees
    float m_minDistance = 0.1f;
    float m_maxDistance = 100.0f;

    // Off-screen rendering
    std::unique_ptr<Framebuffer> m_fbo;
    Shader m_previewShader;
    std::shared_ptr<Texture> m_defaultTexture;
    int m_fboWidth = 512;
    int m_fboHeight = 512;

    // Animation
    std::unique_ptr<SkeletonAnimator> m_animator;
    int m_selectedClip = -1;
    float m_playbackSpeed = 1.0f;
    bool m_showSkeleton = false;
    bool m_showBounds = true;

    // Stats (cached on model open)
    int m_totalVertices = 0;
    int m_totalTriangles = 0;
    AABB m_modelBounds;

    std::string m_assetPath;
};

} // namespace Vestige
