// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file settings.cpp
/// @brief Phase 10 — persistent, user-editable engine settings.
#include "core/settings.h"

#include "core/logger.h"
#include "core/settings_migration.h"
#include "utils/atomic_write.h"
#include "utils/config_path.h"
#include "utils/json_size_cap.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <system_error>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace Vestige
{

// ====== QualityPreset helpers =================================

const char* qualityPresetLabel(QualityPreset q)
{
    switch (q)
    {
        case QualityPreset::Low:    return "Low";
        case QualityPreset::Medium: return "Medium";
        case QualityPreset::High:   return "High";
        case QualityPreset::Ultra:  return "Ultra";
        case QualityPreset::Custom: return "Custom";
    }
    return "Unknown";
}

QualityPreset qualityPresetFromString(const std::string& s, QualityPreset fallback)
{
    if (s == "low")    return QualityPreset::Low;
    if (s == "medium") return QualityPreset::Medium;
    if (s == "high")   return QualityPreset::High;
    if (s == "ultra")  return QualityPreset::Ultra;
    if (s == "custom") return QualityPreset::Custom;
    return fallback;
}

std::string qualityPresetToString(QualityPreset q)
{
    switch (q)
    {
        case QualityPreset::Low:    return "low";
        case QualityPreset::Medium: return "medium";
        case QualityPreset::High:   return "high";
        case QualityPreset::Ultra:  return "ultra";
        case QualityPreset::Custom: return "custom";
    }
    return "medium";
}

// ====== Section equality operators ============================

bool DisplaySettings::operator==(const DisplaySettings& o) const
{
    return windowWidth   == o.windowWidth
        && windowHeight  == o.windowHeight
        && fullscreen    == o.fullscreen
        && vsync         == o.vsync
        && qualityPreset == o.qualityPreset
        && renderScale   == o.renderScale;
}

bool AudioSettings::operator==(const AudioSettings& o) const
{
    return busGains == o.busGains && hrtfEnabled == o.hrtfEnabled;
}

bool ControlsSettings::operator==(const ControlsSettings& o) const
{
    return mouseSensitivity     == o.mouseSensitivity
        && invertY              == o.invertY
        && gamepadDeadzoneLeft  == o.gamepadDeadzoneLeft
        && gamepadDeadzoneRight == o.gamepadDeadzoneRight
        && bindings             == o.bindings;
}

bool PostProcessAccessibilityWire::operator==(const PostProcessAccessibilityWire& o) const
{
    return depthOfFieldEnabled == o.depthOfFieldEnabled
        && motionBlurEnabled   == o.motionBlurEnabled
        && fogEnabled          == o.fogEnabled
        && fogIntensityScale   == o.fogIntensityScale
        && reduceMotionFog     == o.reduceMotionFog;
}

bool PhotosensitiveSafetyWire::operator==(const PhotosensitiveSafetyWire& o) const
{
    return enabled             == o.enabled
        && maxFlashAlpha       == o.maxFlashAlpha
        && shakeAmplitudeScale == o.shakeAmplitudeScale
        && maxStrobeHz         == o.maxStrobeHz
        && bloomIntensityScale == o.bloomIntensityScale;
}

bool AccessibilitySettings::operator==(const AccessibilitySettings& o) const
{
    return uiScalePreset        == o.uiScalePreset
        && highContrast         == o.highContrast
        && reducedMotion        == o.reducedMotion
        && subtitlesEnabled     == o.subtitlesEnabled
        && subtitleSize         == o.subtitleSize
        && colorVisionFilter    == o.colorVisionFilter
        && photosensitiveSafety == o.photosensitiveSafety
        && postProcess          == o.postProcess;
}

// ====== GameplaySettings pimpl ================================

struct GameplaySettings::Impl
{
    json values = json::object();
};

GameplaySettings::GameplaySettings()
    : m_impl(std::make_unique<Impl>())
{
}

GameplaySettings::GameplaySettings(const GameplaySettings& other)
    : m_impl(std::make_unique<Impl>(*other.m_impl))
{
}

GameplaySettings::GameplaySettings(GameplaySettings&&) noexcept = default;

GameplaySettings& GameplaySettings::operator=(const GameplaySettings& other)
{
    if (this != &other)
    {
        m_impl = std::make_unique<Impl>(*other.m_impl);
    }
    return *this;
}

GameplaySettings& GameplaySettings::operator=(GameplaySettings&&) noexcept = default;

GameplaySettings::~GameplaySettings() = default;

json& GameplaySettings::values()
{
    return m_impl->values;
}

const json& GameplaySettings::values() const
{
    return m_impl->values;
}

bool GameplaySettings::operator==(const GameplaySettings& o) const
{
    return m_impl->values == o.m_impl->values;
}

bool OnboardingSettings::operator==(const OnboardingSettings& o) const
{
    return hasCompletedFirstRun == o.hasCompletedFirstRun
        && completedAt          == o.completedAt
        && skipCount            == o.skipCount;
}

// ====== Settings root equality ================================

bool Settings::operator==(const Settings& o) const
{
    return schemaVersion == o.schemaVersion
        && display       == o.display
        && audio         == o.audio
        && controls      == o.controls
        && gameplay      == o.gameplay
        && accessibility == o.accessibility
        && onboarding    == o.onboarding;
}

// ====== JSON serialisation ====================================

namespace
{

// --- Display ---

json displayToJson(const DisplaySettings& d)
{
    return json{
        {"windowWidth",   d.windowWidth},
        {"windowHeight",  d.windowHeight},
        {"fullscreen",    d.fullscreen},
        {"vsync",         d.vsync},
        {"qualityPreset", qualityPresetToString(d.qualityPreset)},
        {"renderScale",   d.renderScale},
    };
}

void displayFromJson(const json& j, DisplaySettings& d)
{
    // Read raw values. Validation (clamping) happens in validate().
    d.windowWidth   = j.value("windowWidth",   d.windowWidth);
    d.windowHeight  = j.value("windowHeight",  d.windowHeight);
    d.fullscreen    = j.value("fullscreen",    d.fullscreen);
    d.vsync         = j.value("vsync",         d.vsync);
    d.qualityPreset = qualityPresetFromString(
        j.value("qualityPreset", qualityPresetToString(d.qualityPreset)),
        d.qualityPreset);
    d.renderScale   = j.value("renderScale", d.renderScale);
}

// --- Audio ---

json audioToJson(const AudioSettings& a)
{
    json busGains = json::object();
    busGains["master"]  = a.busGains[0];
    busGains["music"]   = a.busGains[1];
    busGains["voice"]   = a.busGains[2];
    busGains["sfx"]     = a.busGains[3];
    busGains["ambient"] = a.busGains[4];
    busGains["ui"]      = a.busGains[5];
    return json{
        {"busGains",    busGains},
        {"hrtfEnabled", a.hrtfEnabled},
    };
}

void audioFromJson(const json& j, AudioSettings& a)
{
    if (j.contains("busGains") && j["busGains"].is_object())
    {
        const json& g = j["busGains"];
        a.busGains[0] = g.value("master",  a.busGains[0]);
        a.busGains[1] = g.value("music",   a.busGains[1]);
        a.busGains[2] = g.value("voice",   a.busGains[2]);
        a.busGains[3] = g.value("sfx",     a.busGains[3]);
        a.busGains[4] = g.value("ambient", a.busGains[4]);
        a.busGains[5] = g.value("ui",      a.busGains[5]);
    }
    a.hrtfEnabled = j.value("hrtfEnabled", a.hrtfEnabled);
}

// --- Controls ---

json bindingToJson(const InputBindingWire& b)
{
    return json{
        {"device",   b.device},
        {"scancode", b.scancode},
    };
}

InputBindingWire bindingFromJson(const json& j)
{
    InputBindingWire b;
    b.device   = j.value("device", std::string("none"));
    b.scancode = j.value("scancode", -1);
    if (b.device == "none")
    {
        b.scancode = -1;
    }
    return b;
}

json controlsToJson(const ControlsSettings& c)
{
    json bindings = json::array();
    for (const auto& ab : c.bindings)
    {
        bindings.push_back(json{
            {"id",        ab.id},
            {"primary",   bindingToJson(ab.primary)},
            {"secondary", bindingToJson(ab.secondary)},
            {"gamepad",   bindingToJson(ab.gamepad)},
        });
    }
    return json{
        {"mouseSensitivity",     c.mouseSensitivity},
        {"invertY",              c.invertY},
        {"gamepadDeadzoneLeft",  c.gamepadDeadzoneLeft},
        {"gamepadDeadzoneRight", c.gamepadDeadzoneRight},
        {"bindings",             bindings},
    };
}

void controlsFromJson(const json& j, ControlsSettings& c)
{
    c.mouseSensitivity     = j.value("mouseSensitivity",     c.mouseSensitivity);
    c.invertY              = j.value("invertY",              c.invertY);
    c.gamepadDeadzoneLeft  = j.value("gamepadDeadzoneLeft",  c.gamepadDeadzoneLeft);
    c.gamepadDeadzoneRight = j.value("gamepadDeadzoneRight", c.gamepadDeadzoneRight);

    c.bindings.clear();
    if (j.contains("bindings") && j["bindings"].is_array())
    {
        for (const auto& entry : j["bindings"])
        {
            if (!entry.is_object() || !entry.contains("id")
                || !entry["id"].is_string())
            {
                continue;
            }
            ActionBindingWire ab;
            ab.id = entry["id"].get<std::string>();
            if (entry.contains("primary")   && entry["primary"].is_object())
            {
                ab.primary = bindingFromJson(entry["primary"]);
            }
            if (entry.contains("secondary") && entry["secondary"].is_object())
            {
                ab.secondary = bindingFromJson(entry["secondary"]);
            }
            if (entry.contains("gamepad")   && entry["gamepad"].is_object())
            {
                ab.gamepad = bindingFromJson(entry["gamepad"]);
            }
            c.bindings.push_back(std::move(ab));
        }
    }
}

// --- Accessibility ---

json postProcessAccessToJson(const PostProcessAccessibilityWire& p)
{
    return json{
        {"depthOfFieldEnabled", p.depthOfFieldEnabled},
        {"motionBlurEnabled",   p.motionBlurEnabled},
        {"fogEnabled",          p.fogEnabled},
        {"fogIntensityScale",   p.fogIntensityScale},
        {"reduceMotionFog",     p.reduceMotionFog},
    };
}

void postProcessAccessFromJson(const json& j, PostProcessAccessibilityWire& p)
{
    p.depthOfFieldEnabled = j.value("depthOfFieldEnabled", p.depthOfFieldEnabled);
    p.motionBlurEnabled   = j.value("motionBlurEnabled",   p.motionBlurEnabled);
    p.fogEnabled          = j.value("fogEnabled",          p.fogEnabled);
    p.fogIntensityScale   = j.value("fogIntensityScale",   p.fogIntensityScale);
    p.reduceMotionFog     = j.value("reduceMotionFog",     p.reduceMotionFog);
}

json photosensitiveToJson(const PhotosensitiveSafetyWire& p)
{
    return json{
        {"enabled",             p.enabled},
        {"maxFlashAlpha",       p.maxFlashAlpha},
        {"shakeAmplitudeScale", p.shakeAmplitudeScale},
        {"maxStrobeHz",         p.maxStrobeHz},
        {"bloomIntensityScale", p.bloomIntensityScale},
    };
}

void photosensitiveFromJson(const json& j, PhotosensitiveSafetyWire& p)
{
    p.enabled             = j.value("enabled",             p.enabled);
    p.maxFlashAlpha       = j.value("maxFlashAlpha",       p.maxFlashAlpha);
    p.shakeAmplitudeScale = j.value("shakeAmplitudeScale", p.shakeAmplitudeScale);
    p.maxStrobeHz         = j.value("maxStrobeHz",         p.maxStrobeHz);
    p.bloomIntensityScale = j.value("bloomIntensityScale", p.bloomIntensityScale);
}

json accessibilityToJson(const AccessibilitySettings& a)
{
    return json{
        {"uiScalePreset",        a.uiScalePreset},
        {"highContrast",         a.highContrast},
        {"reducedMotion",        a.reducedMotion},
        {"subtitlesEnabled",     a.subtitlesEnabled},
        {"subtitleSize",         a.subtitleSize},
        {"colorVisionFilter",    a.colorVisionFilter},
        {"photosensitiveSafety", photosensitiveToJson(a.photosensitiveSafety)},
        {"postProcess",          postProcessAccessToJson(a.postProcess)},
    };
}

void accessibilityFromJson(const json& j, AccessibilitySettings& a)
{
    a.uiScalePreset     = j.value("uiScalePreset",     a.uiScalePreset);
    a.highContrast      = j.value("highContrast",      a.highContrast);
    a.reducedMotion     = j.value("reducedMotion",     a.reducedMotion);
    a.subtitlesEnabled  = j.value("subtitlesEnabled",  a.subtitlesEnabled);
    a.subtitleSize      = j.value("subtitleSize",      a.subtitleSize);
    a.colorVisionFilter = j.value("colorVisionFilter", a.colorVisionFilter);

    if (j.contains("photosensitiveSafety") && j["photosensitiveSafety"].is_object())
    {
        photosensitiveFromJson(j["photosensitiveSafety"], a.photosensitiveSafety);
    }
    if (j.contains("postProcess") && j["postProcess"].is_object())
    {
        postProcessAccessFromJson(j["postProcess"], a.postProcess);
    }
}

// --- Onboarding ---

json onboardingToJson(const OnboardingSettings& o)
{
    return json{
        {"hasCompletedFirstRun", o.hasCompletedFirstRun},
        {"completedAt",          o.completedAt},
        {"skipCount",            o.skipCount},
    };
}

void onboardingFromJson(const json& j, OnboardingSettings& o)
{
    o.hasCompletedFirstRun = j.value("hasCompletedFirstRun", o.hasCompletedFirstRun);
    o.completedAt          = j.value("completedAt",          o.completedAt);
    o.skipCount            = j.value("skipCount",            o.skipCount);
}

// --- Validation ---

float clamp01(float v)          { return std::clamp(v, 0.0f, 1.0f); }

bool isValidScalePreset(const std::string& s)
{
    return s == "1.0x" || s == "1.25x" || s == "1.5x" || s == "2.0x";
}

bool isValidSubtitleSize(const std::string& s)
{
    return s == "small" || s == "medium" || s == "large" || s == "xl";
}

bool isValidColorVisionFilter(const std::string& s)
{
    return s == "none" || s == "protanopia" || s == "deuteranopia" || s == "tritanopia";
}

// Clamps / normalises any section fields that can come back
// out-of-range. Returns whether any clamping was applied so
// tests + logs can tell the caller the file had bad data.
bool validate(Settings& s)
{
    bool clamped = false;

    // --- Display ---

    // Reject non-positive resolutions (zero or negative). Don't try
    // to match against glfwGetVideoModes here — that needs a GL
    // context and belongs to slice 13.2. Bogus values fall back to
    // a safe 1280x720.
    if (s.display.windowWidth <= 0 || s.display.windowHeight <= 0)
    {
        s.display.windowWidth  = 1280;
        s.display.windowHeight = 720;
        clamped = true;
    }

    const float rsOriginal = s.display.renderScale;
    s.display.renderScale = std::clamp(s.display.renderScale, 0.25f, 2.0f);
    if (s.display.renderScale != rsOriginal)
    {
        clamped = true;
    }

    // --- Audio ---

    for (auto& g : s.audio.busGains)
    {
        const float orig = g;
        g = clamp01(g);
        if (g != orig)
        {
            clamped = true;
        }
    }

    // --- Controls ---

    const float msOrig = s.controls.mouseSensitivity;
    s.controls.mouseSensitivity = std::clamp(s.controls.mouseSensitivity, 0.1f, 10.0f);
    if (s.controls.mouseSensitivity != msOrig)
    {
        clamped = true;
    }

    const float dzlOrig = s.controls.gamepadDeadzoneLeft;
    s.controls.gamepadDeadzoneLeft = std::clamp(s.controls.gamepadDeadzoneLeft, 0.0f, 0.9f);
    if (s.controls.gamepadDeadzoneLeft != dzlOrig)
    {
        clamped = true;
    }

    const float dzrOrig = s.controls.gamepadDeadzoneRight;
    s.controls.gamepadDeadzoneRight = std::clamp(s.controls.gamepadDeadzoneRight, 0.0f, 0.9f);
    if (s.controls.gamepadDeadzoneRight != dzrOrig)
    {
        clamped = true;
    }

    // --- Accessibility ---

    if (!isValidScalePreset(s.accessibility.uiScalePreset))
    {
        Logger::warning("Settings: unknown uiScalePreset '"
                        + s.accessibility.uiScalePreset + "' — falling back to 1.0x.");
        s.accessibility.uiScalePreset = "1.0x";
        clamped = true;
    }

    if (!isValidSubtitleSize(s.accessibility.subtitleSize))
    {
        Logger::warning("Settings: unknown subtitleSize '"
                        + s.accessibility.subtitleSize + "' — falling back to medium.");
        s.accessibility.subtitleSize = "medium";
        clamped = true;
    }

    if (!isValidColorVisionFilter(s.accessibility.colorVisionFilter))
    {
        Logger::warning("Settings: unknown colorVisionFilter '"
                        + s.accessibility.colorVisionFilter + "' — falling back to none.");
        s.accessibility.colorVisionFilter = "none";
        clamped = true;
    }

    const float fogScaleOrig = s.accessibility.postProcess.fogIntensityScale;
    s.accessibility.postProcess.fogIntensityScale =
        std::clamp(s.accessibility.postProcess.fogIntensityScale, 0.0f, 1.0f);
    if (s.accessibility.postProcess.fogIntensityScale != fogScaleOrig)
    {
        clamped = true;
    }

    auto& ps = s.accessibility.photosensitiveSafety;
    const float flashOrig = ps.maxFlashAlpha;
    ps.maxFlashAlpha = clamp01(ps.maxFlashAlpha);
    if (ps.maxFlashAlpha != flashOrig)
    {
        clamped = true;
    }
    const float shakeOrig = ps.shakeAmplitudeScale;
    ps.shakeAmplitudeScale = clamp01(ps.shakeAmplitudeScale);
    if (ps.shakeAmplitudeScale != shakeOrig)
    {
        clamped = true;
    }
    const float bloomOrig = ps.bloomIntensityScale;
    ps.bloomIntensityScale = clamp01(ps.bloomIntensityScale);
    if (ps.bloomIntensityScale != bloomOrig)
    {
        clamped = true;
    }
    // Strobe Hz: cap at 30 Hz (well above any reasonable game setting;
    // WCAG flags >3 Hz as the safety boundary but users may still want
    // to dial above the default 2.0 for testing).
    if (ps.maxStrobeHz < 0.0f)
    {
        ps.maxStrobeHz = 0.0f;
        clamped = true;
    }
    if (ps.maxStrobeHz > 30.0f)
    {
        ps.maxStrobeHz = 30.0f;
        clamped = true;
    }

    return !clamped;
}

} // namespace

// ====== Settings::toJson / fromJson ===========================

json Settings::toJson() const
{
    json j;
    j["schemaVersion"] = schemaVersion;
    j["display"]       = displayToJson(display);
    j["audio"]         = audioToJson(audio);
    j["controls"]      = controlsToJson(controls);
    j["gameplay"]      = json{{"values", gameplay.values()}};
    j["accessibility"] = accessibilityToJson(accessibility);
    j["onboarding"]    = onboardingToJson(onboarding);
    return j;
}

bool Settings::fromJson(const json& jIn)
{
    // Migrate a copy so the caller's input isn't mutated and so a
    // failed migration doesn't leave a half-upgraded object behind.
    json j = jIn;
    if (!migrate(j))
    {
        return false;
    }
    schemaVersion = j.value("schemaVersion", kCurrentSchemaVersion);

    if (j.contains("display") && j["display"].is_object())
    {
        displayFromJson(j["display"], display);
    }
    if (j.contains("audio") && j["audio"].is_object())
    {
        audioFromJson(j["audio"], audio);
    }
    if (j.contains("controls") && j["controls"].is_object())
    {
        controlsFromJson(j["controls"], controls);
    }
    if (j.contains("gameplay") && j["gameplay"].is_object()
        && j["gameplay"].contains("values")
        && j["gameplay"]["values"].is_object())
    {
        gameplay.values() = j["gameplay"]["values"];
    }
    if (j.contains("accessibility") && j["accessibility"].is_object())
    {
        accessibilityFromJson(j["accessibility"], accessibility);
    }
    if (j.contains("onboarding") && j["onboarding"].is_object())
    {
        onboardingFromJson(j["onboarding"], onboarding);
    }

    // Validate always runs — clamps out-of-range values silently.
    validate(*this);
    return true;
}

// ====== Settings::loadFromDisk / saveAtomic ===================

namespace
{

/// @brief Detect and promote the pre-v2 `welcome_shown` flag file.
///
/// Phase 10.5 slice 14.1 moves the first-run-completion signal from
/// an ad-hoc flag file (`<configDir>/welcome_shown`, written by
/// `WelcomePanel::markAsShown`) into `OnboardingSettings` inside
/// settings.json. Users who upgrade without ever having hit Apply
/// still have the legacy file sitting next to settings.json; without
/// promotion they would be ambushed with the wizard on first launch
/// post-upgrade.
///
/// The promotion is:
///   1. If `onboarding.hasCompletedFirstRun == true`, do nothing —
///      the struct already says we're done, trust it.
///   2. If `<path.parent>/welcome_shown` exists, set
///      `onboarding.hasCompletedFirstRun = true`.
///   3. Delete the legacy file (best-effort). Failure is logged but
///      non-fatal; the promoted in-memory flag will survive the
///      next Settings save and a stale flag file is harmless.
///
/// Lossless: the signal is only cleared after the in-memory struct
/// carries it. A crash between step 2 and step 3 just means the
/// next launch re-runs the promotion — which is idempotent.
void promoteLegacyOnboardingFlag(Settings& s, const fs::path& settingsPath)
{
    if (s.onboarding.hasCompletedFirstRun)
    {
        return;
    }

    const fs::path flagPath = settingsPath.parent_path() / "welcome_shown";
    std::error_code ec;
    if (!fs::exists(flagPath, ec) || ec)
    {
        return;
    }

    s.onboarding.hasCompletedFirstRun = true;
    // completedAt is left empty on purpose — we genuinely do not know
    // when the user saw the legacy welcome panel. A future UI can
    // display "completed previously" when the field is empty but the
    // flag is true.

    std::error_code rmec;
    fs::remove(flagPath, rmec);
    if (rmec)
    {
        Logger::warning(
            "Settings: promoted legacy welcome_shown flag but could not "
            "delete " + flagPath.string() + ": " + rmec.message()
            + " (in-memory promotion stands; next launch will retry deletion).");
    }
    else
    {
        Logger::info("Settings: promoted legacy welcome_shown flag to "
                     "onboarding.hasCompletedFirstRun.");
    }
}

} // namespace

std::pair<Settings, LoadStatus> Settings::loadFromDisk(const fs::path& path)
{
    Settings result;  // starts at defaults

    std::error_code ec;
    if (!fs::exists(path, ec) || ec)
    {
        // No settings.json yet. Still honour the legacy flag — an
        // upgrader whose first post-upgrade launch predates any
        // Apply click has only the legacy signal to go on.
        promoteLegacyOnboardingFlag(result, path);
        return {result, LoadStatus::FileMissing};
    }

    // Cap at 1 MB — settings are tiny and a larger blob suggests a
    // corrupt or adversarial file.
    constexpr size_t kCapBytes = 1ULL * 1024ULL * 1024ULL;
    auto parsed = JsonSizeCap::loadJsonWithSizeCap(
        path.string(), "Settings", kCapBytes);
    if (!parsed)
    {
        // Move the corrupt file aside so the user can recover it
        // manually, then continue with defaults.
        fs::path corruptPath = path;
        corruptPath += ".corrupt";
        std::error_code rnec;
        fs::rename(path, corruptPath, rnec);
        if (rnec)
        {
            Logger::warning(
                "Settings: parse failed; could not move corrupt file to "
                + corruptPath.string() + ": " + rnec.message());
        }
        else
        {
            Logger::warning(
                "Settings: parse failed; corrupt file saved as "
                + corruptPath.string());
        }
        return {result, LoadStatus::ParseError};
    }

    if (!result.fromJson(*parsed))
    {
        // Migration failed (future version / unknown step).
        Logger::warning(
            "Settings: migration failed; using defaults. Leaving "
            + path.string() + " untouched.");
        return {Settings{}, LoadStatus::MigrationError};
    }

    promoteLegacyOnboardingFlag(result, path);
    return {std::move(result), LoadStatus::Ok};
}

SaveStatus Settings::saveAtomic(const fs::path& path) const
{
    std::string serialised;
    try
    {
        serialised = toJson().dump(2);
    }
    catch (const std::exception& e)
    {
        Logger::warning(std::string("Settings: serialisation failed: ") + e.what());
        return SaveStatus::SerializationError;
    }

    AtomicWrite::Status s = AtomicWrite::writeFile(path, serialised);
    if (s != AtomicWrite::Status::Ok)
    {
        Logger::warning(std::string("Settings: atomic write to ") + path.string()
                        + " failed: " + AtomicWrite::describe(s));
        return SaveStatus::WriteError;
    }
    return SaveStatus::Ok;
}

fs::path Settings::defaultPath()
{
    return ConfigPath::getConfigFile("settings.json");
}

} // namespace Vestige
