// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file settings_migration.cpp
/// @brief Schema migration chain for Phase 10 settings.
#include "core/settings_migration.h"

#include "core/logger.h"
#include "core/settings.h"

#include <nlohmann/json.hpp>

namespace Vestige
{

bool migrate(nlohmann::json& j)
{
    // Default to schema v1 if the field is missing — the first
    // released schema is v1 and any file without a version marker
    // almost certainly came from a pre-release build written
    // against v1 defaults.
    int version = j.value("schemaVersion", 1);

    // Guard rail: a version higher than what this build knows
    // about means the file was written by a newer engine. We
    // refuse to run so we don't silently strip fields the newer
    // build relies on. Caller logs + falls back to defaults.
    if (version > kCurrentSchemaVersion)
    {
        Logger::warning(
            "Settings migration: file schemaVersion " + std::to_string(version)
            + " is newer than this build's current "
            + std::to_string(kCurrentSchemaVersion)
            + " — refusing to migrate to avoid data loss.");
        return false;
    }

    while (version < kCurrentSchemaVersion)
    {
        switch (version)
        {
            case 1: migrate_v1_to_v2(j); break;
            case 2: migrate_v2_to_v3(j); break;
            case 3: migrate_v3_to_v4(j); break;
            case 4: migrate_v4_to_v5(j); break;
            case 5: migrate_v5_to_v6(j); break;
            case 6: migrate_v6_to_v7(j); break;
            default:
                Logger::warning(
                    "Settings migration: no migration function registered for "
                    "schemaVersion " + std::to_string(version));
                return false;
        }
        // Each migration function is responsible for bumping the
        // version field. Re-read it so the loop terminates on any
        // (possibly buggy) function that fails to bump.
        int next = j.value("schemaVersion", version + 1);
        if (next <= version)
        {
            Logger::warning(
                "Settings migration: version did not advance past "
                + std::to_string(version) + "; aborting to avoid infinite loop.");
            return false;
        }
        version = next;
    }

    // Pin the version field so an in-memory struct that never
    // carried a version marker serializes cleanly next time.
    j["schemaVersion"] = kCurrentSchemaVersion;
    return true;
}

void migrate_v1_to_v2(nlohmann::json& j)
{
    // Insert the `onboarding` section if absent. If a v1 file
    // somehow already carries an `onboarding` key we leave it
    // alone — fromJson will reconcile via `j.value(key, default)`.
    if (!j.contains("onboarding") || !j["onboarding"].is_object())
    {
        j["onboarding"] = nlohmann::json{
            {"hasCompletedFirstRun", false},
            {"completedAt",          ""},
            {"skipCount",            0},
        };
    }
    j["schemaVersion"] = 2;
}

void migrate_v2_to_v3(nlohmann::json& j)
{
    // Insert the `localization` section if absent. If a v2 file
    // somehow already carries a `localization` key we leave it
    // alone — fromJson reconciles via `j.value(key, default)`.
    if (!j.contains("localization") || !j["localization"].is_object())
    {
        j["localization"] = nlohmann::json{
            {"language", "en"},
        };
    }
    j["schemaVersion"] = 3;
}

void migrate_v3_to_v4(nlohmann::json& j)
{
    // AX8 — add `audio.outputLayout` (default "auto") if absent. "auto"
    // is the current-behaviour default (driver downmix), so a v3 file is
    // unchanged in effect. fromJson would default a missing field anyway;
    // we set it explicitly to mirror the other arms and keep the on-disk
    // document self-describing. The audio section already exists in v3.
    // AX6 rides the same v4 bump — `audio.airAbsorptionEnabled` defaults
    // to true (on), the current-behaviour value, so a v3 file is again
    // unchanged in effect.
    if (j.contains("audio") && j["audio"].is_object())
    {
        if (!j["audio"].contains("outputLayout"))
        {
            j["audio"]["outputLayout"] = "auto";
        }
        if (!j["audio"].contains("airAbsorptionEnabled"))
        {
            j["audio"]["airAbsorptionEnabled"] = true;
        }
        // AX5 also rides the v4 bump — `audio.lodEnabled` defaults on.
        if (!j["audio"].contains("lodEnabled"))
        {
            j["audio"]["lodEnabled"] = true;
        }
        // AX11 rides the same v4 bump — `audio.deviceHotSwap` defaults to
        // "notify" (toast + confirm), the safe mid-session policy. fromJson
        // would default a missing key anyway; set it here to keep the
        // on-disk document self-describing, mirroring the arms above.
        if (!j["audio"].contains("deviceHotSwap"))
        {
            j["audio"]["deviceHotSwap"] = "notify";
        }
        // AX9 rides the same v4 bump — loudness normalisation defaults on at
        // −16 LUFS (game norm). Self-describing arms mirror the ones above.
        if (!j["audio"].contains("loudnessEnabled"))
        {
            j["audio"]["loudnessEnabled"] = true;
        }
        if (!j["audio"].contains("loudnessTargetLufs"))
        {
            j["audio"]["loudnessTargetLufs"] = -16.0;
        }
    }
    j["schemaVersion"] = 4;
}

void migrate_v4_to_v5(nlohmann::json& j)
{
    // AX2 R4 — add the three reverb settings. All default to the
    // current-behaviour value (reverb on, wet cap 0.5, convolution allowed), so
    // a v4 file is unchanged in effect. fromJson would default a missing key
    // anyway; the explicit arms keep the on-disk document self-describing,
    // mirroring the v3→v4 arms above.
    if (j.contains("audio") && j["audio"].is_object())
    {
        if (!j["audio"].contains("reverbEnabled"))
        {
            j["audio"]["reverbEnabled"] = true;
        }
        if (!j["audio"].contains("reverbWetCap"))
        {
            j["audio"]["reverbWetCap"] = 0.5;
        }
        if (!j["audio"].contains("reverbConvolutionEnabled"))
        {
            j["audio"]["reverbConvolutionEnabled"] = true;
        }
    }
    j["schemaVersion"] = 5;
}

void migrate_v5_to_v6(nlohmann::json& j)
{
    // 3D_E-0729 — self-update preferences. "ask" so an existing user is asked
    // once, like a new one, rather than checked without having chosen.
    if (!j.contains("updates") || !j["updates"].is_object())
    {
        j["updates"] = nlohmann::json{{"mode", "ask"}, {"skippedVersion", ""}};
    }
    j["schemaVersion"] = 6;
}

void migrate_v6_to_v7(nlohmann::json& j)
{
    // 3D_E-0035 — per-option graphics settings. Before v7 only the preset
    // and render scale were saved, so a Custom player's options were lost;
    // High's row is the nearest to what the renderers defaulted to.
    if (j.contains("display") && j["display"].is_object()
        && !j["display"].contains("graphics"))
    {
        const QualityPreset preset = qualityPresetFromString(
            j["display"].value("qualityPreset", std::string("high")), QualityPreset::High);
        nlohmann::json graphics;
        graphicsToJson(qualityRowFor(preset).graphics, graphics);
        j["display"]["graphics"] = graphics;
    }
    j["schemaVersion"] = 7;
}

} // namespace Vestige
