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
#include "core/settings_apply.h"
#include "core/settings_editor.h"
#include "core/settings_migration.h"
#include "input/input_bindings.h"
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
    // No schemaVersion marker defaults to v1 per the migrate() contract.
    // The chain then migrates forward to kCurrentSchemaVersion.
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

// ===== Slice 13.2 — apply layer (display) =====================

namespace
{

// Recording mock — captures the last setVideoMode call so tests
// can verify the apply path forwarded the settings block verbatim.
class RecordingDisplaySink final : public DisplayApplySink
{
public:
    int  width      = -1;
    int  height     = -1;
    bool fullscreen = false;
    bool vsync      = false;
    int  callCount  = 0;

    void setVideoMode(int w, int h, bool fs, bool vs) override
    {
        width      = w;
        height     = h;
        fullscreen = fs;
        vsync      = vs;
        ++callCount;
    }
};

} // namespace

TEST(SettingsApply, DisplayForwardsResolutionVsyncAndFullscreen)
{
    DisplaySettings d;
    d.windowWidth  = 2560;
    d.windowHeight = 1440;
    d.fullscreen   = true;
    d.vsync        = false;

    RecordingDisplaySink sink;
    applyDisplay(d, sink);

    EXPECT_EQ(sink.callCount, 1);
    EXPECT_EQ(sink.width,      2560);
    EXPECT_EQ(sink.height,     1440);
    EXPECT_TRUE(sink.fullscreen);
    EXPECT_FALSE(sink.vsync);
}

TEST(SettingsApply, DisplayForwardsWindowedDefaultCase)
{
    DisplaySettings d;   // defaults: 1920x1080, windowed, vsync on
    RecordingDisplaySink sink;
    applyDisplay(d, sink);

    EXPECT_EQ(sink.callCount, 1);
    EXPECT_EQ(sink.width,  1920);
    EXPECT_EQ(sink.height, 1080);
    EXPECT_FALSE(sink.fullscreen);
    EXPECT_TRUE(sink.vsync);
}

TEST(SettingsApply, ApplyDisplayIsIdempotentAcrossRepeatedCalls)
{
    DisplaySettings d;
    d.windowWidth  = 800;
    d.windowHeight = 600;
    d.vsync        = true;
    RecordingDisplaySink sink;

    applyDisplay(d, sink);
    applyDisplay(d, sink);
    applyDisplay(d, sink);

    EXPECT_EQ(sink.callCount, 3);
    EXPECT_EQ(sink.width,  800);
    EXPECT_EQ(sink.height, 600);
}

TEST(SettingsApply, DisplaySettingsFromValidatedLoadRoundTripsThroughApply)
{
    // Chain: user-written JSON → Settings::fromJson (validate) → applyDisplay.
    // Proves a value that survives validation reaches the sink intact.
    json j = Settings{}.toJson();
    j["display"]["windowWidth"]  = 1600;
    j["display"]["windowHeight"] = 900;
    j["display"]["fullscreen"]   = true;
    j["display"]["vsync"]        = false;

    Settings s;
    ASSERT_TRUE(s.fromJson(j));

    RecordingDisplaySink sink;
    applyDisplay(s.display, sink);

    EXPECT_EQ(sink.width,  1600);
    EXPECT_EQ(sink.height, 900);
    EXPECT_TRUE(sink.fullscreen);
    EXPECT_FALSE(sink.vsync);
}

// ===== Slice 14.1 — Onboarding block + v2 migration + legacy promotion ======

TEST(SettingsOnboarding, DefaultsAreFalseEmptyZero)
{
    Settings s;
    EXPECT_FALSE(s.onboarding.hasCompletedFirstRun);
    EXPECT_EQ(s.onboarding.completedAt, "");
    EXPECT_EQ(s.onboarding.skipCount, 0);
}

TEST(SettingsOnboarding, RoundTripsThroughJson)
{
    Settings original;
    original.onboarding.hasCompletedFirstRun = true;
    original.onboarding.completedAt          = "2026-04-22T14:30:00Z";
    original.onboarding.skipCount            = 2;

    Settings loaded;
    ASSERT_TRUE(loaded.fromJson(original.toJson()));
    EXPECT_EQ(loaded.onboarding, original.onboarding);
}

TEST(SettingsMigration, V1ToV2AddsOnboardingBlockWithDefaults)
{
    // Hand-craft a v1-shaped tree: take the current toJson and strip
    // the onboarding block + pin schemaVersion back to 1.
    json j = Settings{}.toJson();
    j.erase("onboarding");
    j["schemaVersion"] = 1;

    ASSERT_TRUE(migrate(j));
    EXPECT_EQ(j["schemaVersion"].get<int>(), 2);
    ASSERT_TRUE(j.contains("onboarding"));
    EXPECT_FALSE(j["onboarding"]["hasCompletedFirstRun"].get<bool>());
    EXPECT_EQ(j["onboarding"]["completedAt"].get<std::string>(), "");
    EXPECT_EQ(j["onboarding"]["skipCount"].get<int>(), 0);
}

TEST(SettingsOnboarding, LegacyFlagPromotesWhenFileExistsAndStructIsDefault)
{
    // Upgrader scenario: settings.json was never written (WelcomePanel
    // wrote the legacy flag instead). Loading defaults still honours it.
    TmpDir tmp;
    fs::path settingsPath = tmp.path() / "settings.json";
    fs::path flagPath     = tmp.path() / "welcome_shown";
    {
        std::ofstream f(flagPath);
        f << "1";
    }
    ASSERT_TRUE(fs::exists(flagPath));

    auto [s, status] = Settings::loadFromDisk(settingsPath);
    EXPECT_EQ(status, LoadStatus::FileMissing);
    EXPECT_TRUE(s.onboarding.hasCompletedFirstRun);
}

