// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file i_system.h
/// @brief Abstract base class for all domain systems in the engine.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Vestige
{

class Engine;
class Scene;
struct SceneRenderData;
class PerformanceProfiler;

/// @brief Coarse update-phase tag controlling per-frame dispatch order.
///
/// Phase 10.9 Slice 11 Sy1. SystemRegistry stable-sorts `m_systems` by this
/// tag at initialise time so callers (Engine::update + the per-frame
/// `updateAll()` it drives) see a deterministic phase ordering regardless of
/// registration order. Within the same phase, registration order is
/// preserved (the sort is stable), keeping legacy "register in update order"
/// semantics intact for the bulk of systems that don't override the default.
///
/// The integer values exist only to make the sort comparator trivial; do
/// not rely on the specific numbers — only their relative ordering.
///
/// Slot meanings:
///
/// - `PreUpdate`   — runs first. Use for transform-sync (e.g. pushing Jolt
///                   body world-transforms back into ECS `Transform`
///                   components) and any other "prime the frame" work the
///                   default-Update systems will read.
/// - `Update`      — default phase. Most domain systems sit here.
/// - `PostCamera`  — runs after the camera has been stepped. Use for
///                   systems that consume camera world transform / view
///                   matrix in their update — `AudioSystem` reads camera
///                   pos+forward+up to update the OpenAL listener (Slice
///                   11 W6 closes the dependency).
/// - `PostPhysics` — runs after physics-driven systems have written their
///                   post-step state. Reserved for Phase 11A consumers
///                   that need post-integration body data (replay record /
///                   playback, telemetry).
/// - `Render`      — runs last. Use for systems that prepare or submit
///                   render-time state (`UISystem`'s ImGui frame state).
enum class UpdatePhase : int
{
    PreUpdate    = -100,
    Update       =    0,
    PostCamera   =  100,
    PostPhysics  =  200,
    Render       =  300,
};

/// @brief Abstract base class for domain systems.
///
/// Each domain system (Terrain, Vegetation, Water, Cloth, etc.) inherits from
/// ISystem and is managed by SystemRegistry. Systems encapsulate all behavior
/// for their domain: simulation, rendering, physics, audio, and editor UI.
///
/// ## Pure virtuals (every system must implement):
/// - getSystemName() -- Human-readable name for logging/profiling
/// - initialize()    -- One-time setup (receives Engine reference)
/// - shutdown()      -- Cleanup
/// - update()        -- Per-frame variable-rate update
///
/// ## Opt-in virtual no-ops (override only if needed):
/// - fixedUpdate()       -- Fixed-timestep update (physics, simulation)
/// - submitRenderData()  -- Push render items before rendering
/// - onSceneLoad()       -- React to scene changes
/// - onSceneUnload()     -- Cleanup scene-specific state
/// - drawDebug()         -- Debug visualization
/// - reportMetrics()     -- Submit profiling data
class ISystem
{
public:
    virtual ~ISystem() = default;

    // -----------------------------------------------------------------------
    // Pure virtuals -- every system MUST implement these
    // -----------------------------------------------------------------------

    /// @brief Returns the human-readable name of this system.
    virtual const std::string& getSystemName() const = 0;

    /// @brief One-time initialization. Called by SystemRegistry during startup.
    /// @param engine Reference to the engine for accessing shared infrastructure.
    /// @return True if initialization succeeded.
    virtual bool initialize(Engine& engine) = 0;

    /// @brief Cleanup. Called by SystemRegistry during shutdown (reverse order).
    virtual void shutdown() = 0;

    /// @brief Per-frame variable-rate update.
    /// @param deltaTime Time elapsed since last frame in seconds.
    virtual void update(float deltaTime) = 0;

    /// @brief Returns the update-phase tag controlling dispatch ordering.
    ///
    /// SystemRegistry stable-sorts by this tag at `initializeAll()` time so
    /// per-frame `updateAll` / `submitRenderDataAll` / `drawDebugAll` walk
    /// the systems in phase order regardless of registration order. Default
    /// is `UpdatePhase::Update`. Override only when the system has a hard
    /// ordering dependency (e.g. AudioSystem reads camera state →
    /// `PostCamera`; UISystem prepares render-time ImGui state →
    /// `Render`). See `UpdatePhase` for slot meanings.
    virtual UpdatePhase getUpdatePhase() const { return UpdatePhase::Update; }

    // -----------------------------------------------------------------------
    // Opt-in virtual no-ops -- override only when needed
    // -----------------------------------------------------------------------

    /// @brief Fixed-timestep update for physics/simulation systems.
    /// @param fixedDeltaTime Fixed timestep in seconds (e.g. 1/60).
    virtual void fixedUpdate(float fixedDeltaTime) { (void)fixedDeltaTime; }

    /// @brief Push render items into SceneRenderData before the render phase.
    /// @param renderData The render data collection to populate.
    virtual void submitRenderData(SceneRenderData& renderData) { (void)renderData; }

    /// @brief Called when a scene finishes loading.
    /// @param scene The newly loaded scene.
    virtual void onSceneLoad(Scene& scene) { (void)scene; }

    /// @brief Called before a scene is unloaded.
    /// @param scene The scene about to be unloaded.
    virtual void onSceneUnload(Scene& scene) { (void)scene; }

    /// @brief Draw debug visualization (lines, shapes, overlays).
    virtual void drawDebug() {}

    /// @brief Submit per-system metrics to the profiler.
    /// @param profiler The performance profiler to report to.
    virtual void reportMetrics(PerformanceProfiler& profiler) { (void)profiler; }

    // -----------------------------------------------------------------------
    // Component ownership -- used by SystemRegistry for auto-activation
    // -----------------------------------------------------------------------

    /// @brief Returns the component type IDs owned by this system.
    /// Used by SystemRegistry to auto-activate systems based on scene contents.
    /// @return Vector of ComponentTypeId values. Empty means no auto-activation.
    virtual std::vector<uint32_t> getOwnedComponentTypes() const { return {}; }

    // -----------------------------------------------------------------------
    // Activation state
    // -----------------------------------------------------------------------

    /// @brief Whether this system is currently active (updated each frame).
    bool isActive() const { return m_isActive; }

    /// @brief Activate or deactivate this system.
    void setActive(bool active) { m_isActive = active; }

    /// @brief Whether this system should always be active regardless of scene contents.
    /// Override to return true for systems like Lighting and Atmosphere.
    virtual bool isForceActive() const { return false; }

    // -----------------------------------------------------------------------
    // Performance budget
    // -----------------------------------------------------------------------

    /// @brief Get the per-frame time budget for this system in milliseconds.
    /// @return Budget in ms. 0.0 means no budget (unlimited).
    float getFrameBudgetMs() const { return m_frameBudgetMs; }

    /// @brief Set the per-frame time budget for this system in milliseconds.
    /// @param budgetMs Budget in ms. 0.0 disables budget checking.
    void setFrameBudgetMs(float budgetMs) { m_frameBudgetMs = budgetMs; }

    /// @brief Get the actual time spent in the last update() call.
    /// @return Duration in milliseconds. Set by SystemRegistry after each update.
    float getLastUpdateTimeMs() const { return m_lastUpdateTimeMs; }

    /// @brief Check if the last update exceeded the frame budget.
    /// @return True if budget is set and last update exceeded it.
    bool isOverBudget() const
    {
        return m_frameBudgetMs > 0.0f && m_lastUpdateTimeMs > m_frameBudgetMs;
    }

private:
    friend class SystemRegistry;

    bool m_isActive = false;
    float m_frameBudgetMs = 0.0f;
    float m_lastUpdateTimeMs = 0.0f;
};

} // namespace Vestige
