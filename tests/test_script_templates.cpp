// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_script_templates.cpp
/// @brief Tests for the pre-built gameplay script templates (Phase 9E-4).
#include "scripting/script_templates.h"
#include "scripting/node_type_registry.h"
#include "scripting/core_nodes.h"
#include "scripting/event_nodes.h"
#include "scripting/action_nodes.h"
#include "scripting/flow_nodes.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

using namespace Vestige;

// Registry populated once per test suite so we can confirm every template
// references only node types that were actually registered. Lives at
// namespace-scope (not an anonymous namespace) so the extern-C-linkage
// registration helpers declared in the headers resolve correctly.
struct TemplateRegistry : ::testing::Test
{
    static NodeTypeRegistry& registry()
    {
        static NodeTypeRegistry reg;
        static bool initialised = false;
        if (!initialised)
        {
            registerCoreNodeTypes(reg);
            registerEventNodeTypes(reg);
            registerActionNodeTypes(reg);
            registerFlowNodeTypes(reg);
            initialised = true;
        }
        return reg;
    }
};

namespace
{

// Every node typeName in the graph must resolve in the registry, and every
// connection must name a pin that the referenced node type actually owns.
// Both are important — a typo in a template pin name silently fails at
// runtime (the connection just never fires), so catching it in a test is
// the cheapest place to fix it.
void expectGraphIsExecutable(const ScriptGraph& graph,
                             const NodeTypeRegistry& registry)
{
    std::string err;
    ASSERT_TRUE(graph.validate(err)) << err;
    EXPECT_FALSE(graph.nodes.empty());
    EXPECT_FALSE(graph.connections.empty());

    std::set<uint32_t> nodeIds;
    for (const auto& node : graph.nodes)
    {
        const NodeTypeDescriptor* desc = registry.findNode(node.typeName);
        ASSERT_NE(desc, nullptr) << "unknown node type: " << node.typeName;
        EXPECT_TRUE(nodeIds.insert(node.id).second)
            << "duplicate node id in template: " << node.id;
    }

    auto hasPin = [&](const NodeTypeDescriptor& d,
                      const std::string& pinName,
                      bool isOutput)
    {
        const auto& defs = isOutput ? d.outputDefs : d.inputDefs;
        for (const auto& p : defs)
        {
            if (p.name == pinName) return true;
        }
        return false;
    };

    for (const auto& c : graph.connections)
    {
        const ScriptNodeDef* src = graph.findNode(c.sourceNode);
        const ScriptNodeDef* tgt = graph.findNode(c.targetNode);
        ASSERT_NE(src, nullptr);
        ASSERT_NE(tgt, nullptr);

        const NodeTypeDescriptor* srcDesc = registry.findNode(src->typeName);
        const NodeTypeDescriptor* tgtDesc = registry.findNode(tgt->typeName);
        ASSERT_NE(srcDesc, nullptr);
        ASSERT_NE(tgtDesc, nullptr);

        EXPECT_TRUE(hasPin(*srcDesc, c.sourcePin, /*isOutput=*/true))
            << src->typeName << " has no output pin named '" << c.sourcePin << "'";
        EXPECT_TRUE(hasPin(*tgtDesc, c.targetPin, /*isOutput=*/false))
            << tgt->typeName << " has no input pin named '" << c.targetPin << "'";
    }
}

} // namespace

TEST_F(TemplateRegistry, DoorTemplateValidates)
{
    ScriptGraph g = buildGameplayTemplate(GameplayTemplate::DOOR_OPENS);
    EXPECT_EQ(g.name, "DoorOpens");
    expectGraphIsExecutable(g, registry());
}

TEST_F(TemplateRegistry, CollectibleTemplateValidates)
{
    ScriptGraph g = buildGameplayTemplate(GameplayTemplate::COLLECTIBLE_ITEM);
    EXPECT_EQ(g.name, "CollectibleItem");
    expectGraphIsExecutable(g, registry());
}

TEST_F(TemplateRegistry, DamageZoneTemplateValidates)
{
    ScriptGraph g = buildGameplayTemplate(GameplayTemplate::DAMAGE_ZONE);
    EXPECT_EQ(g.name, "DamageZone");
    expectGraphIsExecutable(g, registry());
}

TEST_F(TemplateRegistry, CheckpointTemplateValidates)
{
    ScriptGraph g = buildGameplayTemplate(GameplayTemplate::CHECKPOINT);
    EXPECT_EQ(g.name, "Checkpoint");
    expectGraphIsExecutable(g, registry());
}

TEST_F(TemplateRegistry, DialogueTemplateValidates)
{
    ScriptGraph g = buildGameplayTemplate(GameplayTemplate::DIALOGUE_TRIGGER);
    EXPECT_EQ(g.name, "DialogueTrigger");
    expectGraphIsExecutable(g, registry());
}

TEST(GameplayTemplateMetadata, DisplayNamesAndDescriptions)
{
    // Every enumerated template should expose a non-empty display name and
    // description. Keeps the enum-to-metadata mapping honest if a new
    // template is added.
    for (auto t : {GameplayTemplate::DOOR_OPENS,
                   GameplayTemplate::COLLECTIBLE_ITEM,
                   GameplayTemplate::DAMAGE_ZONE,
                   GameplayTemplate::CHECKPOINT,
                   GameplayTemplate::DIALOGUE_TRIGGER})
    {
        EXPECT_STRNE(gameplayTemplateDisplayName(t), "");
        EXPECT_STRNE(gameplayTemplateDescription(t), "");
    }
}

TEST_F(TemplateRegistry, JsonRoundTripPreservesGraphShape)
{
    ScriptGraph original = buildGameplayTemplate(GameplayTemplate::COLLECTIBLE_ITEM);
    nlohmann::json j = original.toJson();
    ScriptGraph restored = ScriptGraph::fromJson(j);

    EXPECT_EQ(restored.name, original.name);
    EXPECT_EQ(restored.nodes.size(), original.nodes.size());
    EXPECT_EQ(restored.connections.size(), original.connections.size());
    expectGraphIsExecutable(restored, registry());
}

TEST_F(TemplateRegistry, AllTemplatesUseTriggerEntryPoint)
{
    // Every gameplay template currently starts from a trigger overlap so
    // designers attach the template to a volume entity. If a future
    // template drops this convention the test should be updated
    // intentionally rather than silently regress.
    for (auto t : {GameplayTemplate::DOOR_OPENS,
                   GameplayTemplate::COLLECTIBLE_ITEM,
                   GameplayTemplate::DAMAGE_ZONE,
                   GameplayTemplate::CHECKPOINT,
                   GameplayTemplate::DIALOGUE_TRIGGER})
    {
        ScriptGraph g = buildGameplayTemplate(t);
        bool hasTriggerEnter = false;
        for (const auto& n : g.nodes)
        {
            if (n.typeName == "OnTriggerEnter") { hasTriggerEnter = true; break; }
        }
        EXPECT_TRUE(hasTriggerEnter) << gameplayTemplateDisplayName(t);
    }
}