TEST(SettingsOnboarding, LegacyFlagPromotionDeletesFlagFile)
{
    // Promotion is lossless: the legacy signal is removed only after
    // the in-memory struct carries it.
    TmpDir tmp;
    fs::path settingsPath = tmp.path() / "settings.json";
    fs::path flagPath     = tmp.path() / "welcome_shown";
    {
        std::ofstream f(flagPath);
        f << "1";
    }

    auto [s, status] = Settings::loadFromDisk(settingsPath);
    EXPECT_EQ(status, LoadStatus::FileMissing);
    EXPECT_TRUE(s.onboarding.hasCompletedFirstRun);
    EXPECT_FALSE(fs::exists(flagPath));
}

TEST(SettingsOnboarding, PromotionSkippedWhenStructAlreadyComplete)
{
    // If settings.json already records completion, the legacy file is
    // left alone — the struct is authoritative and a stale flag file
    // is harmless until the user next saves. Verified by writing a
    // completed v2 settings.json + a flag file, and confirming the
    // flag file is not touched by load.
    TmpDir tmp;
    fs::path settingsPath = tmp.path() / "settings.json";
    fs::path flagPath     = tmp.path() / "welcome_shown";

    Settings pre;
    pre.onboarding.hasCompletedFirstRun = true;
    pre.onboarding.completedAt          = "2026-04-22T10:00:00Z";
    ASSERT_EQ(pre.saveAtomic(settingsPath), SaveStatus::Ok);

    {
        std::ofstream f(flagPath);
        f << "1";
    }
    ASSERT_TRUE(fs::exists(flagPath));

    auto [s, status] = Settings::loadFromDisk(settingsPath);
    EXPECT_EQ(status, LoadStatus::Ok);
    EXPECT_TRUE(s.onboarding.hasCompletedFirstRun);
    EXPECT_EQ(s.onboarding.completedAt, "2026-04-22T10:00:00Z");
    EXPECT_TRUE(fs::exists(flagPath))
        << "Legacy flag file should not be deleted when the in-memory "
           "struct is already authoritative.";
}

// ===== Slice 13.3 — AudioMixer API + audio apply path =======================

namespace
{

/// Recording mock — captures setBusGain calls so tests can verify
/// applyAudio pushed all six gains in the right order.
class RecordingAudioSink final : public AudioApplySink
{
public:
    std::vector<std::pair<AudioBus, float>> calls;

    void setBusGain(AudioBus bus, float gain) override
    {
        calls.emplace_back(bus, gain);
    }
};

} // namespace

TEST(AudioMixerApi, SetBusGainClampsToUnitRange)
{
    AudioMixer m;
    m.setBusGain(AudioBus::Music, -0.5f);
    EXPECT_FLOAT_EQ(m.getBusGain(AudioBus::Music), 0.0f);

    m.setBusGain(AudioBus::Music, 1.7f);
    EXPECT_FLOAT_EQ(m.getBusGain(AudioBus::Music), 1.0f);

    m.setBusGain(AudioBus::Music, 0.6f);
    EXPECT_FLOAT_EQ(m.getBusGain(AudioBus::Music), 0.6f);
}

TEST(AudioMixerApi, GetBusGainReturnsRawStoredValueNotMasterProduct)
{
    // getBusGain returns the per-bus slot; effectiveBusGain still
    // composes with Master. A test ensures the two don't drift apart.
    AudioMixer m;
    m.setBusGain(AudioBus::Master, 0.5f);
    m.setBusGain(AudioBus::Sfx,    0.8f);

    EXPECT_FLOAT_EQ(m.getBusGain(AudioBus::Sfx), 0.8f);
    EXPECT_FLOAT_EQ(effectiveBusGain(m, AudioBus::Sfx), 0.4f);
}

TEST(SettingsApply, AudioForwardsAllSixBusesInEnumOrder)
{
    AudioSettings a;
    a.busGains[0] = 0.5f;  // Master
    a.busGains[1] = 0.4f;  // Music
    a.busGains[2] = 0.6f;  // Voice
    a.busGains[3] = 0.3f;  // Sfx
    a.busGains[4] = 0.7f;  // Ambient
    a.busGains[5] = 0.2f;  // Ui

    RecordingAudioSink sink;
    applyAudio(a, sink);

    ASSERT_EQ(sink.calls.size(), 6u);
    EXPECT_EQ(sink.calls[0].first, AudioBus::Master);
    EXPECT_FLOAT_EQ(sink.calls[0].second, 0.5f);
    EXPECT_EQ(sink.calls[1].first, AudioBus::Music);
    EXPECT_FLOAT_EQ(sink.calls[1].second, 0.4f);
    EXPECT_EQ(sink.calls[5].first, AudioBus::Ui);
    EXPECT_FLOAT_EQ(sink.calls[5].second, 0.2f);
}

TEST(SettingsApply, AudioMixerSinkActuallyMutatesMixerState)
{
    AudioMixer m;
    AudioMixerApplySink sink(m);
    sink.setBusGain(AudioBus::Music, 0.42f);

    EXPECT_FLOAT_EQ(m.getBusGain(AudioBus::Music), 0.42f);
}

