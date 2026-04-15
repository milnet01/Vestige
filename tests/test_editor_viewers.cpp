// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_editor_viewers.cpp
/// @brief Tests for Phase 9D editor viewer panels and game templates.
#include <gtest/gtest.h>

#include "editor/panels/texture_viewer_panel.h"
#include "editor/panels/hdri_viewer_panel.h"
#include "editor/panels/model_viewer_panel.h"
#include "editor/panels/template_dialog.h"

#include <set>

using namespace Vestige;

// ==========================================================================
// TextureViewerPanel tests
// ==========================================================================

class TextureViewerTest : public ::testing::Test {};

TEST_F(TextureViewerTest, DefaultStateIsClosed)
{
    TextureViewerPanel panel;
    EXPECT_FALSE(panel.isOpen());
}

TEST_F(TextureViewerTest, OpenCloseToggle)
{
    TextureViewerPanel panel;
    panel.setOpen(true);
    EXPECT_TRUE(panel.isOpen());
    panel.setOpen(false);
    EXPECT_FALSE(panel.isOpen());
}

TEST_F(TextureViewerTest, ChannelModeCycle)
{
    TextureViewerPanel panel;
    EXPECT_EQ(panel.getChannelMode(), ChannelMode::RGB);
    panel.setChannelMode(ChannelMode::R);
    EXPECT_EQ(panel.getChannelMode(), ChannelMode::R);
    panel.setChannelMode(ChannelMode::G);
    EXPECT_EQ(panel.getChannelMode(), ChannelMode::G);
    panel.setChannelMode(ChannelMode::B);
    EXPECT_EQ(panel.getChannelMode(), ChannelMode::B);
    panel.setChannelMode(ChannelMode::A);
    EXPECT_EQ(panel.getChannelMode(), ChannelMode::A);
}

TEST_F(TextureViewerTest, ZoomClamp)
{
    TextureViewerPanel panel;
    panel.setZoom(0.01f);
    EXPECT_GE(panel.getZoom(), 0.1f);
    panel.setZoom(100.0f);
    EXPECT_LE(panel.getZoom(), 32.0f);
    panel.setZoom(5.0f);
    EXPECT_FLOAT_EQ(panel.getZoom(), 5.0f);
}

TEST_F(TextureViewerTest, TileCountValues)
{
    TextureViewerPanel panel;
    EXPECT_EQ(panel.getTileCount(), 1);
    panel.setTileCount(2);
    EXPECT_EQ(panel.getTileCount(), 2);
    panel.setTileCount(3);
    EXPECT_EQ(panel.getTileCount(), 3);
    panel.setTileCount(0);
    EXPECT_EQ(panel.getTileCount(), 1);
    panel.setTileCount(5);
    EXPECT_EQ(panel.getTileCount(), 3);
}

TEST_F(TextureViewerTest, MipLevelClamp)
{
    TextureViewerPanel panel;
    EXPECT_EQ(panel.getMipLevel(), -1);  // Default: auto
    panel.setMipLevel(0);
    EXPECT_EQ(panel.getMipLevel(), 0);
    panel.setMipLevel(-2);
    EXPECT_EQ(panel.getMipLevel(), -1);
    // Max mip is 0 by default (no texture loaded)
    panel.setMipLevel(100);
    EXPECT_LE(panel.getMipLevel(), panel.getMaxMipLevel());
}

TEST_F(TextureViewerTest, PbrGroupDetection)
{
    // Test the static PBR detection method with a temp directory
    // Just verify it compiles and runs without crash on an empty dir
    auto groups = TextureViewerPanel::detectPbrGroups("/nonexistent/path");
    EXPECT_TRUE(groups.empty());
}

// ==========================================================================
// HdriViewerPanel tests
// ==========================================================================

class HdriViewerTest : public ::testing::Test {};

TEST_F(HdriViewerTest, DefaultStateIsClosed)
{
    HdriViewerPanel panel;
    EXPECT_FALSE(panel.isOpen());
}

TEST_F(HdriViewerTest, ExposureClamp)
{
    HdriViewerPanel panel;
    panel.setExposure(-15.0f);
    EXPECT_GE(panel.getExposure(), -10.0f);
    panel.setExposure(15.0f);
    EXPECT_LE(panel.getExposure(), 10.0f);
    panel.setExposure(2.5f);
    EXPECT_FLOAT_EQ(panel.getExposure(), 2.5f);
}

TEST_F(HdriViewerTest, PreviewOrbitAngles)
{
    HdriViewerPanel panel;
    panel.setPreviewAngles(0.0f, 0.0f);
    EXPECT_FLOAT_EQ(panel.getPreviewYaw(), 0.0f);
    EXPECT_FLOAT_EQ(panel.getPreviewPitch(), 0.0f);

    // Yaw wraps
    panel.setPreviewAngles(370.0f, 0.0f);
    EXPECT_LT(panel.getPreviewYaw(), 360.0f);

    // Pitch clamps
    panel.setPreviewAngles(0.0f, 100.0f);
    EXPECT_LE(panel.getPreviewPitch(), 90.0f);

    panel.setPreviewAngles(0.0f, -100.0f);
    EXPECT_GE(panel.getPreviewPitch(), -90.0f);
}

// ==========================================================================
// ModelViewerPanel tests
// ==========================================================================

class ModelViewerTest : public ::testing::Test {};

TEST_F(ModelViewerTest, DefaultStateIsClosed)
{
    ModelViewerPanel panel;
    EXPECT_FALSE(panel.isOpen());
}

