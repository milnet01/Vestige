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
#include "renderer/particle_renderer.h"
#include "renderer/water_renderer.h"
#include "renderer/water_fbo.h"
#include "renderer/foliage_renderer.h"
#include "renderer/tree_renderer.h"
#include "renderer/terrain_renderer.h"
#include "environment/environment_forces.h"
#include "environment/foliage_manager.h"
#include "environment/terrain.h"
#include "profiler/performance_profiler.h"
#include "physics/physics_world.h"
#include "physics/physics_debug.h"
#include "physics/physics_character_controller.h"
#include "testing/visual_test_runner.h"

#include <memory>

namespace Vestige
{

/// @brief Configuration for the engine.
struct EngineConfig
{
    WindowConfig window;
    std::string assetPath = "assets";
    bool visualTestMode = false;  ///< Run automated visual test and exit
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
    ParticleRenderer m_particleRenderer;
    WaterRenderer m_waterRenderer;
    WaterFbo m_waterFbo;
    FoliageRenderer m_foliageRenderer;
    TreeRenderer m_treeRenderer;
    FoliageManager m_foliageManager;
    EnvironmentForces m_environmentForces;
    Terrain m_terrain;
    bool m_terrainEnabled = true;  ///< Set false for indoor scenes that don't need terrain
    TerrainRenderer m_terrainRenderer;
    PerformanceProfiler m_profiler;
    PhysicsWorld m_physicsWorld;
    PhysicsDebugDraw m_physicsDebugDraw;
    PhysicsCharacterController m_physicsCharController;
    bool m_usePhysicsController = false;  ///< Toggle between AABB and physics controller (P key)
    VisualTestRunner m_visualTestRunner;
    bool m_visualTestMode = false;

    bool m_isRunning;
    bool m_isCursorCaptured;

    // Reusable per-frame data (avoids heap allocation every frame)
    SceneRenderData m_renderData;
    std::vector<AABB> m_colliders;
};

} // namespace Vestige
