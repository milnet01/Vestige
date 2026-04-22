// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file settings.h
/// @brief Phase 10 — persistent, user-editable engine settings.
///
/// This is slice 13.1 of the Phase 10 Settings work: the
/// persistence primitive itself. It defines the in-memory struct,
/// the JSON wire format (schema v1), and the load / save / validate
/// / migrate lifecycle. It intentionally does **not** apply settings
/// to any subsystem — slices 13.2 – 13.5 add the per-subsystem
/// apply paths (video, audio + accessibility, input bindings, UI).
///
/// File location:
///  - Linux:   `$XDG_CONFIG_HOME/vestige/settings.json`
///             (fallback `$HOME/.config/vestige/settings.json`).
///  - Windows: `%LOCALAPPDATA%\Vestige\settings.json`.
///  - Resolved via `ConfigPath::getConfigDir()` (utils/config_path.h).
///
/// Schema evolution:
///  - `schemaVersion` is stored at the document root. Current value
///    is `kCurrentSchemaVersion` (see below).
///  - Older files migrate forward through a chain of
///    `settings_migration.h` functions on load; the struct then
///    receives the migrated values.
///  - Unknown fields are ignored on load (forward-compat); missing
///    fields get defaults from the struct initialiser
///    (backward-compat). This keeps a newer build reading an older
///    settings.json and vice-versa both crash-free.
///
/// Write durability:
///  - Saves go through `AtomicWrite::writeFile` so a crash mid-save
///    leaves either the previous file or the new one, never a
///    truncated hybrid.
///
/// See `docs/PHASE10_SETTINGS_DESIGN.md` for the full rationale
/// and the sign-off log.
#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace Vestige
{

/// @brief Current on-disk schema version. Bumped whenever the
///        shape of `Settings` changes in a way that requires a
///        migration function to read old files.
inline constexpr int kCurrentSchemaVersion = 1;

// --------------------------------------------------------------
// Display section — resolution, vsync, fullscreen, quality.
// --------------------------------------------------------------

/// @brief Engine-wide quality tier. Game code reads this to pick
///        shader variants, LOD bias, shadow resolution, etc.
enum class QualityPreset
{
    Low,
    Medium,
    High,
    Ultra,
    Custom,   ///< User has hand-tweaked knobs in an advanced panel.
};

const char* qualityPresetLabel(QualityPreset q);
QualityPreset qualityPresetFromString(const std::string& s, QualityPreset fallback = QualityPreset::Medium);
std::string qualityPresetToString(QualityPreset q);

struct DisplaySettings
{
    int  windowWidth   = 1920;
    int  windowHeight  = 1080;
    bool fullscreen    = false;
    bool vsync         = true;
    QualityPreset qualityPreset = QualityPreset::High;

    /// Render-resolution scale factor applied before upscaling to the
    /// window size. 1.0 = native, 0.5 = half-res, 2.0 = DSR-style.
    /// Clamped to [0.25, 2.0] at load.
    float renderScale  = 1.0f;

    bool operator==(const DisplaySettings& o) const;
    bool operator!=(const DisplaySettings& o) const { return !(*this == o); }
};

// --------------------------------------------------------------
// Audio section — mixer bus gains + HRTF toggle.
// --------------------------------------------------------------

/// @brief Six-bus gain table mirroring `AudioMixer::busGain`.
///        Index order matches `enum class AudioBus`
///        (Master / Music / Voice / Sfx / Ambient / Ui).
struct AudioSettings
{
    std::array<float, 6> busGains = {
        1.0f,  // Master
        0.8f,  // Music
        1.0f,  // Voice
        0.9f,  // Sfx
        0.7f,  // Ambient
        0.7f,  // Ui
    };

    bool hrtfEnabled = true;

    bool operator==(const AudioSettings& o) const;
    bool operator!=(const AudioSettings& o) const { return !(*this == o); }
};

// --------------------------------------------------------------
// Controls section — mouse sensitivity, deadzones, bindings.
// --------------------------------------------------------------

/// @brief A single physical-input binding in the wire format.
///
/// `device` is one of `"keyboard"`, `"mouse"`, `"gamepad"`,
/// `"none"`. `scancode` is a GLFW scan code for keyboards (Godot's
/// `physical_keycode` convention — preserves WASD across AZERTY /
/// Dvorak), a GLFW mouse-button index for Mouse, a GLFW
/// gamepad-button index for Gamepad, and ignored for None.
///
/// Slice 13.1 ships the pure round-trip; slice 13.4 hooks this to
/// `InputActionMap::{toJson, fromJson}` and the keybinding UI.
struct InputBindingWire
{
    std::string device   = "none";
    int         scancode = -1;

    bool operator==(const InputBindingWire& o) const
    {
        return device == o.device && scancode == o.scancode;
    }
    bool operator!=(const InputBindingWire& o) const { return !(*this == o); }
};

/// @brief One action + its three binding slots. `id` matches a
///        registered `InputAction::id`; unknown ids are dropped on
///        load with a logged warning.
struct ActionBindingWire
{
    std::string        id;
    InputBindingWire   primary;
    InputBindingWire   secondary;
    InputBindingWire   gamepad;

    bool operator==(const ActionBindingWire& o) const
    {
        return id == o.id
            && primary   == o.primary
            && secondary == o.secondary
            && gamepad   == o.gamepad;
    }
    bool operator!=(const ActionBindingWire& o) const { return !(*this == o); }
};

struct ControlsSettings
{
    float mouseSensitivity     = 1.0f;
    bool  invertY              = false;
    float gamepadDeadzoneLeft  = 0.15f;
    float gamepadDeadzoneRight = 0.10f;

    /// Empty by default — game code registers actions during
    /// initialisation and the first save captures them.
    std::vector<ActionBindingWire> bindings;

    bool operator==(const ControlsSettings& o) const;
    bool operator!=(const ControlsSettings& o) const { return !(*this == o); }
};

// --------------------------------------------------------------
// Gameplay section — opaque string→JsonValue map.
// --------------------------------------------------------------

/// @brief Untyped gameplay knobs. The engine does not dictate
///        gameplay settings; each game project reads what it wrote.
///
/// Stored as a nlohmann::json object on the heap so the header
/// doesn't drag the json include into every consumer. Ownership
/// is simple value semantics (deep copy on assign).
class GameplaySettings
{
public:
    GameplaySettings();
    GameplaySettings(const GameplaySettings& other);
    GameplaySettings(GameplaySettings&&) noexcept;
    GameplaySettings& operator=(const GameplaySettings& other);
    GameplaySettings& operator=(GameplaySettings&&) noexcept;
    ~GameplaySettings();

    /// @brief Access the underlying JSON object. Always an object
    ///        (never null, never array). Safe to call `operator[]`
    ///        on the result.
    nlohmann::json&       values();
    const nlohmann::json& values() const;

    bool operator==(const GameplaySettings& o) const;
    bool operator!=(const GameplaySettings& o) const { return !(*this == o); }

private:
    // Pimpl so we don't pull `<nlohmann/json.hpp>` into every TU
    // that includes `settings.h`. The Impl is a tiny wrapper around
    // nlohmann::json.
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// --------------------------------------------------------------
// Accessibility section — all the a11y toggles in one place.
// --------------------------------------------------------------

/// @brief Subset of the six-bus-style post-process flags persisted
///        to settings. Mirrors `PostProcessAccessibilitySettings`
///        so it can serialise without pulling a renderer header.
struct PostProcessAccessibilityWire
{
    bool  depthOfFieldEnabled = true;
    bool  motionBlurEnabled   = true;
    bool  fogEnabled          = true;
    float fogIntensityScale   = 1.0f;
    bool  reduceMotionFog     = false;

    bool operator==(const PostProcessAccessibilityWire& o) const;
    bool operator!=(const PostProcessAccessibilityWire& o) const { return !(*this == o); }
};

/// @brief Photosensitivity-safe-mode toggles persisted to settings.
///        Field names mirror `PhotosensitiveLimits` so the apply
///        step in slice 13.3 is a direct copy.
struct PhotosensitiveSafetyWire
{
    bool  enabled             = false;
    float maxFlashAlpha       = 0.25f;
    float shakeAmplitudeScale = 0.25f;
    float maxStrobeHz         = 2.0f;
    float bloomIntensityScale = 0.6f;

    bool operator==(const PhotosensitiveSafetyWire& o) const;
    bool operator!=(const PhotosensitiveSafetyWire& o) const { return !(*this == o); }
};

struct AccessibilitySettings
{
    /// UIScalePreset on disk: "1.0x" / "1.25x" / "1.5x" / "2.0x".
    std::string uiScalePreset   = "1.0x";
    bool        highContrast    = false;
    bool        reducedMotion   = false;
    bool        subtitlesEnabled = true;
    /// SubtitleSizePreset on disk: "small" / "medium" / "large" / "xl".
    std::string subtitleSize   = "medium";
    /// ColorVisionMode on disk: "none" / "protanopia" / "deuteranopia" / "tritanopia".
    std::string colorVisionFilter = "none";

    PhotosensitiveSafetyWire     photosensitiveSafety;
    PostProcessAccessibilityWire postProcess;

    bool operator==(const AccessibilitySettings& o) const;
    bool operator!=(const AccessibilitySettings& o) const { return !(*this == o); }
};

// --------------------------------------------------------------
// Settings root
// --------------------------------------------------------------

/// @brief Result of a load attempt. Logging policy lives with
///        the caller — `Settings::loadFromDisk` returns the code,
///        the engine's bootstrap logs + decides fallback.
enum class LoadStatus
{
    Ok,                  ///< Parsed, migrated, validated cleanly.
    FileMissing,         ///< No file at the given path. Defaults returned.
    ParseError,          ///< JSON malformed — `.corrupt` sidecar written, defaults returned.
    MigrationError,      ///< Migration chain aborted — defaults returned.
};

/// @brief Result of a save attempt.
enum class SaveStatus
{
    Ok,
    SerializationError,  ///< JSON serialisation threw — no file written.
    WriteError,          ///< Atomic-write utility failed — old file intact.
};

/// @brief Root settings aggregate.
struct Settings
{
    int                    schemaVersion = kCurrentSchemaVersion;
    DisplaySettings        display;
    AudioSettings          audio;
    ControlsSettings       controls;
    GameplaySettings       gameplay;
    AccessibilitySettings  accessibility;

    /// @brief Loads settings from `path`. Returns the parsed
    ///        settings (or defaults on failure) plus a status code.
    ///
    /// On `ParseError` the corrupt file is moved to
    /// `<path>.corrupt` so the user can recover it manually.
    static std::pair<Settings, LoadStatus> loadFromDisk(const std::filesystem::path& path);

    /// @brief Writes the settings to `path` atomically.
    SaveStatus saveAtomic(const std::filesystem::path& path) const;

    /// @brief Default on-disk location for settings.
    ///        Equivalent to `ConfigPath::getConfigFile("settings.json")`.
    static std::filesystem::path defaultPath();

    /// @brief Serialises the settings to a `nlohmann::json` tree.
    ///        Exposed so callers can embed the settings into a
    ///        larger document (e.g. a bug report). Round-trip with
    ///        `fromJson` is lossless.
    nlohmann::json toJson() const;

    /// @brief Parses from an already-loaded json tree. Runs the
    ///        migration + validation chain. Returns whether any
    ///        fields were clamped / dropped during validation.
    bool fromJson(const nlohmann::json& j);

    bool operator==(const Settings& o) const;
    bool operator!=(const Settings& o) const { return !(*this == o); }
};

} // namespace Vestige