TEST_F(ModelViewerTest, OrbitCameraDefaults)
{
    ModelViewerPanel panel;
    EXPECT_GT(panel.getOrbitDistance(), 0.0f);
    EXPECT_GE(panel.getOrbitYaw(), -180.0f);
    EXPECT_LE(panel.getOrbitYaw(), 360.0f);
}

TEST_F(ModelViewerTest, PlaybackSpeedClamp)
{
    ModelViewerPanel panel;
    panel.setPlaybackSpeed(-1.0f);
    EXPECT_GE(panel.getPlaybackSpeed(), 0.0f);
    panel.setPlaybackSpeed(20.0f);
    EXPECT_LE(panel.getPlaybackSpeed(), 10.0f);
    panel.setPlaybackSpeed(1.5f);
    EXPECT_FLOAT_EQ(panel.getPlaybackSpeed(), 1.5f);
}

// ==========================================================================
// TemplateDialog / GameTemplateConfig tests
// ==========================================================================

class GameTemplateTest : public ::testing::Test {};

TEST_F(GameTemplateTest, TemplateCount)
{
    auto templates = TemplateDialog::getTemplates();
    EXPECT_EQ(templates.size(), 6u);
}

TEST_F(GameTemplateTest, AllTemplatesHaveNames)
{
    auto templates = TemplateDialog::getTemplates();
    for (const auto& t : templates)
    {
        EXPECT_FALSE(t.displayName.empty()) << "Template missing display name";
    }
}

TEST_F(GameTemplateTest, AllTemplatesHaveDescriptions)
{
    auto templates = TemplateDialog::getTemplates();
    for (const auto& t : templates)
    {
        EXPECT_FALSE(t.description.empty()) << "Template missing description: " << t.displayName;
    }
}

TEST_F(GameTemplateTest, TemplateTypesUnique)
{
    auto templates = TemplateDialog::getTemplates();
    std::set<int> types;
    for (const auto& t : templates)
    {
        types.insert(static_cast<int>(t.type));
    }
    EXPECT_EQ(types.size(), templates.size());
}

TEST_F(GameTemplateTest, FirstPersonConfig)
{
    auto templates = TemplateDialog::getTemplates();
    const GameTemplateConfig* fps = nullptr;
    for (const auto& t : templates)
    {
        if (t.type == GameTemplateType::FIRST_PERSON_3D)
        {
            fps = &t;
            break;
        }
    }
    ASSERT_NE(fps, nullptr);
    EXPECT_EQ(fps->projectionType, ProjectionType::PERSPECTIVE);
    EXPECT_FLOAT_EQ(fps->fov, 90.0f);
    EXPECT_TRUE(fps->enableGravity);
    EXPECT_TRUE(fps->createSkybox);
}

TEST_F(GameTemplateTest, IsometricConfig)
{
    auto templates = TemplateDialog::getTemplates();
    const GameTemplateConfig* iso = nullptr;
    for (const auto& t : templates)
    {
        if (t.type == GameTemplateType::ISOMETRIC)
        {
            iso = &t;
            break;
        }
    }
    ASSERT_NE(iso, nullptr);
    EXPECT_EQ(iso->projectionType, ProjectionType::ORTHOGRAPHIC);
    EXPECT_GT(iso->orthoSize, 0.0f);
}

TEST_F(GameTemplateTest, TopDownConfig)
{
    auto templates = TemplateDialog::getTemplates();
    const GameTemplateConfig* td = nullptr;
    for (const auto& t : templates)
    {
        if (t.type == GameTemplateType::TOP_DOWN)
        {
            td = &t;
            break;
        }
    }
    ASSERT_NE(td, nullptr);
    EXPECT_EQ(td->projectionType, ProjectionType::ORTHOGRAPHIC);
    EXPECT_GT(td->cameraPosition.y, 10.0f);
}

TEST_F(GameTemplateTest, ThirdPersonConfig)
{
    auto templates = TemplateDialog::getTemplates();
    const GameTemplateConfig* tps = nullptr;
    for (const auto& t : templates)
    {
        if (t.type == GameTemplateType::THIRD_PERSON_3D)
        {
            tps = &t;
            break;
        }
    }
    ASSERT_NE(tps, nullptr);
    EXPECT_EQ(tps->projectionType, ProjectionType::PERSPECTIVE);
    EXPECT_TRUE(tps->createPlayerEntity);
}

TEST_F(GameTemplateTest, TwoPointFiveDConfig)
{
    auto templates = TemplateDialog::getTemplates();
    const GameTemplateConfig* tpfd = nullptr;
    for (const auto& t : templates)
    {
        if (t.type == GameTemplateType::TWO_POINT_FIVE_D)
        {
            tpfd = &t;
            break;
        }
    }
    ASSERT_NE(tpfd, nullptr);
    EXPECT_EQ(tpfd->projectionType, ProjectionType::PERSPECTIVE);
}

TEST_F(GameTemplateTest, PointAndClickConfig)
{
    auto templates = TemplateDialog::getTemplates();
    const GameTemplateConfig* pac = nullptr;
    for (const auto& t : templates)
    {
        if (t.type == GameTemplateType::POINT_AND_CLICK)
        {
            pac = &t;
            break;
        }
    }
    ASSERT_NE(pac, nullptr);
    EXPECT_EQ(pac->projectionType, ProjectionType::PERSPECTIVE);
}
