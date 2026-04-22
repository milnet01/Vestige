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
            // case 1: migrate_v1_to_v2(j); break;
            // case 2: migrate_v2_to_v3(j); break;
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

} // namespace Vestige
