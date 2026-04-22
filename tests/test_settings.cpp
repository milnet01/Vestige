// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_settings.cpp
/// @brief Phase 10 slice 13.1 — Settings primitive + atomic-write +
///        config-path helper tests. No engine wiring yet; everything
///        here exercises in-memory round-trips, schema migration
///        scaffolding, validation clamps, and on-disk load/save via
///        a tmp-directory fixture.

#include <gtest/gtest.h>

#include "core/settings.h"
#include "core/settings_migration.h"
#include "utils/atomic_write.h"
#include "utils/config_path.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace Vestige;

namespace
{

// Creates a unique tmp directory rooted at the system temp path.
// GoogleTest doesn't provide a built-in scoped tmpdir; this is the
// smallest pattern that cleans up after itself in the destructor.
class TmpDir
{
public:
    TmpDir()
    {
        fs::path base = fs::temp_directory_path();
        for (int attempt = 0; attempt < 100; ++attempt)
        {
            fs::path candidate = base / ("vestige_settings_test_"
                + std::to_string(::getpid()) + "_"
                + std::to_string(attempt));
            std::error_code ec;
            if (fs::create_directories(candidate, ec) && !ec)
            {
                m_path = candidate;
                return;
            }
        }
        ADD_FAILURE() << "TmpDir: could not create unique scratch directory";
    }

    ~TmpDir()
    {
        if (!m_path.empty())
        {
            std::error_code ec;
            fs::remove_all(m_path, ec);
        }
    }

    const fs::path& path() const { return m_path; }

private:
    fs::path m_path;
};

} // namespace

// ===== ConfigPath =============================================

TEST(ConfigPath, LinuxUsesXdgConfigHomeWhenSet)
{
#ifndef _WIN32
    // Save + restore env to keep other tests stable.
    const char* oldXdg  = std::getenv("XDG_CONFIG_HOME");
    std::string saved   = oldXdg ? oldXdg : "";
    bool hadValue       = (oldXdg != nullptr);

    ::setenv("XDG_CONFIG_HOME", "/tmp/xdg_fake_root", 1);
    fs::path dir = ConfigPath::getConfigDir();
    EXPECT_EQ(dir, fs::path("/tmp/xdg_fake_root/vestige"));

    if (hadValue)
    {
        ::setenv("XDG_CONFIG_HOME", saved.c_str(), 1);
    }
    else
    {
        ::unsetenv("XDG_CONFIG_HOME");
    }
#else
    GTEST_SKIP() << "POSIX-only test";
#endif
}

TEST(ConfigPath, LinuxFallsBackToHomeDotConfig)
{
#ifndef _WIN32
    const char* oldXdg  = std::getenv("XDG_CONFIG_HOME");
    std::string savedXdg = oldXdg ? oldXdg : "";
    bool hadXdg         = (oldXdg != nullptr);

    const char* oldHome = std::getenv("HOME");
    std::string savedHome = oldHome ? oldHome : "";
    bool hadHome        = (oldHome != nullptr);

    ::unsetenv("XDG_CONFIG_HOME");
    ::setenv("HOME", "/tmp/fake_home_root", 1);
    fs::path dir = ConfigPath::getConfigDir();
    EXPECT_EQ(dir, fs::path("/tmp/fake_home_root/.config/vestige"));

    if (hadXdg)  { ::setenv("XDG_CONFIG_HOME", savedXdg.c_str(), 1); }
    if (hadHome) { ::setenv("HOME",            savedHome.c_str(), 1); }
    else         { ::unsetenv("HOME"); }
#else
    GTEST_SKIP() << "POSIX-only test";
#endif
}

TEST(ConfigPath, GetConfigFileAppendsFilename)
{
    fs::path file = ConfigPath::getConfigFile("settings.json");
    EXPECT_EQ(file.filename(), fs::path("settings.json"));
    EXPECT_EQ(file.parent_path(), ConfigPath::getConfigDir());
}

// ===== AtomicWrite ============================================