TEST(SettingsApply, AudioMixerSinkClampsOutOfRangeGainsFromDisk)
{
    // Simulate a hand-edited settings.json with out-of-range values.
    // The Settings validation step clamps to [0, 1] already, but
    // the apply-side sink must also clamp — belt and braces, since
    // tests (and future callers) can invoke the sink directly.
    AudioMixer m;
    AudioMixerApplySink sink(m);
    sink.setBusGain(AudioBus::Music, 2.5f);
    EXPECT_FLOAT_EQ(m.getBusGain(AudioBus::Music), 1.0f);
}

TEST(SettingsApply, AudioFromValidatedLoadRoundTripsThroughApply)
{
    // Chain: user-written JSON → Settings::fromJson (validate) → applyAudio.
    json j = Settings{}.toJson();
    j["audio"]["busGains"]["music"] = 0.33f;
    j["audio"]["busGains"]["ui"]    = 0.11f;

    Settings s;
    ASSERT_TRUE(s.fromJson(j));

    AudioMixer m;
    AudioMixerApplySink sink(m);
    applyAudio(s.audio, sink);

    EXPECT_FLOAT_EQ(m.getBusGain(AudioBus::Music), 0.33f);
    EXPECT_FLOAT_EQ(m.getBusGain(AudioBus::Ui),    0.11f);
}

// ===== Slice 13.3 — UI accessibility apply path =============================

namespace
{

/// Recording mock — captures the single batch call so tests can
/// verify applyUIAccessibility collapsed scale+contrast+motion
/// into exactly one rebuild.
class RecordingUIAccessSink final : public UIAccessibilityApplySink
{
public:
    int             callCount     = 0;
    UIScalePreset   scale         = UIScalePreset::X1_0;
    bool            highContrast  = false;
    bool            reducedMotion = false;

    void applyScaleContrastMotion(UIScalePreset s, bool hc, bool rm) override
    {
        scale         = s;
        highContrast  = hc;
        reducedMotion = rm;
        ++callCount;
    }
};

} // namespace

TEST(SettingsApply, UIAccessibilityMapsScalePresetStringToEnum)
{
    AccessibilitySettings a;
    a.uiScalePreset = "1.5x";
    a.highContrast  = false;
    a.reducedMotion = false;

    RecordingUIAccessSink sink;
    applyUIAccessibility(a, sink);

    EXPECT_EQ(sink.callCount, 1);
    EXPECT_EQ(sink.scale, UIScalePreset::X1_5);
}

TEST(SettingsApply, UIAccessibilityUnknownScalePresetFallsBackToUnity)
{
    AccessibilitySettings a;
    a.uiScalePreset = "3.0x";  // nonsense value
    RecordingUIAccessSink sink;
    applyUIAccessibility(a, sink);

    // Fallback matches Settings validation policy.
    EXPECT_EQ(sink.scale, UIScalePreset::X1_0);
}

TEST(SettingsApply, UIAccessibilityForwardsContrastAndMotionVerbatim)
{
    AccessibilitySettings a;
    a.uiScalePreset = "1.0x";
    a.highContrast  = true;
    a.reducedMotion = true;

    RecordingUIAccessSink sink;
    applyUIAccessibility(a, sink);

    EXPECT_TRUE(sink.highContrast);
    EXPECT_TRUE(sink.reducedMotion);
}

TEST(SettingsApply, UIAccessibilityIsSingleBatchCallNotThreeSeparateSetters)
{
    // Core behaviour the batch API exists to guarantee: one call,
    // one theme rebuild. Verified by the recording mock's callCount.
    AccessibilitySettings a;
    a.uiScalePreset = "2.0x";
    a.highContrast  = true;
    a.reducedMotion = true;

    RecordingUIAccessSink sink;
    applyUIAccessibility(a, sink);

    EXPECT_EQ(sink.callCount, 1)
        << "applyUIAccessibility must push all three fields in one batch "
           "call so UISystem::rebuildTheme runs exactly once per apply.";
}

TEST(SettingsApply, UIAccessibilityAllPresetStringsMapToDistinctEnums)
{
    // Pins the string→enum table — if someone renames a wire value,
    // this test fires.
    struct Case { const char* s; UIScalePreset expect; };
    const Case cases[] = {
        {"1.0x",  UIScalePreset::X1_0},
        {"1.25x", UIScalePreset::X1_25},
        {"1.5x",  UIScalePreset::X1_5},
        {"2.0x",  UIScalePreset::X2_0},
    };

    for (const auto& c : cases)
    {
        AccessibilitySettings a;
        a.uiScalePreset = c.s;
        RecordingUIAccessSink sink;
        applyUIAccessibility(a, sink);
        EXPECT_EQ(sink.scale, c.expect) << "Input: " << c.s;
    }
}

TEST(SettingsApply, UIAccessibilityFromValidatedLoadRoundTripsThroughApply)
{
    // Chain: JSON → Settings::fromJson → applyUIAccessibility.
    json j = Settings{}.toJson();
    j["accessibility"]["uiScalePreset"] = "1.5x";
    j["accessibility"]["highContrast"]  = true;
    j["accessibility"]["reducedMotion"] = true;

    Settings s;
    ASSERT_TRUE(s.fromJson(j));

    RecordingUIAccessSink sink;
    applyUIAccessibility(s.accessibility, sink);

    EXPECT_EQ(sink.scale,         UIScalePreset::X1_5);
    EXPECT_TRUE(sink.highContrast);
    EXPECT_TRUE(sink.reducedMotion);
}

// ===== Slice 13.3b — Renderer + Subtitle + HRTF + Photosensitive apply =====

