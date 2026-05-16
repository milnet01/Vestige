// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_system_registry.cpp
/// @brief Unit tests for ISystem, SystemRegistry, and cross-system events.
/// @note These tests don't require a full engine setup -- they use mock systems
///       to verify the registry's lifecycle, dispatch, and auto-activation logic.

#include "core/i_system.h"
#include "core/system_registry.h"
#include "core/system_events.h"
#include "scene/component.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <string>
#include <vector>

using namespace Vestige;

// =============================================================================
// Mock system for testing
// =============================================================================

/// @brief A minimal system implementation for testing registry behavior.
class MockSystem : public ISystem
{
public:
    // Track call order across all MockSystem instances
    static std::vector<std::string> s_callLog;

    explicit MockSystem(const std::string& name, bool forceActive = false)
        : m_name(name), m_forceActive(forceActive)
    {
    }

    ~MockSystem() override
    {
        // AUDIT.md §H17 verification hook: lets tests confirm WHEN the
        // destructor runs relative to shutdown() / clear() calls. Critical
        // because the original bug was destructors running too late
        // (during ~Engine instead of ~Engine::shutdown()).
        s_callLog.push_back(m_name + "::destructor");
    }

    const std::string& getSystemName() const override { return m_name; }

    bool initialize(Engine& /*engine*/) override
    {
        s_callLog.push_back(m_name + "::initialize");
        m_initialized = true;
        return m_shouldInitSucceed;
    }

    void shutdown() override
    {
        s_callLog.push_back(m_name + "::shutdown");
        m_initialized = false;
    }

    void update(float deltaTime) override
    {
        s_callLog.push_back(m_name + "::update");
        m_lastDeltaTime = deltaTime;
        m_updateCount++;
    }

    void fixedUpdate(float fixedDt) override
    {
        s_callLog.push_back(m_name + "::fixedUpdate");
        m_lastFixedDt = fixedDt;
        m_fixedUpdateCount++;
    }

    void drawDebug() override
    {
        s_callLog.push_back(m_name + "::drawDebug");
        m_drawDebugCount++;
    }

    bool isForceActive() const override { return m_forceActive; }

    std::vector<uint32_t> getOwnedComponentTypes() const override
    {
        return m_ownedTypes;
    }

    UpdatePhase getUpdatePhase() const override { return m_phase; }

    // Test helpers
    void setShouldInitSucceed(bool succeed) { m_shouldInitSucceed = succeed; }
    void setOwnedComponentTypes(std::vector<uint32_t> types)
    {
        m_ownedTypes = std::move(types);
    }
    void setUpdatePhase(UpdatePhase phase) { m_phase = phase; }

    bool wasInitialized() const { return m_initialized; }
    int getUpdateCount() const { return m_updateCount; }
    int getFixedUpdateCount() const { return m_fixedUpdateCount; }
    int getDrawDebugCount() const { return m_drawDebugCount; }
    float getLastDeltaTime() const { return m_lastDeltaTime; }
    float getLastFixedDt() const { return m_lastFixedDt; }

private:
    std::string m_name;
    bool m_forceActive = false;
    bool m_shouldInitSucceed = true;
    bool m_initialized = false;
    int m_updateCount = 0;
    int m_fixedUpdateCount = 0;
    int m_drawDebugCount = 0;
    float m_lastDeltaTime = 0.0f;
    float m_lastFixedDt = 0.0f;
    std::vector<uint32_t> m_ownedTypes;
    UpdatePhase m_phase = UpdatePhase::Update;
};

std::vector<std::string> MockSystem::s_callLog;

/// @brief Test fixture that clears the call log before each test.
class SystemRegistryTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        MockSystem::s_callLog.clear();
    }

    SystemRegistry registry;
};

// =============================================================================
// Registration
// =============================================================================

TEST_F(SystemRegistryTest, RegisterSystemReturnsPointer)
{
    auto* sys = registry.registerSystem<MockSystem>("TestSystem");
    ASSERT_NE(sys, nullptr);
    EXPECT_EQ(sys->getSystemName(), "TestSystem");
}

