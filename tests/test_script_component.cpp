// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_script_component.cpp
/// @brief 3D_E-0874 lane-19 — ScriptingSystem::registerInstance keeps a
///        ScriptInstance's address, so the component must never move one:
///        not when its list grows, and not when an earlier script is removed.

#include "scripting/script_component.h"

#include <gtest/gtest.h>

#include <memory>

using namespace Vestige;

TEST(ScriptComponent, InstanceAddressSurvivesGrowthAndRemoval)
{
    ScriptComponent component;
    for (int i = 0; i < 3; ++i)
    {
        component.addScript("graph_" + std::to_string(i) + ".json");
        component.instances().push_back(std::make_unique<ScriptInstance>());
    }
    const ScriptInstance* third = component.instances()[2].get();

    for (int i = 3; i < 64; ++i)  // past several capacity doublings
    {
        component.addScript("graph_" + std::to_string(i) + ".json");
        component.instances().push_back(std::make_unique<ScriptInstance>());
    }
    EXPECT_EQ(component.instances()[2].get(), third);

    component.removeScript(0);
    EXPECT_EQ(component.instances()[1].get(), third);
}