namespace
{

class RecordingRendererAccessSink final : public RendererAccessibilityApplySink
{
public:
    ColorVisionMode mode = ColorVisionMode::Normal;
    PostProcessAccessibilitySettings pp;
    int colorCalls = 0;
    int postCalls  = 0;

    void setColorVisionMode(ColorVisionMode m) override
    {
        mode = m;
        ++colorCalls;
    }
    void setPostProcessAccessibility(
        const PostProcessAccessibilitySettings& p) override
    {
        pp = p;
        ++postCalls;
    }
};

class RecordingSubtitleSink final : public SubtitleApplySink
{
public:
    bool               enabled = false;
    SubtitleSizePreset size    = SubtitleSizePreset::Medium;
    int                calls   = 0;

    void setSubtitlesEnabled(bool e) override { enabled = e; ++calls; }
    void setSubtitleSize(SubtitleSizePreset p) override { size = p; ++calls; }
};

class RecordingHrtfSink final : public AudioHrtfApplySink
{
public:
    HrtfMode mode = HrtfMode::Auto;
    int      calls = 0;
    void setHrtfMode(HrtfMode m) override { mode = m; ++calls; }
};

class RecordingPhotoSink final : public PhotosensitiveApplySink
{
public:
    bool                  enabled = false;
    PhotosensitiveLimits  limits;
    int                   calls   = 0;
    void setPhotosensitiveEnabled(bool e) override { enabled = e; ++calls; }
    void setPhotosensitiveLimits(const PhotosensitiveLimits& l) override
    {
        limits = l;
        ++calls;
    }
};

} // namespace

TEST(SettingsApply, RendererAccessibilityMapsEveryColorVisionStringToDistinctEnum)
{
    struct Case { const char* s; ColorVisionMode expect; };
    const Case cases[] = {
        {"none",         ColorVisionMode::Normal},
        {"protanopia",   ColorVisionMode::Protanopia},
        {"deuteranopia", ColorVisionMode::Deuteranopia},
        {"tritanopia",   ColorVisionMode::Tritanopia},
    };

    for (const auto& c : cases)
    {
        AccessibilitySettings a;
        a.colorVisionFilter = c.s;
        RecordingRendererAccessSink sink;
        applyRendererAccessibility(a, sink);
        EXPECT_EQ(sink.mode, c.expect) << "Input: " << c.s;
    }
}

TEST(SettingsApply, RendererAccessibilityUnknownColorVisionFallsBackToNormal)
{
    AccessibilitySettings a;
    a.colorVisionFilter = "megachromy";  // nonsense
    RecordingRendererAccessSink sink;
    applyRendererAccessibility(a, sink);
    EXPECT_EQ(sink.mode, ColorVisionMode::Normal);
}

TEST(SettingsApply, RendererAccessibilityForwardsPostProcessWireFieldsVerbatim)
{
    AccessibilitySettings a;
    a.postProcess.depthOfFieldEnabled = false;
    a.postProcess.motionBlurEnabled   = false;
    a.postProcess.fogEnabled          = true;
    a.postProcess.fogIntensityScale   = 0.3f;
    a.postProcess.reduceMotionFog     = true;

    RecordingRendererAccessSink sink;
    applyRendererAccessibility(a, sink);

    EXPECT_FALSE(sink.pp.depthOfFieldEnabled);
    EXPECT_FALSE(sink.pp.motionBlurEnabled);
    EXPECT_TRUE(sink.pp.fogEnabled);
    EXPECT_FLOAT_EQ(sink.pp.fogIntensityScale, 0.3f);
    EXPECT_TRUE(sink.pp.reduceMotionFog);
}

TEST(SettingsApply, SubtitleMapsEverySizeStringToDistinctEnum)
{
    struct Case { const char* s; SubtitleSizePreset expect; };
    const Case cases[] = {
        {"small",  SubtitleSizePreset::Small},
        {"medium", SubtitleSizePreset::Medium},
        {"large",  SubtitleSizePreset::Large},
        {"xl",     SubtitleSizePreset::XL},
    };

    for (const auto& c : cases)
    {
        AccessibilitySettings a;
        a.subtitleSize = c.s;
        RecordingSubtitleSink sink;
        applySubtitleSettings(a, sink);
        EXPECT_EQ(sink.size, c.expect) << "Input: " << c.s;
    }
}

TEST(SettingsApply, SubtitleForwardsEnabledFlag)
{
    AccessibilitySettings a;
    a.subtitlesEnabled = false;
    a.subtitleSize     = "medium";

    RecordingSubtitleSink sink;
    applySubtitleSettings(a, sink);
    EXPECT_FALSE(sink.enabled);
    EXPECT_EQ(sink.size, SubtitleSizePreset::Medium);
}

TEST(SettingsApply, HrtfBoolMapsToAutoOrDisabled)
{
    // true → Auto (driver decides), false → Disabled (force off).
    {
        AudioSettings a;
        a.hrtfEnabled = true;
        RecordingHrtfSink sink;
        applyAudioHrtf(a, sink);
        EXPECT_EQ(sink.mode, HrtfMode::Auto);
    }
    {
        AudioSettings a;
        a.hrtfEnabled = false;
        RecordingHrtfSink sink;
        applyAudioHrtf(a, sink);
        EXPECT_EQ(sink.mode, HrtfMode::Disabled);
    }
}