TEST_F(SystemRegistryTest, RegisterMultipleSystems)
{
    registry.registerSystem<MockSystem>("A");
    registry.registerSystem<MockSystem>("B");
    registry.registerSystem<MockSystem>("C");
    EXPECT_EQ(registry.getSystemCount(), 3u);
}

TEST_F(SystemRegistryTest, GetSystemReturnsRegistered)
{
    auto* sys = registry.registerSystem<MockSystem>("TestSystem");
    auto* found = registry.getSystem<MockSystem>();
    EXPECT_EQ(found, sys);
}

TEST_F(SystemRegistryTest, GetSystemReturnsNullptrIfNotRegistered)
{
    // Register nothing
    auto* found = registry.getSystem<MockSystem>();
    EXPECT_EQ(found, nullptr);
}

TEST_F(SystemRegistryTest, EmptyRegistryHasZeroSystems)
{
    EXPECT_EQ(registry.getSystemCount(), 0u);
}

// =============================================================================
// Lifecycle -- initialize
// =============================================================================

// NOTE: initializeAll takes Engine& but our mock doesn't use it.
// We reinterpret_cast a dummy to avoid needing a real Engine.
// This is safe because MockSystem::initialize ignores the reference.
static Engine& dummyEngine()
{
    static char buf[1] = {};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<Engine&>(buf);
}

TEST_F(SystemRegistryTest, InitializeAllCallsInRegistrationOrder)
{
    registry.registerSystem<MockSystem>("A");
    registry.registerSystem<MockSystem>("B");
    registry.registerSystem<MockSystem>("C");

    EXPECT_TRUE(registry.initializeAll(dummyEngine()));

    ASSERT_EQ(MockSystem::s_callLog.size(), 3u);
    EXPECT_EQ(MockSystem::s_callLog[0], "A::initialize");
    EXPECT_EQ(MockSystem::s_callLog[1], "B::initialize");
    EXPECT_EQ(MockSystem::s_callLog[2], "C::initialize");
}

TEST_F(SystemRegistryTest, InitializeAllReturnsFalseOnFailure)
{
    auto* sys = registry.registerSystem<MockSystem>("A");
    sys->setShouldInitSucceed(false);

    EXPECT_FALSE(registry.initializeAll(dummyEngine()));
}

TEST_F(SystemRegistryTest, DoubleInitializeReturnsFalse)
{
    registry.registerSystem<MockSystem>("A");
    EXPECT_TRUE(registry.initializeAll(dummyEngine()));
    EXPECT_FALSE(registry.initializeAll(dummyEngine()));
}

// =============================================================================
// Phase 10.9 Slice 1 F10 — partial-init cleanup on failure
// =============================================================================
//
// Context: shipping initializeAll() returns false on the first initialize()
// failure but leaves every already-initialized system still holding its
// resources (GL handles, OpenAL sources, Jolt bodies, etc.). The registry
// destructor does NOT call shutdown() — shutdownAll() is the only call site
// that does, and it early-returns on `!m_initialized`, which stays false
// after a failed init. Result: every 0..N-1 system leaks its engine-owned
// resources until process exit.
//
// F10 contract: on failure of system N, shutdown the successfully-initialized
// 0..N-1 prefix in reverse (mirroring shutdownAll's order) and mark each
// inactive, BEFORE returning false. System N itself gets no shutdown() —
// its initialize() returned false, meaning resources were not acquired.

TEST_F(SystemRegistryTest, InitializeAllShutsDownPrefixInReverseOnFailure_F10)
{
    registry.registerSystem<MockSystem>("A");
    auto* b = registry.registerSystem<MockSystem>("B");
    registry.registerSystem<MockSystem>("C");
    b->setShouldInitSucceed(false);

    EXPECT_FALSE(registry.initializeAll(dummyEngine()));

    // A initialized successfully. B's initialize() ran and returned false.
    // C should never have been touched (we aborted). A must get shutdown()
    // to release any resources it acquired. No other shutdowns fire.
    ASSERT_EQ(MockSystem::s_callLog.size(), 3u);
    EXPECT_EQ(MockSystem::s_callLog[0], "A::initialize");
    EXPECT_EQ(MockSystem::s_callLog[1], "B::initialize");
    EXPECT_EQ(MockSystem::s_callLog[2], "A::shutdown");
}

