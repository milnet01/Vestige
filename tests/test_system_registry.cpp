/// @file test_system_registry.cpp
/// @brief Unit tests for ISystem, SystemRegistry, and cross-system events.
/// @note These tests don't require a full engine setup -- they use mock systems
///       to verify the registry's lifecycle, dispatch, and auto-activation logic.

#include "core/i_system.h"
#include "core/system_registry.h"
#include "core/system_events.h"
#include "core/event_bus.h"
#include "scene/component.h"

#include <gtest/gtest.h>
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

    // Test helpers
    void setShouldInitSucceed(bool succeed) { m_shouldInitSucceed = succeed; }
    void setOwnedComponentTypes(std::vector<uint32_t> types)
    {
        m_ownedTypes = std::move(types);
    }

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

TEST(SystemEventsTest, EventBusPublishAndSubscribe)
{
    EventBus bus;
    bool received = false;
    uint32_t receivedId = 0;

    bus.subscribe<EntityDestroyedEvent>([&](const EntityDestroyedEvent& e)
    {
        received = true;
        receivedId = e.entityId;
    });

    bus.publish(EntityDestroyedEvent(99));

    EXPECT_TRUE(received);
    EXPECT_EQ(receivedId, 99u);
}

TEST(SystemEventsTest, WeatherEventThroughBus)
{
    EventBus bus;
    float receivedTemp = 0.0f;

    bus.subscribe<WeatherChangedEvent>([&](const WeatherChangedEvent& e)
    {
        receivedTemp = e.temperature;
    });

    bus.publish(WeatherChangedEvent(30.0f, 0.5f, 0.0f, 5.0f));

    EXPECT_FLOAT_EQ(receivedTemp, 30.0f);
}

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