TEST(SettingsApply, PhotosensitiveForwardsEnabledAndLimits)
{
    AccessibilitySettings a;
    a.photosensitiveSafety.enabled             = true;
    a.photosensitiveSafety.maxFlashAlpha       = 0.10f;
    a.photosensitiveSafety.shakeAmplitudeScale = 0.5f;
    a.photosensitiveSafety.maxStrobeHz         = 1.5f;
    a.photosensitiveSafety.bloomIntensityScale = 0.4f;

    RecordingPhotoSink sink;
    applyPhotosensitiveSafety(a, sink);

    EXPECT_TRUE(sink.enabled);
    EXPECT_FLOAT_EQ(sink.limits.maxFlashAlpha,       0.10f);
    EXPECT_FLOAT_EQ(sink.limits.shakeAmplitudeScale, 0.5f);
    EXPECT_FLOAT_EQ(sink.limits.maxStrobeHz,         1.5f);
    EXPECT_FLOAT_EQ(sink.limits.bloomIntensityScale, 0.4f);
}

TEST(SettingsApply, RendererAccessibilityFullAccessibilityRoundTripThroughJson)
{
    // End-to-end: JSON → Settings::fromJson (validate) → apply.
    json j = Settings{}.toJson();
    j["accessibility"]["colorVisionFilter"] = "deuteranopia";
    j["accessibility"]["postProcess"]["depthOfFieldEnabled"] = false;
    j["accessibility"]["postProcess"]["fogIntensityScale"]   = 0.5f;

    Settings s;
    ASSERT_TRUE(s.fromJson(j));

    RecordingRendererAccessSink sink;
    applyRendererAccessibility(s.accessibility, sink);

    EXPECT_EQ(sink.mode, ColorVisionMode::Deuteranopia);
    EXPECT_FALSE(sink.pp.depthOfFieldEnabled);
    EXPECT_FLOAT_EQ(sink.pp.fogIntensityScale, 0.5f);
}

TEST(SettingsApply, SubtitleQueueApplySinkActuallyMutatesQueueState)
{
    // Production sink forwards to a live SubtitleQueue.
    SubtitleQueue q;
    SubtitleQueueApplySink sink(q);

    sink.setSubtitleSize(SubtitleSizePreset::Large);
    EXPECT_EQ(q.sizePreset(), SubtitleSizePreset::Large);

    sink.setSubtitlesEnabled(false);
    EXPECT_FALSE(sink.subtitlesEnabled());
}

// ===== Slice 13.4 — Input bindings extract + apply ==========================

namespace
{

InputAction makeAction(const std::string& id,
                        InputBinding primary,
                        InputBinding secondary = InputBinding::none(),
                        InputBinding gamepad   = InputBinding::none())
{
    InputAction a;
    a.id        = id;
    a.label     = id;
    a.category  = "test";
    a.primary   = primary;
    a.secondary = secondary;
    a.gamepad   = gamepad;
    return a;
}

} // namespace

TEST(SettingsApplyInputBindings, ExtractEmitsEveryRegisteredActionInOrder)
{
    InputActionMap map;
    map.addAction(makeAction("Jump", InputBinding::key(32)));
    map.addAction(makeAction("Fire", InputBinding::mouse(0)));
    map.addAction(makeAction("Pause", InputBinding::key(256)));

    auto wires = extractInputBindings(map);
    ASSERT_EQ(wires.size(), 3u);
    EXPECT_EQ(wires[0].id, "Jump");
    EXPECT_EQ(wires[1].id, "Fire");
    EXPECT_EQ(wires[2].id, "Pause");
}

TEST(SettingsApplyInputBindings, ExtractRoundTripsDeviceStrings)
{
    InputActionMap map;
    map.addAction(makeAction("K",  InputBinding::key(65)));
    map.addAction(makeAction("M",  InputBinding::mouse(1)));
    map.addAction(makeAction("G",  InputBinding::gamepad(3)));
    map.addAction(makeAction("Un", InputBinding::none()));

    auto wires = extractInputBindings(map);
    ASSERT_EQ(wires.size(), 4u);
    EXPECT_EQ(wires[0].primary.device, "keyboard");
    EXPECT_EQ(wires[0].primary.scancode, 65);
    EXPECT_EQ(wires[1].primary.device, "mouse");
    EXPECT_EQ(wires[1].primary.scancode, 1);
    EXPECT_EQ(wires[2].primary.device, "gamepad");
    EXPECT_EQ(wires[2].primary.scancode, 3);
    EXPECT_EQ(wires[3].primary.device, "none");
    EXPECT_EQ(wires[3].primary.scancode, -1);
}

TEST(SettingsApplyInputBindings, ExtractPreservesAllThreeSlots)
{
    InputActionMap map;
    map.addAction(makeAction("Use",
        InputBinding::key(69),
        InputBinding::mouse(2),
        InputBinding::gamepad(0)));

    auto wires = extractInputBindings(map);
    ASSERT_EQ(wires.size(), 1u);
    EXPECT_EQ(wires[0].primary.device,   "keyboard");
    EXPECT_EQ(wires[0].primary.scancode, 69);
    EXPECT_EQ(wires[0].secondary.device, "mouse");
    EXPECT_EQ(wires[0].secondary.scancode, 2);
    EXPECT_EQ(wires[0].gamepad.device,   "gamepad");
    EXPECT_EQ(wires[0].gamepad.scancode, 0);
}

TEST(SettingsApplyInputBindings, ApplyUpdatesBindingsOfRegisteredActions)
{
    // Init-order contract: register first, then apply wires.
    InputActionMap map;
    map.addAction(makeAction("Jump", InputBinding::key(32)));

    std::vector<ActionBindingWire> wires;
    ActionBindingWire w;
    w.id = "Jump";
    w.primary.device   = "keyboard";
    w.primary.scancode = 87;   // Remapped to 'W'
    wires.push_back(w);

    applyInputBindings(wires, map);

    const InputAction* a = map.findAction("Jump");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->primary.device, InputDevice::Keyboard);
    EXPECT_EQ(a->primary.code,   87);
}

