// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file scene_manager.cpp
/// @brief SceneManager implementation.
#include "scene/scene_manager.h"
#include "core/event_bus.h"
#include "core/logger.h"
#include "core/system_events.h"

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
        if (m_eventBus)
        {
            ptr->attachEventBus(m_eventBus);
            m_eventBus->publish(SceneLoadedEvent(ptr));
        }
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

    Scene* next = it->second.get();
    if (next == m_activeScene)
    {
        return true;
    }

    if (m_eventBus && m_activeScene)
    {
        m_eventBus->publish(SceneUnloadedEvent(m_activeScene));
        m_activeScene->attachEventBus(nullptr);
    }
    m_activeScene = next;
    Logger::info("Active scene switched to: " + name);
    if (m_eventBus)
    {
        m_activeScene->attachEventBus(m_eventBus);
        m_eventBus->publish(SceneLoadedEvent(m_activeScene));
    }
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
            if (m_eventBus)
            {
                m_eventBus->publish(SceneUnloadedEvent(m_activeScene));
            }
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

void SceneManager::attachEventBus(EventBus& bus)
{
    m_eventBus = &bus;
    if (m_activeScene)
    {
        m_activeScene->attachEventBus(m_eventBus);
        m_eventBus->publish(SceneLoadedEvent(m_activeScene));
    }
}

} // namespace Vestige