TEST_F(SystemRegistryTest, InitializeAllShutsDownEveryPrecedingOnFailure_F10)
{
    // Three succeed, fourth fails — all three must shutdown in reverse.
    registry.registerSystem<MockSystem>("A");
    registry.registerSystem<MockSystem>("B");
    registry.registerSystem<MockSystem>("C");
    auto* d = registry.registerSystem<MockSystem>("D");
    registry.registerSystem<MockSystem>("E");
    d->setShouldInitSucceed(false);

    EXPECT_FALSE(registry.initializeAll(dummyEngine()));

    // Expected: A::init, B::init, C::init, D::init (fail),
    //           C::shutdown, B::shutdown, A::shutdown. E untouched.
    ASSERT_EQ(MockSystem::s_callLog.size(), 7u);
    EXPECT_EQ(MockSystem::s_callLog[0], "A::initialize");
    EXPECT_EQ(MockSystem::s_callLog[1], "B::initialize");
    EXPECT_EQ(MockSystem::s_callLog[2], "C::initialize");
    EXPECT_EQ(MockSystem::s_callLog[3], "D::initialize");
    EXPECT_EQ(MockSystem::s_callLog[4], "C::shutdown");
    EXPECT_EQ(MockSystem::s_callLog[5], "B::shutdown");
    EXPECT_EQ(MockSystem::s_callLog[6], "A::shutdown");
}

TEST_F(SystemRegistryTest, InitializeAllFailureOnFirstSystemShutsDownNothing_F10)
{
    // Edge case: the very first system fails. There is no prefix to clean up.
    auto* a = registry.registerSystem<MockSystem>("A");
    registry.registerSystem<MockSystem>("B");
    a->setShouldInitSucceed(false);

    EXPECT_FALSE(registry.initializeAll(dummyEngine()));

    // Only A's initialize() appears. No shutdown — nothing was initialized.
    ASSERT_EQ(MockSystem::s_callLog.size(), 1u);
    EXPECT_EQ(MockSystem::s_callLog[0], "A::initialize");
}

TEST_F(SystemRegistryTest, InitializeAllFailureDeactivatesPrefix_F10)
{
    // After cleanup, the previously-initialized systems must not be
    // reported as active — subsequent updateAll() calls would otherwise
    // tick subsystems whose resources have been released.
    auto* a = registry.registerSystem<MockSystem>("A");
    auto* b = registry.registerSystem<MockSystem>("B");
    a->setActive(true);
    b->setShouldInitSucceed(false);

    EXPECT_FALSE(registry.initializeAll(dummyEngine()));

    EXPECT_FALSE(a->isActive());
    EXPECT_FALSE(b->isActive());
}

TEST_F(SystemRegistryTest, InitializeAllFailureLeavesRegistryReInitable_F10)
{
    // After cleanup, m_initialized must be false so a subsequent successful
    // init path works. clear() must also still tear the registry down
    // without double-destructing.
    registry.registerSystem<MockSystem>("A");
    auto* b = registry.registerSystem<MockSystem>("B");
    b->setShouldInitSucceed(false);

    EXPECT_FALSE(registry.initializeAll(dummyEngine()));
    MockSystem::s_callLog.clear();

    // shutdownAll() after a failed init is a no-op (nothing still "initialized").
    registry.shutdownAll();
    EXPECT_TRUE(MockSystem::s_callLog.empty());

    // clear() still runs destructors exactly once.
    registry.clear();
    ASSERT_EQ(MockSystem::s_callLog.size(), 2u);
    EXPECT_EQ(MockSystem::s_callLog[0], "B::destructor");
    EXPECT_EQ(MockSystem::s_callLog[1], "A::destructor");
}

// =============================================================================
// Lifecycle -- shutdown
// =============================================================================