TEST(SettingsApplyInputBindings, ApplyDropsPhantomIdsWithoutRegistering)
{
    // Phantom id — a typo or stale save shouldn't magically register
    // new actions. The map stays at the one registered action; the
    // phantom entry is dropped.
    InputActionMap map;
    map.addAction(makeAction("Jump", InputBinding::key(32)));

    std::vector<ActionBindingWire> wires;
    ActionBindingWire w;
    w.id = "TotallyNotRegistered";
    w.primary.device   = "keyboard";
    w.primary.scancode = 65;
    wires.push_back(w);

    applyInputBindings(wires, map);

    EXPECT_EQ(map.actions().size(), 1u);
    EXPECT_NE(map.findAction("Jump"),                 nullptr);
    EXPECT_EQ(map.findAction("TotallyNotRegistered"), nullptr);
}

TEST(SettingsApplyInputBindings, ApplyPreservesActionsNotInWires)
{
    // An action registered on the map but absent from the wire list
    // keeps its current bindings — no clobbering to defaults.
    InputActionMap map;
    map.addAction(makeAction("Jump", InputBinding::key(32)));
    map.addAction(makeAction("Fire", InputBinding::mouse(0)));

    std::vector<ActionBindingWire> wires;
    ActionBindingWire w;
    w.id = "Jump";
    w.primary.device   = "keyboard";
    w.primary.scancode = 87;
    wires.push_back(w);

    applyInputBindings(wires, map);

    const InputAction* fire = map.findAction("Fire");
    ASSERT_NE(fire, nullptr);
    // Fire was absent from wires — its binding must be untouched.
    EXPECT_EQ(fire->primary.device, InputDevice::Mouse);
    EXPECT_EQ(fire->primary.code,   0);
}

TEST(SettingsApplyInputBindings, UnknownDeviceStringFallsBackToNone)
{
    InputActionMap map;
    map.addAction(makeAction("Jump", InputBinding::key(32)));

    std::vector<ActionBindingWire> wires;
    ActionBindingWire w;
    w.id = "Jump";
    w.primary.device   = "chorded-keyboard";   // nonsense
    w.primary.scancode = 99;
    wires.push_back(w);

    applyInputBindings(wires, map);

    const InputAction* a = map.findAction("Jump");
    ASSERT_NE(a, nullptr);
    // Unknown device string → Unbound on apply. code is also
    // normalised because the wire was marked "unbound" by device.
    EXPECT_EQ(a->primary.device, InputDevice::None);
    EXPECT_FALSE(a->primary.isBound());
}

TEST(SettingsApplyInputBindings, NegativeScancodeNormalisesToUnboundBinding)
{
    // Wire carries device "keyboard" but scancode -1 — that's the
    // "I was a keyboard binding but the user cleared me" case.
    // Must normalise to fully Unbound so isBound() returns false.
    InputActionMap map;
    map.addAction(makeAction("Jump", InputBinding::key(32)));

    std::vector<ActionBindingWire> wires;
    ActionBindingWire w;
    w.id = "Jump";
    w.primary.device   = "keyboard";
    w.primary.scancode = -1;
    wires.push_back(w);

    applyInputBindings(wires, map);

    const InputAction* a = map.findAction("Jump");
    ASSERT_NE(a, nullptr);
    EXPECT_FALSE(a->primary.isBound());
    EXPECT_EQ(a->primary.device, InputDevice::None);
}

TEST(SettingsApplyInputBindings, ExtractThenApplyRoundTripIsLossless)
{
    // Full round-trip: extract to wires, fresh map with same
    // registered ids, apply wires, compare binding-by-binding.
    InputActionMap source;
    source.addAction(makeAction("Jump",
        InputBinding::key(32),
        InputBinding::mouse(1),
        InputBinding::gamepad(0)));
    source.addAction(makeAction("Fire",
        InputBinding::mouse(0),
        InputBinding::none(),
        InputBinding::gamepad(7)));

    auto wires = extractInputBindings(source);

    InputActionMap target;
    target.addAction(makeAction("Jump", InputBinding::none()));
    target.addAction(makeAction("Fire", InputBinding::none()));
    applyInputBindings(wires, target);

    const InputAction* jump = target.findAction("Jump");
    ASSERT_NE(jump, nullptr);
    EXPECT_EQ(jump->primary,   (InputBinding{InputDevice::Keyboard, 32}));
    EXPECT_EQ(jump->secondary, (InputBinding{InputDevice::Mouse,    1}));
    EXPECT_EQ(jump->gamepad,   (InputBinding{InputDevice::Gamepad,  0}));

    const InputAction* fire = target.findAction("Fire");
    ASSERT_NE(fire, nullptr);
    EXPECT_EQ(fire->primary,   (InputBinding{InputDevice::Mouse, 0}));
    EXPECT_FALSE(fire->secondary.isBound());
    EXPECT_EQ(fire->gamepad,   (InputBinding{InputDevice::Gamepad, 7}));
}

