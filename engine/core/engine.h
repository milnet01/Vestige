// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file engine.h
/// @brief Central engine class that owns and orchestrates all subsystems.
#pragma once

#include "core/event_bus.h"
#include "core/window.h"
#include "core/timer.h"
#include "core/input_manager.h"
#include "core/first_person_controller.h"
#include "renderer/renderer.h"
#include "renderer/camera.h"
#include "scene/scene_manager.h"
#include "resource/resource_manager.h"
#include "editor/editor.h"
#include "renderer/debug_draw.h"
#include "formula/quality_manager.h"
#include "profiler/performance_profiler.h"
#include "physics/physics_world.h"
#include "physics/physics_debug.h"
#include "testing/visual_test_runner.h"
#include "core/system_registry.h"

#include <memory>
#include <string>

namespace Vestige
{

// Forward declarations for domain system-owned types (accessed via cached pointers)
class EnvironmentForces;
class ParticleRenderer;
class WaterRenderer;
class WaterFbo;
class FoliageManager;
class FoliageRenderer;
class TreeRenderer;
class Terrain;
class TerrainRenderer;
class PhysicsCharacterController;
class UISystem;

/// @brief Configuration for the engine.
struct EngineConfig
{
    WindowConfig window;
    std::string assetPath = "assets";
    bool visualTestMode = false;  ///< Run automated visual test and exit

    /// @brief Diagnostic feature isolation (CLI: --isolate-feature=NAME).
    /// Disables one feature so visual-test runs can mechanically bisect
    /// regressions. Recognised names: "motion-overlay", "bloom", "ssao",
    /// "ibl". Empty string = no override.
    std::string isolateFeature;

    /// @brief Optional .scene file to load at startup (CLI: --scene PATH).
    /// When set, replaces the built-in demo scene. Resolved against CWD
    /// then `assetPath` + "scenes/". Empty string = use built-in demo.
    std::string startupScene;

    /// @brief If true, start directly in first-person PLAY mode with the
    /// editor UI hidden (CLI: --play). Default is EDIT mode with the
    /// editor visible.
    bool startInPlayMode = false;

    /// @brief Phase 10 slice 12.2 — enables the GameScreen state machine.
    ///
    /// When true, Engine opens `GameScreen::MainMenu` at cold-start (for
    /// headless game builds) and routes ESC through `UISystem::applyIntent`
    /// so buttons, pause, and settings operate on the pure state machine.
    /// Defaults to false so the editor / `--play` flow is unchanged; game
    /// projects opt in explicitly.
    bool enableGameScreens = false;
};

/// @brief The central engine — owns all subsystems and runs the main loop.
class Engine
{
public:
    Engine();
    ~Engine();

    // Non-copyable, non-movable
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    /// @brief Initializes all subsystems.
    /// @param config Engine configuration.
    /// @return True if initialization succeeded.
    bool initialize(const EngineConfig& config);

    /// @brief Runs the main engine loop until the window is closed.
    void run();

    /// @brief Shuts down all subsystems and releases resources.
    void shutdown();

private:
    void setupDemoScene();
    void setupTabernacleScene();
    void setupVisualTestViewpoints();
    void drawLightGizmos(Scene& scene, const Selection& selection,
                         bool showAll = false);
    void createPhysicsStaticBodies();

    EventBus m_eventBus;
    std::unique_ptr<Window> m_window;
    std::unique_ptr<Timer> m_timer;
    std::unique_ptr<InputManager> m_inputManager;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<Camera> m_camera;
    std::unique_ptr<FirstPersonController> m_controller;
    std::unique_ptr<SceneManager> m_sceneManager;
    std::unique_ptr<ResourceManager> m_resourceManager;
    std::unique_ptr<Editor> m_editor;
    DebugDraw m_debugDraw;
    FormulaQualityManager m_formulaQuality;
    PerformanceProfiler m_profiler;
    PhysicsWorld m_physicsWorld;
    PhysicsDebugDraw m_physicsDebugDraw;
    VisualTestRunner m_visualTestRunner;
    bool m_visualTestMode = false;
    std::string m_assetPath;

    // Cached pointers to domain system-owned subsystems (set during registration)
    EnvironmentForces* m_environmentForces = nullptr;
    ParticleRenderer* m_particleRenderer = nullptr;
    WaterRenderer* m_waterRenderer = nullptr;
    WaterFbo* m_waterFbo = nullptr;
    FoliageManager* m_foliageManager = nullptr;
    FoliageRenderer* m_foliageRenderer = nullptr;
    // Scratch vector for per-frame visible-foliage-chunk list — keeps its
    // capacity across frames so the hot path doesn't heap-alloc. (AUDIT H9.)
    std::vector<const class FoliageChunk*> m_scratchVisibleChunks;
    TreeRenderer* m_treeRenderer = nullptr;
    Terrain* m_terrain = nullptr;
    TerrainRenderer* m_terrainRenderer = nullptr;
    PhysicsCharacterController* m_physicsCharController = nullptr;
    bool m_usePhysicsController = false;  ///< Toggle between AABB and physics controller
    bool m_terrainEnabled = true;         ///< Set false for indoor scenes
    UISystem* m_uiSystem = nullptr;       ///< Cached pointer for render loop
    bool m_enableGameScreens = false;     ///< Slice 12.2 opt-in flag
    class SpriteSystem* m_spriteSystem = nullptr;  ///< Phase 9F 2D sprite pass
    class Physics2DSystem* m_physics2DSystem = nullptr;  ///< Phase 9F 2D physics

    SystemRegistry m_systemRegistry;

    bool m_isRunning;
    bool m_isCursorCaptured;

    // Reusable per-frame data (avoids heap allocation every frame)
    SceneRenderData m_renderData;
    std::vector<AABB> m_colliders;

public:
    /// @brief Access the system registry (for domain system registration/lookup).
    SystemRegistry& getSystemRegistry() { return m_systemRegistry; }
    const SystemRegistry& getSystemRegistry() const { return m_systemRegistry; }

    /// @brief Access the event bus (for cross-system communication).
    EventBus& getEventBus() { return m_eventBus; }

    /// @brief Access shared infrastructure for domain systems.
    const std::string& getAssetPath() const { return m_assetPath; }
    Window& getWindow() { return *m_window; }
    Camera& getCamera() { return *m_camera; }
    ResourceManager& getResourceManager() { return *m_resourceManager; }
    Renderer& getRenderer() { return *m_renderer; }
    SceneManager& getSceneManager() { return *m_sceneManager; }
    PhysicsWorld& getPhysicsWorld() { return m_physicsWorld; }
    PerformanceProfiler& getProfiler() { return m_profiler; }

    /// @brief Access domain system-owned subsystems (via cached pointers).
    EnvironmentForces& getEnvironmentForces() { return *m_environmentForces; }
    Terrain& getTerrain() { return *m_terrain; }
};

} // namespace Vestige