TEST_F(SystemRegistryTest, ShutdownAllCallsInReverseOrder)
{
    registry.registerSystem<MockSystem>("A");
    registry.registerSystem<MockSystem>("B");
    registry.registerSystem<MockSystem>("C");
    registry.initializeAll(dummyEngine());

    MockSystem::s_callLog.clear();
    registry.shutdownAll();

    ASSERT_EQ(MockSystem::s_callLog.size(), 3u);
    EXPECT_EQ(MockSystem::s_callLog[0], "C::shutdown");
    EXPECT_EQ(MockSystem::s_callLog[1], "B::shutdown");
    EXPECT_EQ(MockSystem::s_callLog[2], "A::shutdown");
}

TEST_F(SystemRegistryTest, ShutdownWithoutInitDoesNotCrash)
{
    registry.registerSystem<MockSystem>("A");
    EXPECT_NO_THROW(registry.shutdownAll());
}

// =============================================================================
// AUDIT.md §H17 — clear() destroys systems while engine is alive
// =============================================================================
//
// The bug clear() fixes: shutdownAll() invokes shutdown() but leaves the
// systems in the unique_ptr vector. Their destructors only run when the
// registry itself is destroyed — which, in Engine::shutdown(), happens
// during ~Engine after Renderer/Window/GL context are already gone.
//
// These tests pin the contract that destructors run synchronously inside
// clear(), in reverse registration order, and that clear() is idempotent.

TEST_F(SystemRegistryTest, ClearDestroysSystemsInReverseOrder)
{
    registry.registerSystem<MockSystem>("A");
    registry.registerSystem<MockSystem>("B");
    registry.registerSystem<MockSystem>("C");

    MockSystem::s_callLog.clear();
    registry.clear();

    ASSERT_EQ(MockSystem::s_callLog.size(), 3u);
    EXPECT_EQ(MockSystem::s_callLog[0], "C::destructor");
    EXPECT_EQ(MockSystem::s_callLog[1], "B::destructor");
    EXPECT_EQ(MockSystem::s_callLog[2], "A::destructor");
}

TEST_F(SystemRegistryTest, ClearEmptiesRegistry)
{
    registry.registerSystem<MockSystem>("A");
    registry.registerSystem<MockSystem>("B");
    EXPECT_EQ(registry.getSystemCount(), 2u);

    registry.clear();

    EXPECT_EQ(registry.getSystemCount(), 0u);
    EXPECT_EQ(registry.getSystem<MockSystem>(), nullptr);
}

TEST_F(SystemRegistryTest, ClearOnEmptyRegistryIsSafe)
{
    EXPECT_NO_THROW(registry.clear());
    EXPECT_EQ(registry.getSystemCount(), 0u);
}

TEST_F(SystemRegistryTest, ClearIsIdempotent)
{
    registry.registerSystem<MockSystem>("A");
    registry.clear();
    EXPECT_NO_THROW(registry.clear());
    EXPECT_EQ(registry.getSystemCount(), 0u);
}

TEST_F(SystemRegistryTest, ShutdownAllThenClearRunsBothInOrder)
{
    // Models the Engine::shutdown() call sequence: shutdown() first to give
    // each system a chance to release engine-owned resources, THEN clear()
    // to actually destroy them while shared infrastructure is alive.
    registry.registerSystem<MockSystem>("A");
    registry.registerSystem<MockSystem>("B");
    registry.initializeAll(dummyEngine());

    MockSystem::s_callLog.clear();
    registry.shutdownAll();
    registry.clear();

    ASSERT_EQ(MockSystem::s_callLog.size(), 4u);
    EXPECT_EQ(MockSystem::s_callLog[0], "B::shutdown");
    EXPECT_EQ(MockSystem::s_callLog[1], "A::shutdown");
    EXPECT_EQ(MockSystem::s_callLog[2], "B::destructor");
    EXPECT_EQ(MockSystem::s_callLog[3], "A::destructor");
}

TEST_F(SystemRegistryTest, ClearWithoutShutdownStillDestroys)
{
    // Defensive: clear() should work even if shutdownAll() wasn't called.
    // (Tests/embedders may legitimately register-and-drop without an init.)
    registry.registerSystem<MockSystem>("A");
    MockSystem::s_callLog.clear();

    registry.clear();

    ASSERT_EQ(MockSystem::s_callLog.size(), 1u);
    EXPECT_EQ(MockSystem::s_callLog[0], "A::destructor");
}