TEST(SettingsApplyInputBindings, ApplyFromSettingsControlsBlockIntegration)
{
    // End-to-end: ControlsSettings::bindings populated via JSON load,
    // applyInputBindings against a registered map, bindings present.
    Settings s;
    ActionBindingWire w;
    w.id = "Pause";
    w.primary.device   = "keyboard";
    w.primary.scancode = 256;    // GLFW_KEY_ESCAPE
    s.controls.bindings.push_back(w);

    InputActionMap map;
    map.addAction(makeAction("Pause", InputBinding::none()));

    applyInputBindings(s.controls.bindings, map);

    const InputAction* p = map.findAction("Pause");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->primary.device, InputDevice::Keyboard);
    EXPECT_EQ(p->primary.code,   256);
}

// ===== Slice 13.5 — SettingsEditor orchestrator =============================

namespace
{

/// Shared recording sinks for every SettingsEditor test. Kept
/// local to this TU so the wizard / apply tests don't see them.
class Record13_5Display final : public DisplayApplySink
{
public:
    int calls = 0;
    int w = 0, h = 0; bool fs = false, vs = false;
    void setVideoMode(int ww, int hh, bool ffs, bool vvs) override
    {
        w = ww; h = hh; fs = ffs; vs = vvs; ++calls;
    }
};

class Record13_5Audio final : public AudioApplySink
{
public:
    int calls = 0;
    std::array<float, AudioBusCount> lastGains{};
    void setBusGain(AudioBus bus, float gain) override
    {
        lastGains[static_cast<std::size_t>(bus)] = gain;
        ++calls;
    }
};

class Record13_5UIAccess final : public UIAccessibilityApplySink
{
public:
    int calls = 0;
    UIScalePreset scale = UIScalePreset::X1_0;
    bool hc = false, rm = false;
    void applyScaleContrastMotion(UIScalePreset s, bool h, bool r) override
    {
        scale = s; hc = h; rm = r; ++calls;
    }
};

} // namespace

TEST(SettingsEditor, InitialStateMatchesAppliedAndIsNotDirty)
{
    Settings s;
    s.display.windowWidth = 1600;
    s.audio.busGains[1]   = 0.42f;

    SettingsEditor editor(s, {});
    EXPECT_EQ(editor.applied(), s);
    EXPECT_EQ(editor.pending(), s);
    EXPECT_FALSE(editor.isDirty());
}

TEST(SettingsEditor, MutateDivergesPendingFromAppliedAndMarksDirty)
{
    Settings s;
    SettingsEditor editor(s, {});

    editor.mutate([](Settings& p) { p.display.renderScale = 0.75f; });
    EXPECT_TRUE(editor.isDirty());
    EXPECT_FLOAT_EQ(editor.pending().display.renderScale, 0.75f);
    EXPECT_FLOAT_EQ(editor.applied().display.renderScale, 1.0f);
}

TEST(SettingsEditor, MutatePushesLiveThroughEveryConfiguredSink)
{
    Record13_5Display display;
    Record13_5Audio   audio;
    Record13_5UIAccess ui;

    SettingsEditor::ApplyTargets t{};
    t.display         = &display;
    t.audio           = &audio;
    t.uiAccessibility = &ui;

    SettingsEditor editor(Settings{}, t);
    editor.mutate([](Settings& p)
    {
        p.display.windowWidth        = 2560;
        p.audio.busGains[3]          = 0.33f;
        p.accessibility.highContrast = true;
    });

    // Every sink should see exactly one call (one mutation cycle).
    EXPECT_EQ(display.calls, 1);
    EXPECT_EQ(audio.calls,   static_cast<int>(AudioBusCount));
    EXPECT_EQ(ui.calls,      1);
    EXPECT_EQ(display.w,              2560);
    EXPECT_FLOAT_EQ(audio.lastGains[3], 0.33f);
    EXPECT_TRUE(ui.hc);
}

TEST(SettingsEditor, ApplyCommitsPendingToAppliedAndPersists)
{
    TmpDir tmp;
    fs::path path = tmp.path() / "settings.json";

    Settings initial;
    SettingsEditor editor(initial, {});
    editor.mutate([](Settings& p) { p.display.renderScale = 1.5f; });
    ASSERT_TRUE(editor.isDirty());

    SaveStatus s = editor.apply(path);
    EXPECT_EQ(s, SaveStatus::Ok);
    EXPECT_FALSE(editor.isDirty());
    EXPECT_EQ(editor.applied(), editor.pending());

    auto [reloaded, status] = Settings::loadFromDisk(path);
    EXPECT_EQ(status, LoadStatus::Ok);
    EXPECT_FLOAT_EQ(reloaded.display.renderScale, 1.5f);
}

TEST(SettingsEditor, ApplyWithFailedWriteKeepsEditorDirty)
{
    // Writing to a path whose parent does not exist succeeds today
    // (AtomicWrite::writeFile creates parents). Simulate failure by
    // passing a path that is itself an existing directory, which
    // AtomicWrite can't overwrite as a file.
    TmpDir tmp;
    fs::path badDirPath = tmp.path() / "is_a_dir";
    fs::create_directories(badDirPath);

    SettingsEditor editor(Settings{}, {});
    editor.mutate([](Settings& p) { p.display.renderScale = 2.0f; });
    ASSERT_TRUE(editor.isDirty());

    SaveStatus s = editor.apply(badDirPath);
    EXPECT_NE(s, SaveStatus::Ok);
    // A failed save leaves the editor still dirty — the user can
    // retry or revert. Live state stays as it is (the subsystems
    // already reflect pending from the mutate() call).
    EXPECT_TRUE(editor.isDirty());
}

