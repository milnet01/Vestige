// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_settings.cpp
/// @brief Phase 10 slice 13.1 — Settings primitive + atomic-write +
///        config-path helper tests. No engine wiring yet; everything
///        here exercises in-memory round-trips, schema migration
///        scaffolding, validation clamps, and on-disk load/save via
///        a tmp-directory fixture.

#include <gtest/gtest.h>

#include "audio/audio_engine.h"
#include "core/settings.h"
#include "core/settings_apply.h"
#include "core/settings_editor.h"
#include "core/settings_migration.h"
#include "accessibility/photosensitive_safety.h"
#include "editor/panels/settings_editor_panel.h"
#include "input/input_bindings.h"
#include "renderer/terrain_renderer.h"   // TerrainGroundQuality (Tier-1 A5)
#include "renderer/foliage_renderer.h"   // FoliageQuality (Tier-1 B3)
#include "renderer/grass_renderer.h"     // GrassQuality (3D_E-0039 G5)
#include "utils/atomic_write.h"
#include "utils/config_path.h"

#include "test_helpers.h"

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
// The per-test uniqueness comes from the shared vestigeTestStamp()
// (<pid>_<test-name>); the attempt counter only disambiguates multiple
// TmpDir instances constructed within a single test.
class TmpDir
{
public:
    TmpDir()
    {
        fs::path base = fs::temp_directory_path();
        for (int attempt = 0; attempt < 100; ++attempt)
        {
            fs::path candidate = base / ("vestige_settings_test_"
                + Testing::vestigeTestStamp() + "_"
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

#ifndef _WIN32
/// RAII guard for a POSIX environment variable. /test-audit 2026-05-17
/// Ts19-I2: the prior ConfigPath tests snapshotted + restored env vars
/// by hand, so an `EXPECT_EQ` failure (which aborts the test body) would
/// leak the test value into every later test that read the same var.
/// EnvGuard restores in its destructor regardless of how scope exits.
class EnvGuard
{
public:
    explicit EnvGuard(const char* name) : m_name(name)
    {
        const char* current = std::getenv(name);
        m_hadValue = (current != nullptr);
        if (m_hadValue) m_savedValue = current;
    }

    ~EnvGuard()
    {
        if (m_hadValue) ::setenv(m_name, m_savedValue.c_str(), 1);
        else            ::unsetenv(m_name);
    }

    EnvGuard(const EnvGuard&) = delete;
    EnvGuard& operator=(const EnvGuard&) = delete;

private:
    const char* m_name;
    std::string m_savedValue;
    bool        m_hadValue = false;
};
#endif

} // namespace

// ===== ConfigPath =============================================

TEST(ConfigPath, LinuxUsesXdgConfigHomeWhenSet)
{
#ifndef _WIN32
    EnvGuard guardXdg("XDG_CONFIG_HOME");

    ::setenv("XDG_CONFIG_HOME", "/tmp/xdg_fake_root", 1);
    fs::path dir = ConfigPath::getConfigDir();
    EXPECT_EQ(dir, fs::path("/tmp/xdg_fake_root/vestige"));
#else
    GTEST_SKIP() << "POSIX-only test";
#endif
}

TEST(ConfigPath, LinuxFallsBackToHomeDotConfig)
{
#ifndef _WIN32
    EnvGuard guardXdg("XDG_CONFIG_HOME");
    EnvGuard guardHome("HOME");

    ::unsetenv("XDG_CONFIG_HOME");
    ::setenv("HOME", "/tmp/fake_home_root", 1);
    fs::path dir = ConfigPath::getConfigDir();
    EXPECT_EQ(dir, fs::path("/tmp/fake_home_root/.config/vestige"));
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
    original.audio.outputLayout = AudioOutputLayout::Surround51;  // AX8
    original.audio.airAbsorptionEnabled = false;                  // AX6
    original.audio.lodEnabled = false;                            // AX5
    original.audio.proceduralAudioEnabled = false;                // AX4 S9
    original.audio.emitUntaggedCollisions = true;                 // AX4 S9
    original.audio.occlusionEnabled = false;                      // AX1
    original.audio.occlusionRayCount = 12;                        // AX1
    original.audio.occlusionMaxDistance = 25.0f;                  // AX1
    original.audio.occlusionSourceRadius = 0.75f;                 // AX1

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
    EXPECT_EQ(s.audio.outputLayout, AudioOutputLayout::Auto);  // AX8 default
    EXPECT_TRUE(s.audio.airAbsorptionEnabled);                 // AX6 default
    EXPECT_TRUE(s.audio.lodEnabled);                           // AX5 default
    EXPECT_TRUE(s.audio.proceduralAudioEnabled);               // AX4 S9 default
    EXPECT_FALSE(s.audio.emitUntaggedCollisions);              // AX4 S9 default
    EXPECT_TRUE(s.audio.occlusionEnabled);                     // AX1 default
    EXPECT_EQ(s.audio.occlusionRayCount, 8);                   // AX1 default
    EXPECT_FLOAT_EQ(s.audio.occlusionMaxDistance, 40.0f);      // AX1 default
    EXPECT_FLOAT_EQ(s.audio.occlusionSourceRadius, 0.5f);      // AX1 default
}

// AX1 — validate() clamps out-of-range occlusion fields (a hand-edited
// settings.json cannot push a bad ray count / negative distance downstream).
TEST(Settings, OcclusionFieldsAreClampedByValidate)
{
    Settings s;
    s.audio.occlusionRayCount    = 99;      // > kMaxOcclusionRayCount(16)
    s.audio.occlusionMaxDistance = -5.0f;   // negative
    s.audio.occlusionSourceRadius = -1.0f;  // negative
    EXPECT_FALSE(validate(s));              // returns false when it had to clamp
    EXPECT_EQ(s.audio.occlusionRayCount, 16);
    EXPECT_FLOAT_EQ(s.audio.occlusionMaxDistance, 0.0f);
    EXPECT_FLOAT_EQ(s.audio.occlusionSourceRadius, 0.0f);

    s.audio.occlusionRayCount = 0;          // < 1
    EXPECT_FALSE(validate(s));
    EXPECT_EQ(s.audio.occlusionRayCount, 1);
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

// L5 verify-step test 19.
TEST(SettingsDisk, PersistsLanguage)
{
    TmpDir tmp;
    fs::path path = tmp.path() / "settings.json";

    Settings original;
    original.localization.language = "he";

    ASSERT_EQ(original.saveAtomic(path), SaveStatus::Ok);

    auto [loaded, status] = Settings::loadFromDisk(path);
    EXPECT_EQ(status, LoadStatus::Ok);
    EXPECT_EQ(loaded.localization.language, "he");
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

    // migrate() runs the full chain to the current schema; the v1 → v2
    // step's effect (the onboarding block) persists through later steps.
    ASSERT_TRUE(migrate(j));
    EXPECT_EQ(j["schemaVersion"].get<int>(), kCurrentSchemaVersion);
    ASSERT_TRUE(j.contains("onboarding"));
    EXPECT_FALSE(j["onboarding"]["hasCompletedFirstRun"].get<bool>());
    EXPECT_EQ(j["onboarding"]["completedAt"].get<std::string>(), "");
    EXPECT_EQ(j["onboarding"]["skipCount"].get<int>(), 0);
}

// L5 verify-step test 18.
TEST(SettingsMigration, V2ToV3PopulatesLanguage)
{
    // Hand-craft a v2-shaped tree: take the current toJson, strip the
    // localization block + pin schemaVersion back to 2.
    json j = Settings{}.toJson();
    j.erase("localization");
    j["schemaVersion"] = 2;

    // migrate() runs the full chain to the current schema; the v2 → v3
    // step's effect (the localization block) persists through later
    // steps, so assert the terminal version rather than the literal 3.
    ASSERT_TRUE(migrate(j));
    EXPECT_EQ(j["schemaVersion"].get<int>(), kCurrentSchemaVersion);
    ASSERT_TRUE(j.contains("localization"));
    EXPECT_EQ(j["localization"]["language"].get<std::string>(), "en");
}

// AX8 + AX6 verify-step: v3 → v4 adds audio.outputLayout ("auto") and
// audio.airAbsorptionEnabled (true) — both current-behaviour defaults,
// so a v3 file is unchanged in effect. (Both AX8 and AX6 ride the one
// v4 bump.)
TEST(SettingsMigration, V3ToV4AddsOutputLayoutDefaultAuto)
{
    // Hand-craft a v3-shaped tree: current toJson, strip the new audio
    // fields + pin schemaVersion back to 3.
    json j = Settings{}.toJson();
    j["audio"].erase("outputLayout");
    j["audio"].erase("airAbsorptionEnabled");
    j["audio"].erase("lodEnabled");
    j["schemaVersion"] = 3;

    ASSERT_TRUE(migrate(j));
    EXPECT_EQ(j["schemaVersion"].get<int>(), kCurrentSchemaVersion);
    ASSERT_TRUE(j["audio"].contains("outputLayout"));
    EXPECT_EQ(j["audio"]["outputLayout"].get<std::string>(), "auto");
    ASSERT_TRUE(j["audio"].contains("airAbsorptionEnabled"));
    EXPECT_TRUE(j["audio"]["airAbsorptionEnabled"].get<bool>());
    ASSERT_TRUE(j["audio"].contains("lodEnabled"));
    EXPECT_TRUE(j["audio"]["lodEnabled"].get<bool>());

    // And it loads back as the current-behaviour defaults (unchanged
    // from pre-v4: Auto layout, air absorption on, LOD on).
    Settings restored;
    ASSERT_TRUE(restored.fromJson(j));
    EXPECT_EQ(restored.audio.outputLayout, AudioOutputLayout::Auto);
    EXPECT_TRUE(restored.audio.airAbsorptionEnabled);
    EXPECT_TRUE(restored.audio.lodEnabled);
}

// AX2 R4 verify-step: v4 → v5 adds the three reverb settings, all at their
// current-behaviour defaults (reverb on, wet cap 0.5, convolution allowed),
// so a v4 file is unchanged in effect.
TEST(SettingsMigration, V4ToV5AddsReverbDefaults)
{
    json j = Settings{}.toJson();
    j["audio"].erase("reverbEnabled");
    j["audio"].erase("reverbWetCap");
    j["audio"].erase("reverbConvolutionEnabled");
    j["schemaVersion"] = 4;

    ASSERT_TRUE(migrate(j));
    EXPECT_EQ(j["schemaVersion"].get<int>(), kCurrentSchemaVersion);
    ASSERT_TRUE(j["audio"].contains("reverbEnabled"));
    EXPECT_TRUE(j["audio"]["reverbEnabled"].get<bool>());
    ASSERT_TRUE(j["audio"].contains("reverbWetCap"));
    EXPECT_NEAR(j["audio"]["reverbWetCap"].get<float>(), 0.5f, 1e-6f);
    ASSERT_TRUE(j["audio"].contains("reverbConvolutionEnabled"));
    EXPECT_TRUE(j["audio"]["reverbConvolutionEnabled"].get<bool>());

    Settings restored;
    ASSERT_TRUE(restored.fromJson(j));
    EXPECT_TRUE(restored.audio.reverbEnabled);
    EXPECT_NEAR(restored.audio.reverbWetCap, 0.5f, 1e-6f);
    EXPECT_TRUE(restored.audio.reverbConvolutionEnabled);
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

// Tier-1 — records the quality-preset renderer pushes.
class RecordingRendererQualitySink final : public RendererQualitySink
{
public:
    AntiAliasMode aa        = AntiAliasMode::MSAA_4X;
    bool          ssao      = false;
    bool          bloom     = false;
    bool          heavyPost = false;
    TerrainGroundQuality ground = TerrainGroundQuality::High;  // sentinel
    FoliageQuality foliage = FoliageQuality::High;             // sentinel
    GrassQuality  grass    = GrassQuality::High;               // sentinel
    int           calls     = 0;

    void setAntiAliasMode(AntiAliasMode m) override { aa = m; ++calls; }
    void setSsaoEnabled(bool e) override            { ssao = e; ++calls; }
    void setBloomEnabled(bool e) override           { bloom = e; ++calls; }
    void setHeavyPostEnabled(bool e) override       { heavyPost = e; ++calls; }
    void setTerrainGroundQuality(TerrainGroundQuality q) override { ground = q; ++calls; }
    void setFoliageQuality(FoliageQuality q) override { foliage = q; ++calls; }
    void setGrassQuality(GrassQuality q) override   { grass = q; ++calls; }
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

class RecordingOutputSink final : public AudioOutputApplySink
{
public:
    AudioOutputLayout layout = AudioOutputLayout::Auto;
    int               calls  = 0;
    void setOutputLayout(AudioOutputLayout l) override { layout = l; ++calls; }
};

class RecordingAirAbsorptionSink final : public AudioAirAbsorptionApplySink
{
public:
    bool enabled = true;
    int  calls   = 0;
    void setAirAbsorptionEnabled(bool e) override { enabled = e; ++calls; }
};

class RecordingLodSink final : public AudioLodApplySink
{
public:
    bool enabled = true;
    int  calls   = 0;
    void setLodEnabled(bool e) override { enabled = e; ++calls; }
};

// AX4 S9 — records the procedural-audio toggle pushes.
class RecordingProceduralAudioSink final : public ProceduralAudioApplySink
{
public:
    bool proceduralEnabled = false;
    bool emitUntagged      = true;
    int  calls             = 0;
    void setProceduralAudioEnabled(bool e) override { proceduralEnabled = e; ++calls; }
    void setEmitUntaggedCollisions(bool e) override { emitUntagged = e; ++calls; }
};

// AX2 R4 — records the reverb setting pushes.
class RecordingReverbSink final : public AudioReverbApplySink
{
public:
    bool  enabled     = false;
    float wetCap      = -1.0f;
    bool  convolution = false;
    int   calls       = 0;
    void setReverbEnabled(bool e) override            { enabled = e; ++calls; }
    void setReverbWetCap(float c) override            { wetCap = c; ++calls; }
    void setReverbConvolutionEnabled(bool e) override { convolution = e; ++calls; }
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
    a.postProcess.volumetricFogEnabled = false;
    a.postProcess.dynamicGiEnabled    = false;  // Slice R4
    a.postProcess.reduceMotionGi      = true;    // Slice R4

    RecordingRendererAccessSink sink;
    applyRendererAccessibility(a, sink);

    EXPECT_FALSE(sink.pp.depthOfFieldEnabled);
    EXPECT_FALSE(sink.pp.motionBlurEnabled);
    EXPECT_TRUE(sink.pp.fogEnabled);
    EXPECT_FLOAT_EQ(sink.pp.fogIntensityScale, 0.3f);
    EXPECT_TRUE(sink.pp.reduceMotionFog);
    EXPECT_FALSE(sink.pp.volumetricFogEnabled);
    EXPECT_FALSE(sink.pp.dynamicGiEnabled);
    EXPECT_TRUE(sink.pp.reduceMotionGi);
}

// ================================================================
// Tier 1 — quality-preset apply (design §4.1 / §6 items 3–4)
// ================================================================

TEST(SettingsApply, QualityPresetLowUsesFxaaHalfScaleAndDropsHeavyPost)
{
    DisplaySettings d;
    d.renderScale = 999.0f;  // sentinel: must be overwritten
    RecordingRendererQualitySink sink;
    applyQualityPreset(QualityPreset::Low, d, sink);

    EXPECT_FLOAT_EQ(d.renderScale, 0.66f);
    EXPECT_EQ(sink.aa, AntiAliasMode::FXAA);
    EXPECT_FALSE(sink.ssao);
    EXPECT_FALSE(sink.bloom);
    EXPECT_FALSE(sink.heavyPost);
    EXPECT_EQ(sink.ground, TerrainGroundQuality::Low);   // A5: cheapest terrain path
    EXPECT_EQ(sink.foliage, FoliageQuality::Low);        // B3: short grass distance, no shadows
    EXPECT_EQ(sink.grass, GrassQuality::Low);            // G5: short GPU-grass draw distance
    EXPECT_EQ(sink.calls, 7);                            // 4 toggles + terrain + foliage + grass tier
}

TEST(SettingsApply, QualityPresetMediumKeepsSsaoBloomButStillFxaaAndNoHeavyPost)
{
    DisplaySettings d;
    RecordingRendererQualitySink sink;
    applyQualityPreset(QualityPreset::Medium, d, sink);

    EXPECT_FLOAT_EQ(d.renderScale, 0.75f);
    EXPECT_EQ(sink.aa, AntiAliasMode::FXAA);
    EXPECT_TRUE(sink.ssao);
    EXPECT_TRUE(sink.bloom);
    EXPECT_FALSE(sink.heavyPost);
    EXPECT_EQ(sink.ground, TerrainGroundQuality::Medium);  // A5: drops distance-tiling
    EXPECT_EQ(sink.foliage, FoliageQuality::Medium);       // B3: mid grass distance
    EXPECT_EQ(sink.grass, GrassQuality::Medium);           // G5: mid GPU-grass draw distance
}

TEST(SettingsApply, QualityPresetHighAndUltraRenderIdenticallyInWave1)
{
    for (QualityPreset p : {QualityPreset::High, QualityPreset::Ultra})
    {
        DisplaySettings d;
        RecordingRendererQualitySink sink;
        applyQualityPreset(p, d, sink);

        EXPECT_FLOAT_EQ(d.renderScale, 1.0f) << "preset " << qualityPresetLabel(p);
        EXPECT_EQ(sink.aa, AntiAliasMode::TAA) << "preset " << qualityPresetLabel(p);
        EXPECT_TRUE(sink.ssao);
        EXPECT_TRUE(sink.bloom);
        EXPECT_TRUE(sink.heavyPost);
        EXPECT_EQ(sink.ground, TerrainGroundQuality::High)  // A5: all terrain features
            << "preset " << qualityPresetLabel(p);
        EXPECT_EQ(sink.foliage, FoliageQuality::High)       // B3: full grass distance + shadows
            << "preset " << qualityPresetLabel(p);
        EXPECT_EQ(sink.grass, GrassQuality::High)           // G5: full GPU-grass draw distance
            << "preset " << qualityPresetLabel(p);
    }
}

TEST(SettingsApply, QualityPresetCustomAppliesNothing)
{
    DisplaySettings d;
    d.renderScale = 0.42f;  // a hand-tuned value that must survive
    RecordingRendererQualitySink sink;
    applyQualityPreset(QualityPreset::Custom, d, sink);

    EXPECT_FLOAT_EQ(d.renderScale, 0.42f);  // untouched
    EXPECT_EQ(sink.calls, 0);               // no renderer pushes
}

// INV-A11Y (design §4.3 / §6 item 4): a preset whose quality side wants
// the heavy passes ON (Ultra) can never re-enable a pass that accessibility
// turned OFF. The two apply functions write to disjoint sinks — the preset
// only reaches `heavyPost`, never the accessibility fog/GI fields — and the
// renderer's runtime gate is the AND of both. Model that AND here to pin
// that Ultra's quality=true stays gated off when accessibility=false.
TEST(SettingsApply, QualityPresetCannotReEnableAccessibilityDisabledHeavyPost)
{
    // Quality side: Ultra wants fog + GI on.
    DisplaySettings d;
    RecordingRendererQualitySink qualitySink;
    applyQualityPreset(QualityPreset::Ultra, d, qualitySink);
    EXPECT_TRUE(qualitySink.heavyPost);

    // Accessibility side: user disabled volumetric fog + dynamic GI.
    AccessibilitySettings a;
    a.postProcess.volumetricFogEnabled = false;
    a.postProcess.dynamicGiEnabled     = false;
    RecordingRendererAccessSink accessSink;
    applyRendererAccessibility(a, accessSink);
    EXPECT_FALSE(accessSink.pp.volumetricFogEnabled);
    EXPECT_FALSE(accessSink.pp.dynamicGiEnabled);

    // Effective runtime gate = accessibility AND quality (renderer.cpp).
    // Ultra's quality=true must NOT override accessibility=false.
    const bool volumetricActive =
        accessSink.pp.volumetricFogEnabled && qualitySink.heavyPost;
    const bool giActive =
        accessSink.pp.dynamicGiEnabled && qualitySink.heavyPost;
    EXPECT_FALSE(volumetricActive);
    EXPECT_FALSE(giActive);
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

// AX8 — applyAudioOutput forwards the persisted layout verbatim to the
// sink (the HRTF-precedence decision lives in resolveOutputMode at the
// engine boundary, not in the apply forwarder).
TEST(SettingsApply, OutputLayoutForwardedVerbatim)
{
    const AudioOutputLayout all[] = {
        AudioOutputLayout::Auto, AudioOutputLayout::Mono, AudioOutputLayout::Stereo,
        AudioOutputLayout::Surround51, AudioOutputLayout::Surround71,
    };
    for (AudioOutputLayout layout : all)
    {
        AudioSettings a;
        a.outputLayout = layout;
        RecordingOutputSink sink;
        applyAudioOutput(a, sink);
        EXPECT_EQ(sink.layout, layout);
        EXPECT_EQ(sink.calls, 1);
    }
}

// AX6 — applyAudioAirAbsorption forwards the persisted toggle verbatim.
TEST(SettingsApply, AirAbsorptionForwardedVerbatim)
{
    for (bool enabled : {true, false})
    {
        AudioSettings a;
        a.airAbsorptionEnabled = enabled;
        RecordingAirAbsorptionSink sink;
        applyAudioAirAbsorption(a, sink);
        EXPECT_EQ(sink.enabled, enabled);
        EXPECT_EQ(sink.calls, 1);
    }
}

// AX5 — applyAudioLod forwards the persisted toggle verbatim.
TEST(SettingsApply, AudioLodForwardedVerbatim)
{
    for (bool enabled : {true, false})
    {
        AudioSettings a;
        a.lodEnabled = enabled;
        RecordingLodSink sink;
        applyAudioLod(a, sink);
        EXPECT_EQ(sink.enabled, enabled);
        EXPECT_EQ(sink.calls, 1);
    }
}

// AX4 S9 — applyProceduralAudio forwards both toggles verbatim.
TEST(SettingsApply, ProceduralAudioForwardedVerbatim)
{
    for (bool proc : {true, false})
    {
        for (bool untagged : {true, false})
        {
            AudioSettings a;
            a.proceduralAudioEnabled = proc;
            a.emitUntaggedCollisions = untagged;
            RecordingProceduralAudioSink sink;
            applyProceduralAudio(a, sink);
            EXPECT_EQ(sink.proceduralEnabled, proc);
            EXPECT_EQ(sink.emitUntagged, untagged);
            EXPECT_EQ(sink.calls, 2);
        }
    }
}

// AX2 R4 — applyAudioReverb forwards all three settings verbatim.
TEST(SettingsApply, ReverbForwardedVerbatim)
{
    AudioSettings a;
    a.reverbEnabled            = false;
    a.reverbWetCap             = 0.33f;
    a.reverbConvolutionEnabled = false;
    RecordingReverbSink sink;
    applyAudioReverb(a, sink);
    EXPECT_FALSE(sink.enabled);
    EXPECT_NEAR(sink.wetCap, 0.33f, 1e-6f);
    EXPECT_FALSE(sink.convolution);
    EXPECT_EQ(sink.calls, 3);
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

// Phase 10.9 Slice 2 P5 — sink must forward to the queue, not only
// its own m_enabled. Shipping code stored a local flag that nothing
// read; this test pins the fix so the toggle reaches the renderer
// through the queue's consumer-visible view.
TEST(SettingsApply, SubtitleQueueApplySinkForwardsEnabledToQueue_P5)
{
    SubtitleQueue q;
    SubtitleQueueApplySink sink(q);
    EXPECT_TRUE(q.isEnabled());

    sink.setSubtitlesEnabled(false);
    EXPECT_FALSE(q.isEnabled())
        << "sink.setSubtitlesEnabled(false) must reach the live queue — "
           "otherwise activeSubtitles() stays non-empty and the renderer "
           "keeps drawing captions the user toggled off.";

    sink.setSubtitlesEnabled(true);
    EXPECT_TRUE(q.isEnabled());
}

// ===== Slice 13.5e — Photosensitive store sink + HRTF sink ==================

TEST(SettingsApply, PhotosensitiveStoreApplySinkWritesEnabledAndLimits)
{
    bool                 enabled = false;
    PhotosensitiveLimits limits{};
    PhotosensitiveStoreApplySink sink(&enabled, &limits);

    PhotosensitiveLimits newLimits;
    newLimits.maxFlashAlpha       = 0.1f;
    newLimits.shakeAmplitudeScale = 0.2f;
    newLimits.maxStrobeHz         = 1.5f;
    newLimits.bloomIntensityScale = 0.4f;

    sink.setPhotosensitiveEnabled(true);
    sink.setPhotosensitiveLimits(newLimits);

    EXPECT_TRUE(enabled);
    EXPECT_FLOAT_EQ(limits.maxFlashAlpha,       0.1f);
    EXPECT_FLOAT_EQ(limits.shakeAmplitudeScale, 0.2f);
    EXPECT_FLOAT_EQ(limits.maxStrobeHz,         1.5f);
    EXPECT_FLOAT_EQ(limits.bloomIntensityScale, 0.4f);
}

TEST(SettingsApply, PhotosensitiveStoreApplySinkTolerantOfNullPointers)
{
    // Defensive: a sink built with null pointers must not crash. Tests
    // construct this path when they want to verify the orchestrator
    // calls the sink but don't care about the downstream writes.
    PhotosensitiveStoreApplySink sink(nullptr, nullptr);
    sink.setPhotosensitiveEnabled(true);

    PhotosensitiveLimits any;
    sink.setPhotosensitiveLimits(any);
    SUCCEED();
}

TEST(SettingsApply, PhotosensitiveStoreApplySinkRoundTripsFromSettings)
{
    // End-to-end: load a Settings, push through the store sink, verify
    // both flag + all four caps populated the engine-side state.
    Settings s;
    s.accessibility.photosensitiveSafety.enabled             = true;
    s.accessibility.photosensitiveSafety.maxFlashAlpha       = 0.15f;
    s.accessibility.photosensitiveSafety.shakeAmplitudeScale = 0.30f;
    s.accessibility.photosensitiveSafety.maxStrobeHz         = 2.5f;
    s.accessibility.photosensitiveSafety.bloomIntensityScale = 0.55f;

    bool                 enabled = false;
    PhotosensitiveLimits limits{};
    PhotosensitiveStoreApplySink sink(&enabled, &limits);

    applyPhotosensitiveSafety(s.accessibility, sink);

    EXPECT_TRUE(enabled);
    EXPECT_FLOAT_EQ(limits.maxFlashAlpha,       0.15f);
    EXPECT_FLOAT_EQ(limits.shakeAmplitudeScale, 0.30f);
    EXPECT_FLOAT_EQ(limits.maxStrobeHz,         2.5f);
    EXPECT_FLOAT_EQ(limits.bloomIntensityScale, 0.55f);
}

TEST(SettingsApply, AudioEngineHrtfApplySinkForwardsMode)
{
    // AudioEngine default-constructs in a safe not-yet-initialized
    // state; setHrtfMode updates the stored mode and bails before
    // touching ALC if !m_available, so this test runs headless.
    AudioEngine engine;
    AudioEngineHrtfApplySink sink(engine);

    sink.setHrtfMode(HrtfMode::Forced);
    EXPECT_EQ(engine.getHrtfSettings().mode, HrtfMode::Forced);

    sink.setHrtfMode(HrtfMode::Disabled);
    EXPECT_EQ(engine.getHrtfSettings().mode, HrtfMode::Disabled);
}

TEST(SettingsApply, AudioHrtfApplyPicksAutoWhenEnabled)
{
    // applyAudioHrtf translates the bool Settings flag to HrtfMode:
    // true → Auto (driver heuristic), false → Disabled.
    AudioEngine engine;
    AudioEngineHrtfApplySink sink(engine);

    AudioSettings audio;
    audio.hrtfEnabled = true;
    applyAudioHrtf(audio, sink);
    EXPECT_EQ(engine.getHrtfSettings().mode, HrtfMode::Auto);

    audio.hrtfEnabled = false;
    applyAudioHrtf(audio, sink);
    EXPECT_EQ(engine.getHrtfSettings().mode, HrtfMode::Disabled);
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
    map.addAction(makeAction("Jump", InputBinding::scancode(32)));
    map.addAction(makeAction("Fire", InputBinding::mouse(0)));
    map.addAction(makeAction("Pause", InputBinding::scancode(256)));

    auto wires = extractInputBindings(map);
    ASSERT_EQ(wires.size(), 3u);
    EXPECT_EQ(wires[0].id, "Jump");
    EXPECT_EQ(wires[1].id, "Fire");
    EXPECT_EQ(wires[2].id, "Pause");
}

TEST(SettingsApplyInputBindings, ExtractRoundTripsDeviceStrings)
{
    InputActionMap map;
    map.addAction(makeAction("K",  InputBinding::scancode(65)));
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
        InputBinding::scancode(69),
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
    map.addAction(makeAction("Jump", InputBinding::scancode(32)));

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
    map.addAction(makeAction("Jump", InputBinding::scancode(32)));

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
    map.addAction(makeAction("Jump", InputBinding::scancode(32)));
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
    map.addAction(makeAction("Jump", InputBinding::scancode(32)));

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
    map.addAction(makeAction("Jump", InputBinding::scancode(32)));

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
        InputBinding::scancode(32),
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
        a.primary   = InputBinding::scancode(32);
        map.addAction(a);
    }
    // User remapped the action.
    map.setPrimary("Jump", InputBinding::scancode(87));
    ASSERT_EQ(map.findAction("Jump")->primary.code, 87);

    SettingsEditor::ApplyTargets t{};
    t.inputMap = &map;
    SettingsEditor editor(Settings{}, t);
    editor.restoreControlsDefaults();

    // The action-registered default was key(32); after restore the
    // map's primary should be back to 32 via resetToDefaults().
    EXPECT_EQ(map.findAction("Jump")->primary.code, 32);
}

// ===== Phase 10.9 Slice 1 F8 — mutate() validate-before-push ================
//
// Contract: SettingsEditor::mutate() must run the same clamp / validation
// policy as Settings::fromJson() before pushing the pending state through
// the apply sinks. The runtime-UI slider path (ImGui sliders wrapped in
// m_editor->mutate(...)) is otherwise a hole that lets out-of-range values
// reach subsystems — e.g. an AudioMixer bus gain above 1.0 (clipping),
// a renderScale above 2.0 (unbounded GPU cost), or a strobe Hz above the
// 30 Hz persistence cap.
//
// These red tests plant out-of-range values through mutate() and assert
// that pending() reflects the clamped value, NOT the raw slider value.
// Pre-F8 they fail because mutate() writes straight into m_pending and
// then pushes unchecked.

TEST(SettingsEditor, MutateClampsAudioBusGainBeforePushingToSink_F8)
{
    Record13_5Audio sink;
    SettingsEditor::ApplyTargets t{};
    t.audio = &sink;

    SettingsEditor editor(Settings{}, t);
    // Slider-rate mutation writes an out-of-range gain — the
    // validate() policy clamps busGains to [0.0, 1.0]. Pre-F8
    // mutate() skips validate() and the 2.5 reaches the mixer.
    editor.mutate([](Settings& p) { p.audio.busGains[0] = 2.5f; });

    EXPECT_FLOAT_EQ(editor.pending().audio.busGains[0], 1.0f)
        << "mutate() must clamp out-of-range bus gain before pushing "
           "to sink (CLAUDE.md Rule 10 — no workarounds, fix the "
           "validation bypass)";
    EXPECT_FLOAT_EQ(sink.lastGains[0], 1.0f)
        << "AudioMixer received raw 2.5 gain — would clip every sample";
}

TEST(SettingsEditor, MutateClampsRenderScaleBeforePushingToSink_F8)
{
    SettingsEditor editor(Settings{}, {});
    // Slider could in theory drive renderScale above the 2.0 cap
    // (the live SettingsEditorPanel slider tops out at 2.0 but a
    // future panel or keybind-bound mutator could overshoot).
    editor.mutate([](Settings& p) { p.display.renderScale = 3.5f; });

    EXPECT_FLOAT_EQ(editor.pending().display.renderScale, 2.0f)
        << "mutate() must clamp renderScale to [0.25, 2.0] — validate() "
           "policy from settings.cpp";
}

TEST(SettingsEditor, MutateClampsStrobeHzBeforePushingToSink_F8)
{
    SettingsEditor editor(Settings{}, {});
    // A malformed mutator (or future slider bound wider than 30 Hz)
    // must not reach the photosensitive-safety subsystem as-is.
    editor.mutate([](Settings& p)
    {
        p.accessibility.photosensitiveSafety.maxStrobeHz = 120.0f;
    });

    EXPECT_FLOAT_EQ(editor.pending().accessibility.photosensitiveSafety.maxStrobeHz,
                    30.0f)
        << "mutate() must clamp maxStrobeHz to [0, 30] Hz (validate() "
           "policy; runtime further caps to 3 Hz via clampStrobeHz when "
           "photosensitive safety is enabled)";
}

// =============================================================================
// Phase 10.9 Slice 1 F11 — "Max strobe Hz" slider honesty
// =============================================================================
//
// Context: the Accessibility tab's "Max strobe Hz" slider is only shown when
// photosensitiveSafety.enabled is true (settings_editor_panel.cpp:503).
// In that mode, PhotosensitiveSafety::clampStrobeHz() runtime-caps any
// effective output at min(limits.maxStrobeHz, WCAG_MAX_STROBE_HZ == 3.0).
// Shipping code sets the slider range to [0, 10] Hz, so a partially-sighted
// user can authoritatively drag the slider to 7.0, watch the settings file
// persist 7.0, and never realise the subsystem silently discarded
// everything above 3.0. The UI lies.
//
// F11 contract: the slider max for the in-safe-mode strobe control must
// equal the WCAG 2.2 SC 2.3.1 ceiling, so the slider can never promise
// a value the runtime does not honour. The constant lives on
// `SettingsEditorPanel::SAFE_MODE_STROBE_HZ_SLIDER_MAX` so the .cpp
// slider bound and this test share one source of truth.

TEST(SettingsEditorPanel, SafeModeStrobeSliderMaxEqualsWcagCeiling_F11)
{
    EXPECT_FLOAT_EQ(SettingsEditorPanel::SAFE_MODE_STROBE_HZ_SLIDER_MAX,
                    WCAG_MAX_STROBE_HZ)
        << "Safe-mode strobe slider max must track WCAG_MAX_STROBE_HZ — "
           "anything higher lets the slider persist values that "
           "PhotosensitiveSafety::clampStrobeHz silently discards.";
}

TEST(SettingsEditorPanel, SafeModeStrobeSliderMaxIs3Hz_F11)
{
    // Belt-and-braces pin: a future refactor that re-bases
    // WCAG_MAX_STROBE_HZ to something other than 3.0 should surface
    // in this test so the UI contract is re-reviewed, not silently
    // re-aligned.
    EXPECT_FLOAT_EQ(SettingsEditorPanel::SAFE_MODE_STROBE_HZ_SLIDER_MAX, 3.0f)
        << "WCAG 2.2 SC 2.3.1 ('Three Flashes or Below Threshold') caps "
           "general-flash frequency at 3 Hz. If the slider bound no longer "
           "matches this, re-check both PhotosensitiveSafety::clampStrobeHz "
           "and the spec.";
}