// =============================================================================
// Update dispatch
// =============================================================================

TEST_F(SystemRegistryTest, UpdateAllSkipsInactiveSystems)
{
    auto* a = registry.registerSystem<MockSystem>("A");
    auto* b = registry.registerSystem<MockSystem>("B");
    a->setActive(true);
    b->setActive(false);

    registry.updateAll(0.016f);

    EXPECT_EQ(a->getUpdateCount(), 1);
    EXPECT_EQ(b->getUpdateCount(), 0);
}

TEST_F(SystemRegistryTest, UpdateAllPassesDeltaTime)
{
    auto* sys = registry.registerSystem<MockSystem>("A");
    sys->setActive(true);

    registry.updateAll(0.033f);

    EXPECT_FLOAT_EQ(sys->getLastDeltaTime(), 0.033f);
}

TEST_F(SystemRegistryTest, FixedUpdateAllDispatchesToActiveSystems)
{
    auto* a = registry.registerSystem<MockSystem>("A");
    auto* b = registry.registerSystem<MockSystem>("B");
    a->setActive(true);
    b->setActive(false);

    registry.fixedUpdateAll(1.0f / 60.0f);

    EXPECT_EQ(a->getFixedUpdateCount(), 1);
    EXPECT_EQ(b->getFixedUpdateCount(), 0);
    EXPECT_FLOAT_EQ(a->getLastFixedDt(), 1.0f / 60.0f);
}

TEST_F(SystemRegistryTest, DrawDebugAllDispatchesToActiveSystems)
{
    auto* a = registry.registerSystem<MockSystem>("A");
    auto* b = registry.registerSystem<MockSystem>("B");
    a->setActive(true);
    b->setActive(false);

    registry.drawDebugAll();

    EXPECT_EQ(a->getDrawDebugCount(), 1);
    EXPECT_EQ(b->getDrawDebugCount(), 0);
}

TEST_F(SystemRegistryTest, UpdateAllMeasuresTime)
{
    auto* sys = registry.registerSystem<MockSystem>("A");
    sys->setActive(true);

    registry.updateAll(0.016f);

    // Time should be non-negative (could be 0 for fast mock)
    EXPECT_GE(sys->getLastUpdateTimeMs(), 0.0f);
}

// =============================================================================
// Performance budget
// =============================================================================

TEST_F(SystemRegistryTest, DefaultBudgetIsZero)
{
    MockSystem sys("Test");
    EXPECT_FLOAT_EQ(sys.getFrameBudgetMs(), 0.0f);
    EXPECT_FALSE(sys.isOverBudget());
}

TEST_F(SystemRegistryTest, SetFrameBudget)
{
    MockSystem sys("Test");
    sys.setFrameBudgetMs(2.0f);
    EXPECT_FLOAT_EQ(sys.getFrameBudgetMs(), 2.0f);
}

TEST_F(SystemRegistryTest, IsOverBudgetChecksCorrectly)
{
    MockSystem sys("Test");
    sys.setFrameBudgetMs(1.0f);

    // Simulate time measurement (normally set by registry)
    // Since m_lastUpdateTimeMs is private and set by registry via friend,
    // we test through the registry path
    auto* registered = registry.registerSystem<MockSystem>("BudgetTest");
    registered->setActive(true);
    registered->setFrameBudgetMs(1.0f);

    // updateAll measures real time; MockSystem::update is near-zero
    registry.updateAll(0.016f);

    // Should not be over budget for a trivial no-op update
    EXPECT_FALSE(registered->isOverBudget());
}

// =============================================================================
// Activation state
// =============================================================================

TEST_F(SystemRegistryTest, SystemStartsInactive)
{
    MockSystem sys("Test");
    EXPECT_FALSE(sys.isActive());
}

TEST_F(SystemRegistryTest, SetActiveChangesState)
{
    MockSystem sys("Test");
    sys.setActive(true);
    EXPECT_TRUE(sys.isActive());
    sys.setActive(false);
    EXPECT_FALSE(sys.isActive());
}

TEST_F(SystemRegistryTest, ForceActiveReturnsFalseByDefault)
{
    MockSystem sys("Test");
    EXPECT_FALSE(sys.isForceActive());
}