TEST(AtomicWrite, WritesFileSuccessfully)
{
    TmpDir tmp;
    fs::path target = tmp.path() / "payload.txt";

    auto status = AtomicWrite::writeFile(target, "hello world\n");
    EXPECT_EQ(status, AtomicWrite::Status::Ok);
    ASSERT_TRUE(fs::exists(target));

    std::ifstream in(target);
    std::string body((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
    EXPECT_EQ(body, "hello world\n");
}

TEST(AtomicWrite, OverwritesExistingFile)
{
    TmpDir tmp;
    fs::path target = tmp.path() / "payload.txt";

    ASSERT_EQ(AtomicWrite::writeFile(target, "first"), AtomicWrite::Status::Ok);
    ASSERT_EQ(AtomicWrite::writeFile(target, "second"), AtomicWrite::Status::Ok);

    std::ifstream in(target);
    std::string body((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
    EXPECT_EQ(body, "second");
}

TEST(AtomicWrite, CreatesParentDirectory)
{
    TmpDir tmp;
    fs::path target = tmp.path() / "nested" / "deeper" / "payload.txt";

    auto status = AtomicWrite::writeFile(target, "x");
    EXPECT_EQ(status, AtomicWrite::Status::Ok);
    EXPECT_TRUE(fs::exists(target));
}

TEST(AtomicWrite, LeavesNoTempOnSuccess)
{
    TmpDir tmp;
    fs::path target  = tmp.path() / "payload.txt";
    fs::path tempSib = target;
    tempSib += ".tmp";

    ASSERT_EQ(AtomicWrite::writeFile(target, "done"), AtomicWrite::Status::Ok);
    EXPECT_FALSE(fs::exists(tempSib));
}

TEST(AtomicWrite, EmptyPayloadWritesEmptyFile)
{
    TmpDir tmp;
    fs::path target = tmp.path() / "empty.txt";

    EXPECT_EQ(AtomicWrite::writeFile(target, ""), AtomicWrite::Status::Ok);
    ASSERT_TRUE(fs::exists(target));
    EXPECT_EQ(fs::file_size(target), 0u);
}

TEST(AtomicWrite, DescribeReturnsNonNullForEveryStatus)
{
    EXPECT_STREQ(AtomicWrite::describe(AtomicWrite::Status::Ok),              "ok");
    EXPECT_STREQ(AtomicWrite::describe(AtomicWrite::Status::TempWriteFailed), "temp-write-failed");
    EXPECT_STREQ(AtomicWrite::describe(AtomicWrite::Status::FsyncFailed),     "fsync-failed");
    EXPECT_STREQ(AtomicWrite::describe(AtomicWrite::Status::RenameFailed),    "rename-failed");
    EXPECT_STREQ(AtomicWrite::describe(AtomicWrite::Status::DirFsyncFailed),  "dir-fsync-failed");
}

// ===== Settings defaults + round-trip =========================

TEST(Settings, DefaultsAreSane)
{
    Settings s;
    EXPECT_EQ(s.schemaVersion, kCurrentSchemaVersion);
    EXPECT_EQ(s.display.windowWidth,  1920);
    EXPECT_EQ(s.display.windowHeight, 1080);
    EXPECT_TRUE(s.display.vsync);
    EXPECT_EQ(s.display.qualityPreset, QualityPreset::High);
    EXPECT_FLOAT_EQ(s.display.renderScale, 1.0f);

    EXPECT_FLOAT_EQ(s.audio.busGains[0], 1.0f);  // master
    EXPECT_TRUE(s.audio.hrtfEnabled);

    EXPECT_FLOAT_EQ(s.controls.mouseSensitivity, 1.0f);
    EXPECT_FALSE(s.controls.invertY);

    EXPECT_EQ(s.accessibility.uiScalePreset, "1.0x");
    EXPECT_FALSE(s.accessibility.highContrast);
    EXPECT_EQ(s.accessibility.colorVisionFilter, "none");
}

TEST(Settings, RoundTripsThroughJson)
{
    Settings original;
    original.display.windowWidth   = 2560;
    original.display.windowHeight  = 1440;
    original.display.vsync         = false;
    original.display.qualityPreset = QualityPreset::Ultra;
    original.display.renderScale   = 0.75f;

    original.audio.busGains[1] = 0.5f;   // Music
    original.audio.hrtfEnabled = false;

    original.controls.mouseSensitivity = 2.5f;
    original.controls.invertY          = true;

    ActionBindingWire jump;
    jump.id = "Jump";
    jump.primary = {"keyboard", 65};
    jump.gamepad = {"gamepad",  0};
    original.controls.bindings.push_back(jump);

    original.accessibility.uiScalePreset     = "1.5x";
    original.accessibility.highContrast      = true;
    original.accessibility.reducedMotion     = true;
    original.accessibility.colorVisionFilter = "deuteranopia";
    original.accessibility.photosensitiveSafety.enabled = true;
    original.accessibility.postProcess.motionBlurEnabled = false;

    original.gameplay.values()["difficulty"] = "hard";
    original.gameplay.values()["fovDegrees"] = 95;

    json j = original.toJson();

    Settings restored;
    ASSERT_TRUE(restored.fromJson(j));
    EXPECT_EQ(restored, original);
}

TEST(Settings, FromJsonWithMissingSectionsKeepsDefaults)
{
    Settings s;
    json j = json::object();
    j["schemaVersion"] = kCurrentSchemaVersion;

    ASSERT_TRUE(s.fromJson(j));
    // All default values should survive a partial JSON.
    EXPECT_EQ(s.display.windowWidth, 1920);
    EXPECT_EQ(s.accessibility.uiScalePreset, "1.0x");
    EXPECT_TRUE(s.audio.hrtfEnabled);
}

TEST(Settings, FromJsonIgnoresUnknownFields)
{
    // Forward-compat: a v2 build's file loaded by a v1 build
    // contains extra fields. They must be silently ignored.
    Settings s;
    json j = s.toJson();
    j["unknownTopLevelField"]         = "future-data";
    j["display"]["unknownDisplayFld"] = 42;

    Settings restored;
    ASSERT_TRUE(restored.fromJson(j));
    EXPECT_EQ(restored, s);
}

TEST(Settings, FromJsonGameplayValuesPreserved)
{
    Settings s;
    s.gameplay.values()["custom_knob"]  = 3.14;
    s.gameplay.values()["custom_array"] = json::array({1, 2, 3});

    json j = s.toJson();

    Settings restored;
    ASSERT_TRUE(restored.fromJson(j));
    EXPECT_EQ(restored.gameplay.values()["custom_knob"].get<double>(), 3.14);
    EXPECT_EQ(restored.gameplay.values()["custom_array"].size(), 3u);
}

// ===== Quality preset enum ↔ string ===========================

TEST(Settings, QualityPresetRoundTrip)
{
    for (auto q : {QualityPreset::Low, QualityPreset::Medium, QualityPreset::High,
                    QualityPreset::Ultra, QualityPreset::Custom})
    {
        std::string s = qualityPresetToString(q);
        EXPECT_EQ(qualityPresetFromString(s), q) << "for preset " << qualityPresetLabel(q);
    }
}

TEST(Settings, QualityPresetFromUnknownFallsBack)
{
    EXPECT_EQ(qualityPresetFromString("bogus"), QualityPreset::Medium);
    EXPECT_EQ(qualityPresetFromString("bogus", QualityPreset::Low), QualityPreset::Low);
}

// ===== Validation =============================================

TEST(Settings, ValidationClampsNegativeRenderScale)
{
    Settings s;
    s.display.renderScale = -1.0f;
    json j = s.toJson();

    Settings restored;
    ASSERT_TRUE(restored.fromJson(j));
    EXPECT_FLOAT_EQ(restored.display.renderScale, 0.25f);
}

TEST(Settings, ValidationClampsHighRenderScale)
{
    Settings s;
    s.display.renderScale = 5.0f;
    json j = s.toJson();

    Settings restored;
    ASSERT_TRUE(restored.fromJson(j));
    EXPECT_FLOAT_EQ(restored.display.renderScale, 2.0f);
}

TEST(Settings, ValidationRejectsZeroResolution)
{
    json j = Settings{}.toJson();
    j["display"]["windowWidth"]  = 0;
    j["display"]["windowHeight"] = -5;

    Settings restored;
    ASSERT_TRUE(restored.fromJson(j));
    EXPECT_GT(restored.display.windowWidth, 0);
    EXPECT_GT(restored.display.windowHeight, 0);
}

TEST(Settings, ValidationClampsOutOfRangeBusGain)
{
    json j = Settings{}.toJson();
    j["audio"]["busGains"]["master"] = 2.0;
    j["audio"]["busGains"]["music"]  = -0.5;

    Settings restored;
    ASSERT_TRUE(restored.fromJson(j));
    EXPECT_FLOAT_EQ(restored.audio.busGains[0], 1.0f);
    EXPECT_FLOAT_EQ(restored.audio.busGains[1], 0.0f);
}

TEST(Settings, ValidationClampsMouseSensitivity)
{
    json j = Settings{}.toJson();
    j["controls"]["mouseSensitivity"] = 50.0;

    Settings restored;
    ASSERT_TRUE(restored.fromJson(j));
    EXPECT_FLOAT_EQ(restored.controls.mouseSensitivity, 10.0f);
}

TEST(Settings, ValidationClampsDeadzone)
{
    json j = Settings{}.toJson();
    j["controls"]["gamepadDeadzoneLeft"]  = 1.5;
    j["controls"]["gamepadDeadzoneRight"] = -0.2;

    Settings restored;
    ASSERT_TRUE(restored.fromJson(j));
    EXPECT_FLOAT_EQ(restored.controls.gamepadDeadzoneLeft,  0.9f);
    EXPECT_FLOAT_EQ(restored.controls.gamepadDeadzoneRight, 0.0f);
}

TEST(Settings, ValidationFallsBackUnknownScalePreset)
{
    json j = Settings{}.toJson();
    j["accessibility"]["uiScalePreset"] = "9000x";

    Settings restored;
    ASSERT_TRUE(restored.fromJson(j));
    EXPECT_EQ(restored.accessibility.uiScalePreset, "1.0x");
}

TEST(Settings, ValidationFallsBackUnknownSubtitleSize)
{
    json j = Settings{}.toJson();
    j["accessibility"]["subtitleSize"] = "gigantic";

    Settings restored;
    ASSERT_TRUE(restored.fromJson(j));
    EXPECT_EQ(restored.accessibility.subtitleSize, "medium");
}

TEST(Settings, ValidationFallsBackUnknownColorVisionFilter)
{
    json j = Settings{}.toJson();
    j["accessibility"]["colorVisionFilter"] = "bogus_filter";

    Settings restored;
    ASSERT_TRUE(restored.fromJson(j));
    EXPECT_EQ(restored.accessibility.colorVisionFilter, "none");
}

TEST(Settings, ValidationClampsPhotosensitiveFields)
{
    json j = Settings{}.toJson();
    j["accessibility"]["photosensitiveSafety"]["maxFlashAlpha"]       = 2.0;
    j["accessibility"]["photosensitiveSafety"]["shakeAmplitudeScale"] = -0.5;
    j["accessibility"]["photosensitiveSafety"]["maxStrobeHz"]         = -1.0;

    Settings restored;
    ASSERT_TRUE(restored.fromJson(j));
    EXPECT_FLOAT_EQ(restored.accessibility.photosensitiveSafety.maxFlashAlpha,       1.0f);
    EXPECT_FLOAT_EQ(restored.accessibility.photosensitiveSafety.shakeAmplitudeScale, 0.0f);
    EXPECT_FLOAT_EQ(restored.accessibility.photosensitiveSafety.maxStrobeHz,         0.0f);
}

TEST(Settings, KeybindingRoundTripPreservesDeviceAndScancode)
{
    Settings s;
    ActionBindingWire ab;
    ab.id = "MoveForward";
    ab.primary   = {"keyboard", 17};   // W on US QWERTY
    ab.secondary = {"keyboard", 200};
    ab.gamepad   = {"gamepad",  11};
    s.controls.bindings.push_back(ab);

    json j = s.toJson();

    Settings restored;
    ASSERT_TRUE(restored.fromJson(j));
    ASSERT_EQ(restored.controls.bindings.size(), 1u);
    EXPECT_EQ(restored.controls.bindings[0].id, "MoveForward");
    EXPECT_EQ(restored.controls.bindings[0].primary.device,   "keyboard");
    EXPECT_EQ(restored.controls.bindings[0].primary.scancode, 17);
    EXPECT_EQ(restored.controls.bindings[0].gamepad.scancode, 11);
}

TEST(Settings, KeybindingDeserializeDropsMalformedEntries)
{
    json j = Settings{}.toJson();
    json bindings = json::array();
    // Well-formed
    bindings.push_back(json{
        {"id", "Jump"},
        {"primary",   {{"device","keyboard"},{"scancode",57}}},
        {"secondary", {{"device","none"},    {"scancode",-1}}},
        {"gamepad",   {{"device","gamepad"}, {"scancode",0}}},
    });
    // Missing id → dropped
    bindings.push_back(json{
        {"primary", {{"device","keyboard"},{"scancode",65}}},
    });
    // id wrong type → dropped
    bindings.push_back(json{
        {"id", 42},
        {"primary", {{"device","keyboard"},{"scancode",65}}},
    });
    j["controls"]["bindings"] = bindings;

    Settings restored;
    ASSERT_TRUE(restored.fromJson(j));
    ASSERT_EQ(restored.controls.bindings.size(), 1u);
    EXPECT_EQ(restored.controls.bindings[0].id, "Jump");
}

// ===== Migration ==============================================

TEST(SettingsMigration, CurrentVersionIsANoOp)
{
    json j = Settings{}.toJson();
    ASSERT_TRUE(migrate(j));
    EXPECT_EQ(j["schemaVersion"].get<int>(), kCurrentSchemaVersion);
}

TEST(SettingsMigration, MissingSchemaVersionDefaultsToV1AndSucceeds)
{
    // v1 is the current schema, so a file missing the field
    // should round-trip cleanly without needing an explicit migration.
    json j = Settings{}.toJson();
    j.erase("schemaVersion");
    ASSERT_TRUE(migrate(j));
    EXPECT_EQ(j["schemaVersion"].get<int>(), kCurrentSchemaVersion);
}

TEST(SettingsMigration, FutureVersionRefusesToDowngrade)
{
    json j = Settings{}.toJson();
    j["schemaVersion"] = kCurrentSchemaVersion + 5;
    EXPECT_FALSE(migrate(j));
}

// ===== Disk load / save =======================================

TEST(SettingsDisk, MissingFileReturnsDefaults)
{
    TmpDir tmp;
    fs::path path = tmp.path() / "not_there.json";

    auto [s, status] = Settings::loadFromDisk(path);
    EXPECT_EQ(status, LoadStatus::FileMissing);
    EXPECT_EQ(s, Settings{});
}

TEST(SettingsDisk, SaveAndReloadIsEqual)
{
    TmpDir tmp;
    fs::path path = tmp.path() / "settings.json";

    Settings original;
    original.display.renderScale     = 0.5f;
    original.audio.busGains[3]       = 0.4f;   // sfx
    original.accessibility.highContrast = true;

    ASSERT_EQ(original.saveAtomic(path), SaveStatus::Ok);
    ASSERT_TRUE(fs::exists(path));

    auto [loaded, status] = Settings::loadFromDisk(path);
    EXPECT_EQ(status, LoadStatus::Ok);
    EXPECT_EQ(loaded, original);
}

TEST(SettingsDisk, CorruptFileMovesToSidecarAndReturnsDefaults)
{
    TmpDir tmp;
    fs::path path = tmp.path() / "settings.json";

    {
        std::ofstream f(path);
        f << "{this is not valid json";
    }
    ASSERT_TRUE(fs::exists(path));

    auto [loaded, status] = Settings::loadFromDisk(path);
    EXPECT_EQ(status, LoadStatus::ParseError);
    EXPECT_EQ(loaded, Settings{});

    fs::path corrupt = path;
    corrupt += ".corrupt";
    EXPECT_TRUE(fs::exists(corrupt));
    EXPECT_FALSE(fs::exists(path));
}

TEST(SettingsDisk, DefaultPathIsInConfigDir)
{
    fs::path path = Settings::defaultPath();
    EXPECT_EQ(path.filename(), fs::path("settings.json"));
    EXPECT_EQ(path.parent_path(), ConfigPath::getConfigDir());
}

TEST(SettingsDisk, SaveCreatesParentDirectoryIfMissing)
{
    TmpDir tmp;
    fs::path path = tmp.path() / "nested" / "config" / "settings.json";

    Settings s;
    EXPECT_EQ(s.saveAtomic(path), SaveStatus::Ok);
    EXPECT_TRUE(fs::exists(path));
}
