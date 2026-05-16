// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_panel_registry.cpp
/// @brief Phase 10.9 Slice 12 Ed5 — PanelRegistry + IPanel contract tests.
///
/// The `drawMenuToggle` path itself needs an ImGui context, so it is
/// exercised at engine launch (same precedent as `text_renderer` batching
/// tests). Here we pin the headless surface: registration is idempotent,
/// null-safe, ordered, and surfaces every concrete panel through the
/// IPanel base pointer so the menu loop is type-blind.

#include "editor/panels/environment_panel.h"
#include "editor/panels/hdri_viewer_panel.h"
#include "editor/panels/i_panel.h"
#include "editor/panels/model_viewer_panel.h"
#include "editor/panels/navigation_panel.h"
#include "editor/panels/panel_registry.h"
#include "editor/panels/performance_panel.h"
#include "editor/panels/script_editor_panel.h"
#include "editor/panels/terrain_panel.h"
#include "editor/panels/texture_viewer_panel.h"
#include "editor/panels/ui_layout_panel.h"
#include "editor/panels/ui_runtime_panel.h"
#include "editor/panels/validation_panel.h"

#include <gtest/gtest.h>

namespace
{

using namespace Vestige;

class FakePanel : public IPanel
{
public:
    explicit FakePanel(const char* name) : m_name(name) {}
    const char* displayName() const override { return m_name; }
    bool isOpen() const override { return m_open; }
    void setOpen(bool open) override { m_open = open; }
private:
    const char* m_name;
    bool m_open = false;
};

TEST(PanelRegistryEd5, EmptyRegistryHasZeroPanels)
{
    PanelRegistry reg;
    EXPECT_EQ(reg.panelCount(), 0u);
    EXPECT_EQ(reg.panelAt(0), nullptr);
}

TEST(PanelRegistryEd5, RegisterPanelAppends)
{
    PanelRegistry reg;
    FakePanel a("A");
    FakePanel b("B");
    reg.registerPanel(&a);
    reg.registerPanel(&b);
    ASSERT_EQ(reg.panelCount(), 2u);
    EXPECT_EQ(reg.panelAt(0), &a);
    EXPECT_EQ(reg.panelAt(1), &b);
}

TEST(PanelRegistryEd5, RegisterNullIsNoOp)
{
    PanelRegistry reg;
    reg.registerPanel(nullptr);
    EXPECT_EQ(reg.panelCount(), 0u);
}

TEST(PanelRegistryEd5, RegisterDuplicateIsNoOp)
{
    PanelRegistry reg;
    FakePanel a("A");
    reg.registerPanel(&a);
    reg.registerPanel(&a);
    reg.registerPanel(&a);
    EXPECT_EQ(reg.panelCount(), 1u);
}

TEST(PanelRegistryEd5, PanelAtOutOfRangeReturnsNull)
{
    PanelRegistry reg;
    FakePanel a("A");
    reg.registerPanel(&a);
    EXPECT_EQ(reg.panelAt(0), &a);
    EXPECT_EQ(reg.panelAt(1), nullptr);
    EXPECT_EQ(reg.panelAt(99), nullptr);
}

TEST(PanelRegistryEd5, ClearEmptiesRegistry)
{
    PanelRegistry reg;
    FakePanel a("A");
    FakePanel b("B");
    reg.registerPanel(&a);
    reg.registerPanel(&b);
    reg.clear();
    EXPECT_EQ(reg.panelCount(), 0u);
    EXPECT_EQ(reg.panelAt(0), nullptr);
}

TEST(PanelRegistryEd5, IPanelPolymorphismSurfacesDisplayName)
{
    // The headline behaviour Ed5 enables: the menu loop walks `IPanel*` and
    // doesn't know the concrete panel type. Pin that the dispatch works.
    FakePanel a("Alpha");
    IPanel* base = &a;
    EXPECT_STREQ(base->displayName(), "Alpha");
    EXPECT_FALSE(base->isOpen());
    base->setOpen(true);
    EXPECT_TRUE(base->isOpen());
    EXPECT_TRUE(a.isOpen());
    EXPECT_EQ(base->shortcut(), nullptr);  // default
}

TEST(PanelRegistryEd5, RealPanelsExposeDisplayNames)
{
    // Pin each retrofitted concrete panel's displayName() — these strings
    // are what users see in the Window menu, and they're the entry point
    // for the registry-driven menu wiring in editor.cpp. If any of these
    // drift, the menu silently relabels.
    //
    // Note: all 11 concrete panel ctors below are headless-safe — none of
    // them touch GL in the constructor (textures / VAOs are created lazily
    // in initialize() / draw(), which we don't call here). If a future
    // panel adds GL-context calls to its ctor, this test will start
    // crashing rather than skipping; add a `GTEST_SKIP() << "needs GL"`
    // guard at the top in that case.
    EnvironmentPanel envPanel;
    TerrainPanel terrainPanel;
    NavigationPanel navPanel;
    UILayoutPanel uiLayoutPanel;
    UIRuntimePanel uiRuntimePanel;
    ScriptEditorPanel scriptPanel;
    PerformancePanel perfPanel;
    ValidationPanel validationPanel;
    ModelViewerPanel modelPanel;
    TextureViewerPanel texPanel;
    HdriViewerPanel hdriPanel;

    EXPECT_STREQ(envPanel.displayName(), "Environment");
    EXPECT_STREQ(terrainPanel.displayName(), "Terrain");
    EXPECT_STREQ(navPanel.displayName(), "Navigation");
    EXPECT_STREQ(uiLayoutPanel.displayName(), "UI Layout");
    EXPECT_STREQ(uiRuntimePanel.displayName(), "UI Runtime");
    EXPECT_STREQ(scriptPanel.displayName(), "Script Editor");
    EXPECT_STREQ(perfPanel.displayName(), "Performance");
    EXPECT_STREQ(perfPanel.shortcut(), "F12");
    EXPECT_STREQ(validationPanel.displayName(), "Scene Validation");
    EXPECT_STREQ(modelPanel.displayName(), "Model Viewer");
    EXPECT_STREQ(texPanel.displayName(), "Texture Viewer");
    EXPECT_STREQ(hdriPanel.displayName(), "HDRI Viewer");

    // Default shortcut() returns nullptr for panels that don't override.
    EXPECT_EQ(envPanel.shortcut(), nullptr);
}

TEST(PanelRegistryEd5, ScriptEditorPanelSetOpenBridgesOpenClose)
{
    // ScriptEditorPanel uses open()/close() instead of a setOpen() field.
    // Ed5 added setOpen() as an IPanel override that bridges to the same
    // visibility flag — pin both directions.
    ScriptEditorPanel panel;
    EXPECT_FALSE(panel.isOpen());
    panel.setOpen(true);
    EXPECT_TRUE(panel.isOpen());
    panel.close();
    EXPECT_FALSE(panel.isOpen());
    panel.open();
    EXPECT_TRUE(panel.isOpen());
    panel.setOpen(false);
    EXPECT_FALSE(panel.isOpen());
}

TEST(PanelRegistryEd5, RegistryRoundTripsThroughIPanelBase)
{
    // Walking the registry as `IPanel*` must reach every concrete panel
    // and read back its current open-state — that's the contract the
    // Window-menu loop depends on.
    PanelRegistry reg;
    EnvironmentPanel env;
    PerformancePanel perf;
    reg.registerPanel(&env);
    reg.registerPanel(&perf);

    IPanel* a = reg.panelAt(0);
    IPanel* b = reg.panelAt(1);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_STREQ(a->displayName(), "Environment");
    EXPECT_STREQ(b->displayName(), "Performance");

    a->setOpen(true);
    EXPECT_TRUE(env.isOpen());
    b->setOpen(true);
    EXPECT_TRUE(perf.isOpen());
}

} // namespace
