// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file scene_manager.cpp
/// @brief SceneManager implementation.
#include "scene/scene_manager.h"
#include "core/logger.h"

namespace Vestige
{

SceneManager::SceneManager()
    : m_activeScene(nullptr)
{
}

SceneManager::~SceneManager() = default;

Scene* SceneManager::createScene(const std::string& name)
{
    if (m_scenes.find(name) != m_scenes.end())
    {
        Logger::warning("Scene already exists: " + name);
        return m_scenes[name].get();
    }

    auto scene = std::make_unique<Scene>(name);
    Scene* ptr = scene.get();
    m_scenes[name] = std::move(scene);

    Logger::info("Scene created: " + name);

    // If no active scene, set this one
    if (!m_activeScene)
    {
        m_activeScene = ptr;
        Logger::info("Active scene set to: " + name);
    }

    return ptr;
}

bool SceneManager::setActiveScene(const std::string& name)
{
    auto it = m_scenes.find(name);
    if (it == m_scenes.end())
    {
        Logger::error("Scene not found: " + name);
        return false;
    }

    m_activeScene = it->second.get();
    Logger::info("Active scene switched to: " + name);
    return true;
}

Scene* SceneManager::getActiveScene()
{
    return m_activeScene;
}

void SceneManager::update(float deltaTime)
{
    if (m_activeScene)
    {
        m_activeScene->update(deltaTime);
    }
}

void SceneManager::removeScene(const std::string& name)
{
    auto it = m_scenes.find(name);
    if (it != m_scenes.end())
    {
        if (m_activeScene == it->second.get())
        {
            m_activeScene = nullptr;
        }
        m_scenes.erase(it);
        Logger::info("Scene removed: " + name);
    }
}

size_t SceneManager::getSceneCount() const
{
    return m_scenes.size();
}

} // namespace Vestige
