// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file scene_manager.h
/// @brief Manages loading, switching, and updating scenes.
#pragma once

#include "scene/scene.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace Vestige
{

/// @brief Manages multiple scenes and the currently active scene.
class SceneManager
{
public:
    SceneManager();
    ~SceneManager();

    /// @brief Creates a new empty scene.
    /// @param name Unique name for the scene.
    /// @return Pointer to the new scene.
    Scene* createScene(const std::string& name);

    /// @brief Sets the active scene by name.
    /// @param name The scene name.
    /// @return True if the scene was found and set.
    bool setActiveScene(const std::string& name);

    /// @brief Gets the currently active scene.
    /// @return Pointer to the active scene, or nullptr.
    Scene* getActiveScene();

    /// @brief Updates the active scene.
    /// @param deltaTime Time elapsed since last frame.
    void update(float deltaTime);

    /// @brief Removes a scene by name.
    void removeScene(const std::string& name);

    /// @brief Gets the number of loaded scenes.
    size_t getSceneCount() const;

private:
    std::unordered_map<std::string, std::unique_ptr<Scene>> m_scenes;
    Scene* m_activeScene;
};

} // namespace Vestige