TEST_F(SystemRegistryTest, ForceActiveOverride)
{
    MockSystem sys("Test", true);
    EXPECT_TRUE(sys.isForceActive());
}

// =============================================================================
// Metrics
// =============================================================================

TEST_F(SystemRegistryTest, GetSystemMetricsReturnsAllSystems)
{
    auto* a = registry.registerSystem<MockSystem>("Alpha");
    auto* b = registry.registerSystem<MockSystem>("Beta");
    a->setActive(true);
    b->setActive(false);
    a->setFrameBudgetMs(5.0f);

    auto metrics = registry.getSystemMetrics();
    ASSERT_EQ(metrics.size(), 2u);

    EXPECT_EQ(metrics[0].name, "Alpha");
    EXPECT_TRUE(metrics[0].active);
    EXPECT_FLOAT_EQ(metrics[0].budgetMs, 5.0f);

    EXPECT_EQ(metrics[1].name, "Beta");
    EXPECT_FALSE(metrics[1].active);
}

TEST_F(SystemRegistryTest, TotalUpdateTimeForEmptyRegistry)
{
    EXPECT_FLOAT_EQ(registry.getTotalUpdateTimeMs(), 0.0f);
}

// =============================================================================
// Component ownership and auto-activation
// =============================================================================

// Dummy components for type ID testing
class TestComponentA : public Component {};
class TestComponentB : public Component {};
class TestComponentC : public Component {};

TEST_F(SystemRegistryTest, GetOwnedComponentTypesEmptyByDefault)
{
    MockSystem sys("Test");
    EXPECT_TRUE(sys.getOwnedComponentTypes().empty());
}

TEST_F(SystemRegistryTest, SetOwnedComponentTypes)
{
    MockSystem sys("Test");
    sys.setOwnedComponentTypes({1, 2, 3});
    auto types = sys.getOwnedComponentTypes();
    ASSERT_EQ(types.size(), 3u);
    EXPECT_EQ(types[0], 1u);
    EXPECT_EQ(types[1], 2u);
    EXPECT_EQ(types[2], 3u);
}

// =============================================================================
// Cross-system events
// =============================================================================

TEST(SystemEventsTest, SceneLoadedEventStoresScene)
{
    SceneLoadedEvent event(nullptr);
    EXPECT_EQ(event.scene, nullptr);
}

TEST(SystemEventsTest, SceneUnloadedEventStoresScene)
{
    SceneUnloadedEvent event(nullptr);
    EXPECT_EQ(event.scene, nullptr);
}

TEST(SystemEventsTest, WeatherChangedEventStoresValues)
{
    WeatherChangedEvent event(25.0f, 0.8f, 0.5f, 10.0f);
    EXPECT_FLOAT_EQ(event.temperature, 25.0f);
    EXPECT_FLOAT_EQ(event.humidity, 0.8f);
    EXPECT_FLOAT_EQ(event.precipitation, 0.5f);
    EXPECT_FLOAT_EQ(event.windStrength, 10.0f);
}

TEST(SystemEventsTest, EntityDestroyedEventStoresId)
{
    EntityDestroyedEvent event(42);
    EXPECT_EQ(event.entityId, 42u);
}

TEST(SystemEventsTest, TerrainModifiedEventStoresBounds)
{
    TerrainModifiedEvent event(1.0f, 2.0f, 3.0f, 4.0f);
    EXPECT_FLOAT_EQ(event.minX, 1.0f);
    EXPECT_FLOAT_EQ(event.minZ, 2.0f);
    EXPECT_FLOAT_EQ(event.maxX, 3.0f);
    EXPECT_FLOAT_EQ(event.maxZ, 4.0f);
}

// Slice 18 Ts3: dropped `EventBusPublishAndSubscribe` and
// `WeatherEventThroughBus` — generic publish/subscribe is canonical in
// `test_event_bus.cpp` (SubscribeAndPublish, MultipleSubscribers,
// etc.). The event-struct field round-trips are pinned earlier in
// this file (`SystemEventsTest.*Event*` fields); only the
// publish/subscribe wiring through `bus.subscribe<T>` was redundant.