TEST(SettingsEditor, RevertRestoresPendingFromAppliedAndRepushes)
{
    Record13_5Display display;
    SettingsEditor::ApplyTargets t{};
    t.display = &display;

    Settings initial;   // defaults
    SettingsEditor editor(initial, t);
    editor.mutate([](Settings& p) { p.display.windowWidth = 2560; });
    ASSERT_TRUE(editor.isDirty());
    EXPECT_EQ(display.w, 2560);   // live-applied

    editor.revert();
    EXPECT_FALSE(editor.isDirty());
    EXPECT_EQ(editor.pending(), initial);
    // Revert re-pushes through the sinks so subsystems roll back.
    EXPECT_EQ(display.w, initial.display.windowWidth);
}

TEST(SettingsEditor, RestoreDisplayDefaultsResetsOnlyDisplay)
{
    Settings initial;
    initial.display.renderScale = 0.5f;
    initial.audio.busGains[1]   = 0.33f;
    initial.accessibility.highContrast = true;

    SettingsEditor editor(initial, {});
    editor.restoreDisplayDefaults();

    EXPECT_EQ(editor.pending().display, DisplaySettings{});
    // Other categories untouched.
    EXPECT_FLOAT_EQ(editor.pending().audio.busGains[1], 0.33f);
    EXPECT_TRUE(editor.pending().accessibility.highContrast);
}

TEST(SettingsEditor, RestoreAudioDefaultsResetsOnlyAudio)
{
    Settings initial;
    initial.display.renderScale = 0.5f;
    initial.audio.busGains[1]   = 0.33f;

    SettingsEditor editor(initial, {});
    editor.restoreAudioDefaults();

    EXPECT_EQ(editor.pending().audio, AudioSettings{});
    EXPECT_FLOAT_EQ(editor.pending().display.renderScale, 0.5f);
}

TEST(SettingsEditor, RestoreAccessibilityDefaultsResetsOnlyAccessibility)
{
    Settings initial;
    initial.display.renderScale        = 0.5f;
    initial.accessibility.highContrast = true;
    initial.accessibility.uiScalePreset = "2.0x";

    SettingsEditor editor(initial, {});
    editor.restoreAccessibilityDefaults();

    EXPECT_EQ(editor.pending().accessibility, AccessibilitySettings{});
    EXPECT_FLOAT_EQ(editor.pending().display.renderScale, 0.5f);
}

TEST(SettingsEditor, RestoreAllDefaultsResetsEverythingExceptOnboardingAndSchema)
{
    Settings initial;
    initial.schemaVersion              = kCurrentSchemaVersion;
    initial.display.renderScale        = 0.5f;
    initial.audio.busGains[1]          = 0.33f;
    initial.accessibility.highContrast = true;
    initial.onboarding.hasCompletedFirstRun = true;
    initial.onboarding.completedAt          = "2026-04-22T10:00:00Z";

    SettingsEditor editor(initial, {});
    editor.restoreAllDefaults();

    EXPECT_EQ(editor.pending().display,       DisplaySettings{});
    EXPECT_EQ(editor.pending().audio,         AudioSettings{});
    EXPECT_EQ(editor.pending().accessibility, AccessibilitySettings{});
    // Onboarding must survive — a full-reset shouldn't re-open the
    // first-run wizard.
    EXPECT_TRUE(editor.pending().onboarding.hasCompletedFirstRun);
    EXPECT_EQ(editor.pending().onboarding.completedAt,
              "2026-04-22T10:00:00Z");
    // schemaVersion must also survive — resetting it to 0 would
    // break the next load's migration path.
    EXPECT_EQ(editor.pending().schemaVersion, kCurrentSchemaVersion);
}

TEST(SettingsEditor, PerCategoryRestoreIsLiveAppliedThroughSinks)
{
    Record13_5Display display;
    SettingsEditor::ApplyTargets t{};
    t.display = &display;

    Settings initial;
    initial.display.windowWidth = 2560;
    SettingsEditor editor(initial, t);

    int callsBefore = display.calls;
    editor.restoreDisplayDefaults();

    EXPECT_GT(display.calls, callsBefore);
    // Sink must have received the default window width, not the 2560
    // value the editor started with.
    EXPECT_EQ(display.w, DisplaySettings{}.windowWidth);
}

TEST(SettingsEditor, DirtyTrackingAcrossMutateApplyRevertCycle)
{
    SettingsEditor editor(Settings{}, {});
    EXPECT_FALSE(editor.isDirty());

    editor.mutate([](Settings& p) { p.display.renderScale = 0.8f; });
    EXPECT_TRUE(editor.isDirty());

    TmpDir tmp;
    editor.apply(tmp.path() / "settings.json");
    EXPECT_FALSE(editor.isDirty());

    editor.mutate([](Settings& p) { p.audio.busGains[0] = 0.5f; });
    EXPECT_TRUE(editor.isDirty());

    editor.revert();
    EXPECT_FALSE(editor.isDirty());
}

TEST(SettingsEditor, RestoreControlsResetsInputMapWhenAttached)
{
    InputActionMap map;
    {
        InputAction a;
        a.id        = "Jump";
        a.primary   = InputBinding::key(32);
        map.addAction(a);
    }
    // User remapped the action.
    map.setPrimary("Jump", InputBinding::key(87));
    ASSERT_EQ(map.findAction("Jump")->primary.code, 87);

    SettingsEditor::ApplyTargets t{};
    t.inputMap = &map;
    SettingsEditor editor(Settings{}, t);
    editor.restoreControlsDefaults();

    // The action-registered default was key(32); after restore the
    // map's primary should be back to 32 via resetToDefaults().
    EXPECT_EQ(map.findAction("Jump")->primary.code, 32);
}
