// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_localization_format.cpp
/// @brief 3D_E-0024 — patterns with named slots (INV-1), and the widgets that
///        build their text from them (INV-2).
#include "localization/localization_service.h"
#include "localization/string_table.h"
#include "ui/subtitle.h"
#include "ui/subtitle_renderer.h"
#include "ui/ui_fps_counter.h"
#include "ui/ui_interaction_prompt.h"
#include "ui/ui_slider.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

using namespace Vestige;

// ----- INV-1 -----------------------------------------------------------

TEST(LocalizationFormat, FillsNamedSlots)
{
    EXPECT_EQ(substituteTrArgs("Press [{key}] to {action}",
                               {{"key", "E"}, {"action", "open"}}),
              "Press [E] to open");
}

TEST(LocalizationFormat, SlotOrderComesFromThePattern)
{
    EXPECT_EQ(substituteTrArgs("{action} with [{key}]",
                               {{"key", "E"}, {"action", "Open"}}),
              "Open with [E]");
}

TEST(LocalizationFormat, SlotWithoutArgumentStaysAsWritten)
{
    EXPECT_EQ(substituteTrArgs("{speaker}: {text}", {{"text", "Hello"}}),
              "{speaker}: Hello");
}

TEST(LocalizationFormat, DoubledBracesWriteOneBrace)
{
    EXPECT_EQ(substituteTrArgs("{{literal}} {v}", {{"v", "x"}}), "{literal} x");
}

TEST(LocalizationFormat, UnterminatedBraceIsLiteral)
{
    EXPECT_EQ(substituteTrArgs("50% {", {}), "50% {");
}

// ----- INV-2 -----------------------------------------------------------

namespace
{

/// A table whose patterns put every slot in an order English does not.
StringTable reorderedTable()
{
    const auto path = std::filesystem::temp_directory_path() / "vestige_trf_reordered.json";
    {
        std::ofstream out(path);
        out << R"json({
  "ui.prompt.interact": "{action} <- [{key}]",
  "ui.prompt.verb.use": "USE",
  "subtitle.speaker_line": "{text} -- {speaker}",
  "subtitle.sound_cue": "(({text}))",
  "ui.hud.fps": "FPS={fps}",
  "ui.slider.percent": "%{value}"
})json";
    }
    StringTable table;
    table.loadFromFile(path.string());
    std::filesystem::remove(path);
    return table;
}

}  // namespace

TEST(LocalizationFormat, WidgetsBuildTheirTextFromPatterns)
{
    const StringTable table = reorderedTable();
    ASSERT_EQ(table.size(), 6u);
    const ScopedStringTableOverride scope(table);

    UIInteractionPrompt prompt;
    prompt.keyLabel = "E";  // actionVerb keeps its default key
    EXPECT_EQ(prompt.composedText(), "USE <- [E]");

    Subtitle line;
    line.category = SubtitleCategory::Dialogue;
    line.speaker  = "Moses";
    line.text     = "Draw near.";
    EXPECT_EQ(composeSubtitleText(line), "Draw near. -- Moses");

    Subtitle cue;
    cue.category = SubtitleCategory::SoundCue;
    cue.text     = "thunder";
    EXPECT_EQ(composeSubtitleText(cue), "((thunder))");

    EXPECT_EQ(UIFpsCounter::composeText(59.6f), "FPS=60");
    EXPECT_EQ(UISlider::composePercentText(42), "%42");
}