// =============================================================================
// Edge cases
// =============================================================================

TEST_F(SystemRegistryTest, UpdateAllOnEmptyRegistryDoesNotCrash)
{
    EXPECT_NO_THROW(registry.updateAll(0.016f));
}

TEST_F(SystemRegistryTest, FixedUpdateAllOnEmptyRegistryDoesNotCrash)
{
    EXPECT_NO_THROW(registry.fixedUpdateAll(1.0f / 60.0f));
}

TEST_F(SystemRegistryTest, ShutdownAllOnEmptyRegistryDoesNotCrash)
{
    EXPECT_NO_THROW(registry.shutdownAll());
}

TEST_F(SystemRegistryTest, DrawDebugAllOnEmptyRegistryDoesNotCrash)
{
    EXPECT_NO_THROW(registry.drawDebugAll());
}

TEST_F(SystemRegistryTest, MetricsOnEmptyRegistry)
{
    auto metrics = registry.getSystemMetrics();
    EXPECT_TRUE(metrics.empty());
}

// =============================================================================
// Phase 10.9 Slice 11 Sy1 — UpdatePhase ordering
// =============================================================================
//
// Pin contract: SystemRegistry::sortByUpdatePhase() reorders m_systems so
// PreUpdate < Update < PostCamera < PostPhysics < Render, and the sort is
// stable (within-phase order is preserved as registration order).
//
// Direct verification uses getSystemsForTest() to read the live order
// without going through initializeAll() — that lets the assertions compare
// pointer identity, which is more precise than re-driving update() and
// inspecting MockSystem::s_callLog.

namespace
{

/// @brief Returns the ordered names of the registry's current m_systems
///        layout. Helper for the Sy1 phase-ordering tests.
std::vector<std::string> registryNamesInOrder(const SystemRegistry& reg)
{
    std::vector<std::string> names;
    for (const auto& sys : reg.getSystemsForTest())
    {
        names.push_back(sys->getSystemName());
    }
    return names;
}

}  // namespace

TEST_F(SystemRegistryTest, GetUpdatePhaseDefaultsToUpdate_Sy1)
{
    MockSystem* sys = registry.registerSystem<MockSystem>("Default");
    EXPECT_EQ(sys->getUpdatePhase(), UpdatePhase::Update);
}

TEST_F(SystemRegistryTest, SortByUpdatePhaseOrdersAcrossSlots_Sy1)
{
    // Register out-of-phase-order on purpose: Render, PreUpdate, PostCamera,
    // Update, PostPhysics. Sort should reorder them PreUpdate → Update →
    // PostCamera → PostPhysics → Render.
    auto* render     = registry.registerSystem<MockSystem>("Render");
    render->setUpdatePhase(UpdatePhase::Render);
    auto* preUpdate  = registry.registerSystem<MockSystem>("Pre");
    preUpdate->setUpdatePhase(UpdatePhase::PreUpdate);
    auto* postCamera = registry.registerSystem<MockSystem>("PostCam");
    postCamera->setUpdatePhase(UpdatePhase::PostCamera);
    auto* update     = registry.registerSystem<MockSystem>("Upd");
    update->setUpdatePhase(UpdatePhase::Update);
    auto* postPhys   = registry.registerSystem<MockSystem>("PostPhys");
    postPhys->setUpdatePhase(UpdatePhase::PostPhysics);

    registry.sortByUpdatePhase();

    EXPECT_EQ(registryNamesInOrder(registry),
        (std::vector<std::string>{"Pre", "Upd", "PostCam", "PostPhys", "Render"}));
}

TEST_F(SystemRegistryTest, SortByUpdatePhasePreservesWithinPhaseOrder_Sy1)
{
    // Three default-phase systems registered in order. Stable sort must
    // leave them in that order; an unstable sort could swap A↔C.
    registry.registerSystem<MockSystem>("A");
    registry.registerSystem<MockSystem>("B");
    registry.registerSystem<MockSystem>("C");

    registry.sortByUpdatePhase();

    EXPECT_EQ(registryNamesInOrder(registry),
        (std::vector<std::string>{"A", "B", "C"}));
}

