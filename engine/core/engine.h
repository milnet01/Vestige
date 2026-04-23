// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file engine.h
/// @brief Central engine class that owns and orchestrates all subsystems.
#pragma once

#include "accessibility/photosensitive_safety.h"
#include "audio/audio_mixer.h"
#include "core/event_bus.h"
#include "core/window.h"
#include "core/timer.h"
#include "core/input_manager.h"
#include "core/first_person_controller.h"
#include "core/settings.h"
#include "core/settings_apply.h"
#include "core/settings_editor.h"
#include "input/input_bindings.h"
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
#include "ui/caption_map.h"
#include "ui/subtitle.h"

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

    /// @brief Opt-in to the Tabernacle built-in demo (CLI: --biblical-demo).
    ///
    /// Default `false` — a fresh public clone opens `setupDemoScene()`
    /// which uses only the CC0 textures shipped in the public repo.
    /// Setting this to `true` calls `setupTabernacleScene()` instead,
    /// which references assets under `assets/textures/tabernacle/`. Those
    /// textures live in a separate private repo (commercial Steam release)
    /// and are not present in public clones — the flag exists for the
    /// maintainer's local development environment. Public users who enable
    /// it will see the tabernacle scene render with missing-texture
    /// fallbacks.
    bool biblicalDemo = false;
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

    /// @brief Persistent user settings loaded from
    ///        `ConfigPath::getConfigFile("settings.json")` during
    ///        `initialize()`. Saved atomically whenever the
    ///        first-run wizard closes with completion flipped
    ///        (slice 14.4) or on explicit user Apply (later slices).
    Settings m_settings;

    /// @brief Orchestrator behind the Settings UI panel (slice
    ///        13.5a). Owned here so its lifetime matches the engine;
    ///        the Editor receives a non-owning pointer for its panel.
    std::unique_ptr<SettingsEditor> m_settingsEditor;

    /// @brief Engine-owned stores (slice 13.5e) for subsystems that
    ///        didn't previously have a central source-of-truth for
    ///        Settings-driven values:
    ///
    /// - `m_audioMixer` holds authoritative bus gains. The editor's
    ///   `AudioPanel` previously owned the only instance; now the
    ///   engine owns one and the panel can be refitted to consult
    ///   this copy if desired. Downstream audio playback (AudioSource
    ///   gain resolution) still reads from per-source values — piping
    ///   the mixer into the OpenAL gain path is a follow-on audio
    ///   integration task (Phase 10.7).
    ///
    /// - `m_subtitleQueue` is the central caption queue. No consumer
    ///   ticks or renders it yet; the HUD / game-screen wiring that
    ///   calls `tick(dt)` and draws `activeSubtitles()` arrives with
    ///   the subtitle render pass (tracked as a Phase 11 game-screen
    ///   integration task). Settings routes `enabled` + `sizePreset`
    ///   to it so the store is authoritative once rendering lands.
    ///
    /// - `m_photosensitiveLimits` + `m_photosensitiveEnabled` are the
    ///   central caps store. Consumers (camera shake, flash overlay,
    ///   strobe emitters, bloom post) read from these via the
    ///   `clampFlashAlpha` / `clampShakeAmplitude` / `clampStrobeHz`
    ///   / `limitBloomIntensity` helpers at their own call sites.
    ///   Retrofitting each consumer to read from Engine is a Phase
    ///   10.7 item.
    AudioMixer              m_audioMixer;
    SubtitleQueue           m_subtitleQueue;
    CaptionMap              m_captionMap;
    PhotosensitiveLimits    m_photosensitiveLimits;
    bool                    m_photosensitiveEnabled = false;

    /// @brief Concrete apply-sink instances wrapping live engine
    ///        subsystems (slices 13.5d and 13.5e). Held here so their
    ///        lifetime matches the subsystems they forward to.
    ///        Construction order matters — each must be built after
    ///        its target subsystem is constructed and before the
    ///        SettingsEditor binds to them.
    ///
    /// Full coverage: display (Window), renderer accessibility
    /// (Renderer), UI accessibility (UISystem), audio bus gains
    /// (AudioMixer), subtitles (SubtitleQueue), HRTF (AudioEngine via
    /// AudioSystem), photosensitive caps (engine-owned store).
    std::unique_ptr<WindowDisplaySink>                   m_displaySink;
    std::unique_ptr<RendererAccessibilityApplySinkImpl>  m_rendererAccessSink;
    std::unique_ptr<UISystemAccessibilityApplySink>      m_uiAccessSink;
    std::unique_ptr<AudioMixerApplySink>                 m_audioSink;
    std::unique_ptr<SubtitleQueueApplySink>              m_subtitleSink;
    std::unique_ptr<AudioEngineHrtfApplySink>            m_hrtfSink;
    std::unique_ptr<PhotosensitiveStoreApplySink>        m_photosensitiveSink;

    /// @brief Engine-owned input action map. Game code pushes its
    ///        action definitions here before `initialize()` returns
    ///        so the Settings editor's Controls tab can present the
    ///        three-column rebind UI. Engine pre-registers a small
    ///        demo set of actions (wireframe / tonemap / screenshot
    ///        / fullscreen) that mirror the hardcoded F-key
    ///        shortcuts — useful for exercising the rebind UI
    ///        without a game project.
    InputActionMap m_inputActionMap;

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

    /// @brief Engine-owned audio bus-gain table (slice 13.5e).
    ///        Settings writes through `m_audioSink`; downstream audio
    ///        playback will consult this once the OpenAL gain path is
    ///        refitted (Phase 10.7 follow-on).
    AudioMixer&         getAudioMixer()        { return m_audioMixer; }
    const AudioMixer&   getAudioMixer() const  { return m_audioMixer; }

    /// @brief Engine-owned caption queue (slice 13.5e). Game code
    ///        enqueues captions; the not-yet-wired HUD render pass
    ///        will tick + draw. Settings controls `enabled` via the
    ///        subtitle sink and size preset via `setSizePreset`.
    SubtitleQueue&       getSubtitleQueue()       { return m_subtitleQueue; }
    const SubtitleQueue& getSubtitleQueue() const { return m_subtitleQueue; }

    /// @brief Engine-owned caption map (slice B3). Loaded from
    ///        `assets/captions.json` at engine init; missing file
    ///        means the project has no captions (empty map). Game
    ///        code and subsystems call `enqueueFor(clipPath, queue)`
    ///        to auto-fire a caption when a mapped clip plays.
    CaptionMap&       getCaptionMap()       { return m_captionMap; }
    const CaptionMap& getCaptionMap() const { return m_captionMap; }

    /// @brief Engine-owned photosensitive-safety state (slice 13.5e).
    ///        Consumers read via these accessors and pass to the
    ///        `clampFlashAlpha` / `clampShakeAmplitude` /
    ///        `clampStrobeHz` / `limitBloomIntensity` helpers.
    bool photosensitiveEnabled() const { return m_photosensitiveEnabled; }
    const PhotosensitiveLimits& photosensitiveLimits() const
    {
        return m_photosensitiveLimits;
    }
};

} // namespace Vestige