TEST_F(SystemRegistryTest, SortByUpdatePhaseStableAcrossInterleavedPhases_Sy1)
{
    // Two Update-phase + two PostCamera-phase, interleaved at registration:
    // U1, PC1, U2, PC2. Sort should produce U1, U2, PC1, PC2 (within-phase
    // registration order preserved).
    registry.registerSystem<MockSystem>("U1");
    auto* pc1 = registry.registerSystem<MockSystem>("PC1");
    pc1->setUpdatePhase(UpdatePhase::PostCamera);
    registry.registerSystem<MockSystem>("U2");
    auto* pc2 = registry.registerSystem<MockSystem>("PC2");
    pc2->setUpdatePhase(UpdatePhase::PostCamera);

    registry.sortByUpdatePhase();

    EXPECT_EQ(registryNamesInOrder(registry),
        (std::vector<std::string>{"U1", "U2", "PC1", "PC2"}));
}

TEST_F(SystemRegistryTest, SortByUpdatePhaseIsIdempotent_Sy1)
{
    auto* a = registry.registerSystem<MockSystem>("A");
    a->setUpdatePhase(UpdatePhase::PostCamera);
    registry.registerSystem<MockSystem>("B");

    registry.sortByUpdatePhase();
    auto firstPass = registryNamesInOrder(registry);
    registry.sortByUpdatePhase();
    auto secondPass = registryNamesInOrder(registry);

    EXPECT_EQ(firstPass, secondPass);
}

TEST_F(SystemRegistryTest, InitializeAllSortsBeforeInit_Sy1)
{
    // Register Render-phase first, Update-phase second. After initializeAll
    // the call log should show "Update::initialize" before "Render::initialize"
    // because the sort runs before the per-system init loop.
    auto* render = registry.registerSystem<MockSystem>("Render");
    render->setUpdatePhase(UpdatePhase::Render);
    registry.registerSystem<MockSystem>("Update");

    // Use the dummyEngine() helper at line 179 — same pattern every other
    // initialize test in this file uses. Avoids `*nullptr` UB; MockSystem
    // ignores the reference but UBSan / future optimizers may still trap.
    registry.initializeAll(dummyEngine());

    auto& log = MockSystem::s_callLog;
    auto updateInit = std::find(log.begin(), log.end(),
                                std::string("Update::initialize"));
    auto renderInit = std::find(log.begin(), log.end(),
                                std::string("Render::initialize"));
    ASSERT_NE(updateInit, log.end());
    ASSERT_NE(renderInit, log.end());
    EXPECT_LT(updateInit, renderInit);
}

TEST_F(SystemRegistryTest, UpdateAllRunsInPhaseOrder_Sy1)
{
    // Register out-of-order; activate everything; updateAll should call
    // them PreUpdate → Update → PostCamera → Render.
    auto* render = registry.registerSystem<MockSystem>("R");
    render->setUpdatePhase(UpdatePhase::Render);
    render->setActive(true);
    auto* upd = registry.registerSystem<MockSystem>("U");
    upd->setActive(true);  // default phase = Update
    auto* pre = registry.registerSystem<MockSystem>("Pre");
    pre->setUpdatePhase(UpdatePhase::PreUpdate);
    pre->setActive(true);
    auto* pc = registry.registerSystem<MockSystem>("PC");
    pc->setUpdatePhase(UpdatePhase::PostCamera);
    pc->setActive(true);

    registry.sortByUpdatePhase();
    registry.updateAll(0.016f);

    auto& log = MockSystem::s_callLog;
    auto preIt    = std::find(log.begin(), log.end(), std::string("Pre::update"));
    auto updIt    = std::find(log.begin(), log.end(), std::string("U::update"));
    auto pcIt     = std::find(log.begin(), log.end(), std::string("PC::update"));
    auto renderIt = std::find(log.begin(), log.end(), std::string("R::update"));
    ASSERT_NE(preIt, log.end());
    ASSERT_NE(updIt, log.end());
    ASSERT_NE(pcIt, log.end());
    ASSERT_NE(renderIt, log.end());
    EXPECT_LT(preIt, updIt);
    EXPECT_LT(updIt, pcIt);
    EXPECT_LT(pcIt, renderIt);
}
